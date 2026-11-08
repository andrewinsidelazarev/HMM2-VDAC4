#include "hmm_clock.h"
/* Codex: стенд ПК использует игровой платформенный слой и пишет палитровые кадры для сравнения.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
extern "C" int hmm_exception_pending(void);
extern "C" volatile int hmm_running;
#ifdef HMM2_PACKED
extern "C" void hmm_gpu_shutdown();
#endif
#include <SDL.h>
#include <filesystem>
#include <cstdio>
// Клод: на ПК (HMM2_BIG_LOG) — каждое выделение от 128 КиБ одним куском со стеком вызовов (адреса относительно начала программы,
// разбор по hmm2_host.map): на плате такие куски не находятся в раздробленной PSRAM (книга заклинаний, загрузка Ghostplt).
#include <new>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <windows.h>
static std::atomic<bool> reject_unwind_alloc{false};
static std::atomic<bool> reject_restore_alloc{false};
static const auto allocation_main_thread=std::this_thread::get_id();
void hmm_host_reject_unwind_alloc(bool enabled){reject_unwind_alloc.store(enabled);}
void hmm_host_reject_restore_alloc(bool enabled){reject_restore_alloc.store(enabled);}
static void hmm_big_log(size_t n){
    if(reject_unwind_alloc.load()&&std::uncaught_exceptions())throw std::bad_alloc();
    if(reject_restore_alloc.load()&&std::this_thread::get_id()==allocation_main_thread)throw std::bad_alloc();
    static const bool on=std::getenv("HMM2_BIG_LOG")!=nullptr;if(!on||n<128*1024)return;
    void *frames[14];const unsigned k=RtlCaptureStackBackTrace(2,14,frames,nullptr);const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("BIG ALLOC %zu stack",n);for(unsigned i=0;i<k;++i)std::printf(" %llx",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i])-base));
    std::printf("%c",10);std::fflush(stdout);
}
void *operator new(size_t n){hmm_big_log(n);if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n){hmm_big_log(n);if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,size_t)noexcept{std::free(p);}
void operator delete[](void *p,size_t)noexcept{std::free(p);}

#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include "screen.h"
#include "image_palette.h"
#include <exception>
#ifdef _WIN64
#define NOMINMAX
#include <windows.h>
static LONG WINAPI crash(EXCEPTION_POINTERS *e){
    // Codex: адреса аварийного хостового стека сопоставляются с MAP этой же сборки.
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));void *frames[32];
    unsigned count=CaptureStackBackTrace(0,32,frames,nullptr);
    std::fprintf(stderr,"HOST CRASH %08lx RIP=%llx BASE=%llx\n",e->ExceptionRecord->ExceptionCode,(unsigned long long)e->ContextRecord->Rip,(unsigned long long)base);
    for(unsigned i=0;i<count;++i)std::fprintf(stderr,"STACK RVA=%llx\n",(unsigned long long)(reinterpret_cast<uintptr_t>(frames[i])-base));
    std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
#endif
#ifndef HMM2_PACKED
struct hmm_file { FILE *file; };
#endif
static unsigned frames,limit=30;
unsigned hmm_host_frame_count(){return frames;}
static std::string output;
static std::filesystem::path root;
static uint64_t tick0;
static uint64_t virtual_tick;
static bool scripted_time(){return std::getenv("HMM2_TEST_MAP")||std::getenv("HMM2_TEST_UI")||std::getenv("HMM2_AUDIO_TEST");}
extern const char *hmm_test_phase;
void hmm_scene_events();
static std::filesystem::path resolve(const char *name) {
    std::filesystem::path p=std::filesystem::path(name).lexically_normal();
    if(p.is_absolute())return p;
    const std::string s=p.generic_string();
    if(s=="files/save"||s.compare(0,11,"files/save/")==0)return root/p;
    if(s=="hmm2.gfx")return std::filesystem::path("E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.GFX");
    if(s=="data"||s.compare(0,5,"data/")==0)return std::filesystem::path("E:/zx/HMM2/Assets/Original/DATA")/s.substr(s=="data"?4:5);
    if(s=="maps"||s.compare(0,5,"maps/")==0)return std::filesystem::path("E:/zx/HMM2/Assets/Original/MAPS")/s.substr(s=="maps"?4:5);
    if(s=="files"||s.compare(0,6,"files/")==0)return std::filesystem::path("E:/zx/HMM2 VDAC4/third_party/openhmm2/files")/s.substr(s=="files"?5:6);
    return root/p;
}
uint64_t hmm_millis(){if(scripted_time())return virtual_tick;return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void hmm_delay(uint32_t ms){if(scripted_time()){virtual_tick+=ms;return;}std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
void hmm_log(const char *s){std::printf("HMM2 %llu: %s\n",static_cast<unsigned long long>(hmm_millis()-tick0),s);std::fflush(stdout);}
int hmm_video_open(){
#ifdef HMM2_PACKED
    hmm_gpu_init();
#endif
    return 1;
}
void hmm_video_close(){}
#ifdef HMM2_PACKED
void hmm_host_capture_cached_cursor(){
    ++frames;extern void hmm_host_trace_frame(unsigned);hmm_host_trace_frame(frames);
    if(std::getenv("HMM2_TEST_ADVENTURE_AUDIT"))std::printf("ADVENTURE_VISIBLE phase=%s frame=%u\n",hmm_test_phase,frames);
    char name[512];std::snprintf(name,sizeof(name),"%s/frame_%04u.pal",output.c_str(),frames);
    FILE *f=std::fopen(name,"wb");std::fwrite(fheroes2::getGamePalette(),1,768,f);std::fclose(f);
}
#endif
void hmm_video_frame(const uint8_t *pixels,const uint8_t *rgb,int w,int h){
    if(!hmm_running||hmm_exception_pending())return;
#ifdef HMM2_PACKED
    const auto gpu_begin=std::chrono::steady_clock::now();
    static auto native_returned=gpu_begin;
    if(std::getenv("HMM2_NATIVE_PROFILE"))std::printf("NATIVE_ENGINE frame=%u us=%lld phase=%s\n",frames+1,(long long)std::chrono::duration_cast<std::chrono::microseconds>(gpu_begin-native_returned).count(),hmm_test_phase);
    hmm_gpu_frame(pixels,rgb,w,h);
    if(std::getenv("HMM2_NATIVE_PROFILE"))std::printf("NATIVE_GPU frame=%u us=%lld phase=%s\n",frames+1,(long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-gpu_begin).count(),hmm_test_phase);
    extern void hmm_host_trace_frame(unsigned);hmm_host_trace_frame(frames+1);
    // Codex: необязательный CPU-оракул только для проверки слоёв, не для измерения производительности.
    if(!pixels&&std::getenv("HMM2_CANVAS_ORACLE"))pixels=static_cast<const fheroes2::Display&>(fheroes2::Display::instance()).image();
#endif
    ++frames;char name[512];FILE *f;
    if(pixels){std::snprintf(name,sizeof(name),"%s/frame_%04u.idx",output.c_str(),frames);
    f=std::fopen(name,"wb");if(!f)std::abort();std::fwrite(pixels,1,size_t(w)*h,f);std::fclose(f);}
    std::snprintf(name,sizeof(name),"%s/frame_%04u.pal",output.c_str(),frames);f=std::fopen(name,"wb");std::fwrite(rgb,1,768,f);std::fclose(f);
    uint32_t hash=2166136261u;if(pixels)for(int i=0;i<w*h;i++)hash=(hash^pixels[i])*16777619u;
    std::printf("HMM2 frame=%u size=%dx%d hash=%08x phase=%s\n",frames,w,h,hash,hmm_test_phase);std::fflush(stdout);
#ifdef HMM2_PACKED
    native_returned=std::chrono::steady_clock::now();
#endif
}

extern "C" void hmm_input_init(){}
extern "C" uint32_t hmm_input_polled(){return 0;}
extern "C" void hmm_loading_mouse_begin(int,int){}
extern "C" void hmm_loading_mouse(int *x,int *y){
    // Codex: SDL оставляет клавиши и кнопки штатному обработчику.
    SDL_PumpEvents();SDL_Event e;int px,py;SDL_GetMouseState(&px,&py);
    while(SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_MOUSEMOTION,SDL_MOUSEMOTION)>0){px=e.motion.x;py=e.motion.y;}
    *x=px;*y=py;
}
int hmm_poll_event(void *p){
    auto *e=static_cast<SDL_Event*>(p);
    if(const char *folder=std::getenv("HMM2_TEST_UI")){
        // Codex: обычные меню получают те же нажатия, что QEMU. Это проверяет переходы,
        // которые прямой сценарий карты намеренно не вызывает.
        static uint8_t keys[64];static uint32_t times[64];static unsigned count=0,at=0;static bool loaded=false;
        if(!loaded){loaded=true;FILE*f=std::fopen((std::string(folder)+"/ui_keys.bin").c_str(),"rb");
            if(f){count=unsigned(std::fread(keys,1,64,f));std::fclose(f);}
            f=std::fopen((std::string(folder)+"/ui_times.bin").c_str(),"rb");
            if(f){std::fread(times,4,count,f);std::fclose(f);}
        }
        if(at<count&&virtual_tick>=times[at]){
            SDL_Keycode k=0;switch(keys[at]){
                case 0x31:k=SDLK_n;break;case 0x3a:k=SDLK_m;break;case 0x33:k=SDLK_h;break;
                case 0x1e:k=SDLK_2;break;case 0x26:k=SDLK_3;break;case 0x25:k=SDLK_4;break;
                case 0x2e:k=SDLK_5;break;case 0x36:k=SDLK_6;break;case 0x1b:k=SDLK_s;break;
                case 0x5a:k=SDLK_RETURN;break;case 0x2c:k=SDLK_t;break;case 0x76:k=SDLK_ESCAPE;break;
                case 0x06:k=SDLK_F2;break;case 0x32:k=SDLK_b;break;case 0x1c:k=SDLK_a;break;
                case 0x24:k=SDLK_e;break;case 0x4b:k=SDLK_l;break;case 0x72:k=SDLK_KP_2;break;
                case 0x1d:k=SDLK_w;break;
            }
            std::printf("HOST UI PS2=%02x at=%llu\n",keys[at++],static_cast<unsigned long long>(virtual_tick));
            SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=k;SDL_PushEvent(&key);key.type=SDL_KEYUP;SDL_PushEvent(&key);
        }
        // Codex: тот же ввод мышью, что в QEMU, запускает настоящий цикл хода героя.
        // Нужен для сравнения повторённых статических частей FT812 с полной записью.
        struct Click {uint32_t time,x,y,button;};
        static Click clicks[512];static unsigned click_count=0,click_at=0;static bool clicks_loaded=false;
        static uint64_t release_at=0;static uint8_t release_button=0;static int release_x=0,release_y=0;
        if(!clicks_loaded){clicks_loaded=true;FILE *f=std::fopen((std::string(folder)+"/ui_clicks.bin").c_str(),"rb");
            if(f){click_count=unsigned(std::fread(clicks,sizeof(Click),512,f));std::fclose(f);}}
        if(release_at&&virtual_tick>=release_at){SDL_Event mouse{};mouse.type=SDL_MOUSEBUTTONUP;mouse.button.button=release_button;mouse.button.x=release_x;mouse.button.y=release_y;SDL_PushEvent(&mouse);release_at=0;}
        if(!release_at&&click_at<click_count&&virtual_tick>=clicks[click_at].time){const auto &c=clicks[click_at++];
            SDL_Event mouse{};mouse.type=SDL_MOUSEMOTION;mouse.motion.x=int(c.x);mouse.motion.y=int(c.y);SDL_PushEvent(&mouse);
            // Codex: кнопка 0 соответствует движению без щелчка в стенде QEMU.
            if(c.button){mouse={};mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.state=SDL_PRESSED;mouse.button.button=c.button==2?SDL_BUTTON_RIGHT:SDL_BUTTON_LEFT;
            mouse.button.x=int(c.x);mouse.button.y=int(c.y);SDL_PushEvent(&mouse);
            release_button=mouse.button.button;release_x=int(c.x);release_y=int(c.y);release_at=virtual_tick+8;}
            std::printf("HOST UI click=%u x=%u y=%u at=%llu\n",click_at,c.x,c.y,static_cast<unsigned long long>(virtual_tick));
        }
    }
    // Codex: обычный переход главного меню через SDL, отдельно от прямого сценария карты.
    static bool pressed=false;
    if(!pressed&&frames>=25&&std::getenv("HMM2_TEST_NEWGAME")){
        pressed=true;SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_n;SDL_PushEvent(&key);
        key.type=SDL_KEYUP;SDL_PushEvent(&key);
    }
    // Клод: HMM2_TEST_MOUSE — мышь водится по экрану с 30-го кадра, как рукой по меню на плате.
    // На плате список слоёв меню рос до переполнения DL, а на стенде без движения мыши — нет.
    if(std::getenv("HMM2_TEST_MOUSE")&&frames>=30){
        static int t=0,px=320,py=240;++t;
        const int x=320+int(280*std::sin(t*0.07)),y=240+int(200*std::sin(t*0.043));
        SDL_Event m{};m.type=SDL_MOUSEMOTION;m.motion.x=x;m.motion.y=y;m.motion.xrel=x-px;m.motion.yrel=y-py;px=x;py=y;
        SDL_PushEvent(&m);
    }
    hmm_scene_events();
    if(scripted_time())++virtual_tick;
    if(frames>=limit){std::printf("HMM2 стенд: достигнут предел кадров=%u\n",frames);std::fflush(stdout);
#ifdef HMM2_PACKED
        hmm_gpu_shutdown();
#endif
        std::exit(0);}
    const int found=SDL_PollEvent(e);
    if(found&&std::getenv("HMM2_EVENT_TRACE")&&(e->type==SDL_KEYDOWN||e->type==SDL_KEYUP))
        std::printf("HOST KEY type=%u sym=%d phase=%s\n",e->type,e->key.keysym.sym,hmm_test_phase);
    return found;
}
int hmm_modifiers(){return SDL_GetModState();}
int main(int argc,char **argv){
#ifdef _WIN64
    SetUnhandledExceptionFilter(crash);
    std::set_terminate([]{
        try {if(auto e=std::current_exception())std::rethrow_exception(e);}
        catch(const std::exception &e){std::fprintf(stderr,"HOST TERMINATE: %s\n",e.what());}
        catch(...){std::fprintf(stderr,"HOST TERMINATE: unknown exception\n");}
        void *stack[32];unsigned n=CaptureStackBackTrace(0,32,stack,nullptr);auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        for(unsigned i=0;i<n;++i)std::fprintf(stderr,"STACK RVA=%llx\n",(unsigned long long)(reinterpret_cast<uintptr_t>(stack[i])-base));
        std::fflush(stderr);std::_Exit(99);
    });
#endif
    if(argc<2)return 2;output=argv[1];root=std::filesystem::path(output)/"run";
    std::filesystem::create_directories(root);if(argc>2)limit=unsigned(std::strtoul(argv[2],nullptr,10));
    tick0=hmm_millis();SDL_SetMainReady();SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS);
    extern void hmm_host_files_init(const char*);hmm_host_files_init(root.string().c_str());
    const uint8_t rtc[]={2,0,0,0x14,0x29,0x10,0x26,0x80};hmm_clock_reset();hmm_clock_sync(rtc,sizeof(rtc));
    if(std::getenv("HMM2_AUDIO_TEST")){extern int hmm_run_audio_test();return hmm_run_audio_test();}
    char name[]="HMM2.ELF";char *args[]={name,nullptr};int r=hmm_game_main(1,args);
#ifdef HMM2_PACKED
    hmm_gpu_shutdown();
#endif
    SDL_Quit();
    extern void hmm_host_prefetch_stats();hmm_host_prefetch_stats();
    std::printf("HMM2 exit=%d frames=%u\n",r,frames);return (frames||std::getenv("HMM2_PACK_OUT"))? r:3;
}
