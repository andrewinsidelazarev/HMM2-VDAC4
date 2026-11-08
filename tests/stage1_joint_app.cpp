/* Codex: отдельная DLL игры выгружается между запусками; модель ESP продолжает работать. */
#include "hmm_platform.h"
#include "world.h"
#include "serialize.h"
#include "stage1_joint_app.h"
extern "C" {
#include "hmm_core.h"
}
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <SDL.h>
extern const char *hmm_test_phase;
extern void hmm_scene_events();
extern "C" int hmm_scene_run();
static uint64_t virtual_tick;
static bool scripted;
static std::string output;
static std::atomic<unsigned> frame_count{0};
static std::atomic<int> world_ready{0};
static void (*frame_marker)(unsigned);
// Codex: реальный файловый слой ELF и настоящий FAT32; отказы вводит виртуальная SD стенда.
static int saves_probe(){
    const char *mode=std::getenv("HMM2_SAVES_PROBE");
    const bool fault=std::string(mode)!="ok";
    std::vector<unsigned char> bytes(180*1024+137),readback(bytes.size());
    for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((i*79)^(i>>7));
    hmm_file *f=hmm_fopen("PROBE.SAV","wb");if(!f)throw std::runtime_error("probe create");
    const size_t written=hmm_fwrite(bytes.data(),1,bytes.size(),f);
    const int closed=hmm_fclose(f);
    if(fault){
        if(written==bytes.size()&&closed==0)throw std::runtime_error("probe ignored fault");
        hmm_core->log("SAVES PROBE expected error mode=%s written=%u close=%d",mode,unsigned(written),closed);
    }else{
        if(written!=bytes.size()||closed)throw std::runtime_error("probe write");
        f=hmm_fopen("PROBE.SAV","rb");if(!f)throw std::runtime_error("probe reopen");
        if(hmm_fread(readback.data(),1,bytes.size(),f)!=bytes.size()||readback!=bytes)throw std::runtime_error("probe bytes");
        if(hmm_fread(readback.data(),1,1,f))throw std::runtime_error("probe EOF");
        if(hmm_fseek(f,73,0)||hmm_fread(readback.data(),1,17,f)!=17||std::memcmp(readback.data(),bytes.data()+73,17))throw std::runtime_error("probe seek");
        if(hmm_fclose(f))throw std::runtime_error("probe read close");
        unsigned char tail[16];
        if(!hmm_fs_call(1,"PROBE.SAV",nullptr,uint32_t(bytes.size()-8),tail,sizeof(tail),nullptr))throw std::runtime_error("probe short accepted");
        if(hmm_fs_call(6,"PROBE.SAV",nullptr,0,nullptr,0,nullptr))throw std::runtime_error("probe short close");
        hmm_core->log("SAVES PROBE exact bytes=%u short_read=rejected",unsigned(bytes.size()));
    }
    hmm_core->log("SAVES PROBE COMPLETE");return 1;
}
extern "C" {
const zm_core_t *hmm_core;
// Codex: нативный стенд использует CRT; счётчики собственного аллокатора ELF здесь нулевые.
size_t hmm_heap_big_bytes,hmm_heap_big_peak,hmm_heap_big_count;
// Codex: в нативном совместном стенде нет распределителя PSRAM целевого ELF.
size_t hmm_heap_mid_bytes,hmm_heap_mid_peak,hmm_heap_small_bytes;
unsigned hmm_heap_small_pages(void){return 0;}
volatile int hmm_running,hmm_done,hmm_state;
uint32_t hmm_package_size,hmm_frames;
char hmm_error[96];
uint64_t hmm_millis(){
    /* Клод: HMM2_TEST_REALTIME — сценарий по настоящим часам: пропуск кадров роликов зависит от реального времени. */
    if(scripted&&!std::getenv("HMM2_TEST_REALTIME"))return virtual_tick;
    /* Codex: CRT вызывает конструкторы до передачи ABI; часы доступны и на этом этапе. */
    return hmm_core?hmm_core->millis():uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void hmm_delay(uint32_t ms){if(scripted&&!std::getenv("HMM2_TEST_REALTIME"))virtual_tick+=ms;else hmm_core->delay(ms);}
void hmm_log(const char*s){hmm_core->log("game %s",s);}
/* Codex: деструкторы нативного C++ исполняет CRT при FreeLibrary, после join задачи игры. */
void hmm_runtime_close(){}
int hmm_host_ready(){hmm_core->log("GAME_READY");return std::getenv("HMM2_SAVES_PROBE")?saves_probe():scripted?hmm_scene_run():0;}
int joint_poll_event_actual(void*);
int hmm_poll_event(void*p){if(scripted){hmm_scene_events();++virtual_tick;if(SDL_PollEvent(static_cast<SDL_Event*>(p)))return 1;}return joint_poll_event_actual(p);}
void joint_video_frame_actual(const uint8_t*,const uint8_t*,int,int);
void hmm_video_frame(const uint8_t*p,const uint8_t*pal,int w,int h){
    joint_video_frame_actual(p,pal,w,h);
    unsigned f=frame_count.load()+1;
    if(world.w()>0&&world.h()>0&&world.CountDay()>0)world_ready=1;
    char path[1024];FILE *file;
    if(p){std::snprintf(path,sizeof(path),"%s/frame_%04u.idx",output.c_str(),f);
        file=std::fopen(path,"wb");if(!file)std::abort();std::fwrite(p,1,size_t(w)*h,file);std::fclose(file);}
    std::snprintf(path,sizeof(path),"%s/frame_%04u.pal",output.c_str(),f);
    file=std::fopen(path,"wb");if(!file)std::abort();std::fwrite(pal,1,768,file);std::fclose(file);
    hmm_core->log("FRAME %u %dx%d phase=%s",f,w,h,scripted?hmm_test_phase:"ui");
    if(world_ready&&f==200){
        RWStreamBuf raw;raw.setBigendian(true);raw<<world;
        file=std::fopen((output+"/world.be").c_str(),"wb");if(!file)std::abort();
        std::fwrite(raw.data(),1,raw.size(),file);std::fclose(file);
        hmm_core->log("WORLD %dx%d day=%u bytes=%zu",world.w(),world.h(),world.CountDay(),raw.size());
    }
    frame_marker(f);frame_count=f;
}
}
static int start(const zm_core_t*){
    hmm_session_reset();if(!hmm_io_init())return ZM_ERROR;
    return hmm_core->task_start(hmm_session_task,nullptr,16384,4,1);
}
static void stop(){hmm_session_stop();}
extern "C" __declspec(dllexport) const JointApp *joint_app_init(const zm_core_t *core,const char *dir,void (*marker)(unsigned)){
    hmm_core=core;output=dir;frame_marker=marker;scripted=std::getenv("HMM2_TEST_MAP")!=nullptr;
    if(scripted){SDL_SetMainReady();SDL_Init(SDL_INIT_EVENTS);}
    static const zm_app_t app={sizeof(zm_app_t),1,4,"hmm2","test",0x31,0x77,start,stop,hmm_command,hmm_video_poll};
    static const JointApp api={&app,[]{return frame_count.load();},[]{return world_ready.load();},
        []{return hmm_done?(hmm_state==255?1:0):-999;},[]{return int(hmm_state);},[]{return int(hmm_done);}};
    return &api;
}

// Codex: захват кадров без полного render используется отдельным стендом ПК.
void hmm_host_capture_cached_cursor(){std::abort();}
