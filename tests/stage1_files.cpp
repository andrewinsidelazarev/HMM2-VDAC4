/* Codex: файловый слой ELF работает без изменений; только удалённый носитель заменён файлом стенда. */
extern "C" {
#include "hmm_core.h"
uint32_t hmm_package_size;
volatile int hmm_running=1;
int hmm_read_hold,hmm_window_owner;
// Codex: счётчики ELF доступны и родному стенду; PSRAM учитывает только QEMU.
uint32_t hmm_decode_bytes,hmm_sfx_bytes,hmm_loop_bytes;
size_t hmm_heap_big_bytes,hmm_heap_big_peak,hmm_heap_big_count,hmm_heap_small_bytes;
size_t hmm_heap_mid_bytes,hmm_heap_mid_peak;
unsigned hmm_heap_small_pages(void){return 0;}
const zm_core_t *hmm_core;
}
#include <cstdio>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <cstdarg>
#include <mutex>
#include <thread>
static std::mutex ft_trace_mutex;
static const std::thread::id ft_main_thread=std::this_thread::get_id();
static unsigned prefetch_worker_writes,prefetch_worker_bytes,prefetch_worker_registers,prefetch_probes;
static bool prefetch_failure_injected;
static void (*prefetch_test_hook)();
void hmm_host_register_prefetch_test(void (*hook)()){prefetch_test_hook=hook;}
void hmm_host_prefetch_probe(){if(prefetch_test_hook){++prefetch_probes;prefetch_test_hook();}}
void hmm_host_prefetch_stats(){std::printf("HOST PREFETCH writes=%u bytes=%u registers=%u failure=%u probes=%u\n",prefetch_worker_writes,prefetch_worker_bytes,prefetch_worker_registers,unsigned(prefetch_failure_injected),prefetch_probes);}
static uint32_t command_position;static bool fail_next_read;
static bool torn_command,torn_fired;static unsigned torn_words;
void hmm_host_arm_torn_command(){torn_command=true;torn_fired=false;torn_words=0;}
bool hmm_host_torn_command_fired(){return torn_fired;}
void hmm_host_fail_next_read(){fail_next_read=true;}
static FILE *pack,*trace;static std::string folder;static unsigned requests,bytes;
static std::vector<uint8_t> catalogue_override;
// Codex: счётчики реальных чтений для сравнения общего кэша SFX с прежним.
unsigned hmm_host_read_requests(){return requests;}
unsigned hmm_host_read_bytes(){return bytes;}
void hmm_host_trace_frame(unsigned frame){std::lock_guard<std::mutex> guard(ft_trace_mutex);uint32_t h[]={0xffffffffu,1,frame};std::fwrite(h,4,3,trace);std::fflush(trace);if(std::getenv("HMM2_IO_TRACE"))std::printf("HOST IO frame=%u requests=%u bytes=%u\n",frame,requests,bytes);}
static int ft_write(uint32_t at,const uint32_t*p,uint32_t n){
    const bool worker=std::this_thread::get_id()!=ft_main_thread;
    if(worker)if(const char *ms=std::getenv("HMM2_PREFETCH_SLOW_MS"))std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(ms)));
    std::lock_guard<std::mutex> guard(ft_trace_mutex);
    if(worker){
        ++prefetch_worker_writes;
        if(at>=0x300000){++prefetch_worker_registers;return ZM_ERROR;}
        if(std::getenv("HMM2_PREFETCH_FAIL")&&!prefetch_failure_injected){
            prefetch_failure_injected=true;const uint32_t accepted=n/2;uint32_t h[]={at,accepted};
            std::fwrite(h,4,2,trace);std::fwrite(p,4,accepted,trace);prefetch_worker_bytes+=accepted*4;
            std::puts("HOST PREFETCH injected partial RAM_G write");return ZM_ERROR;
        }
        prefetch_worker_bytes+=n*4;
    }
    if(torn_command&&at>=0x308000&&at<0x309000){
        unsigned accepted=0;while(accepted<n&&torn_words<2){++accepted;++torn_words;}
        if(accepted<n){
            if(accepted){uint32_t h[]={at,accepted};std::fwrite(h,4,2,trace);std::fwrite(p,4,accepted,trace);}
            torn_command=false;torn_fired=true;std::puts("HOST injected partial cursor packet after two words");return ZM_ERROR;
        }
    }
    if(at==0x3020fc)command_position=*p;uint32_t h[]={at,n};if(std::fwrite(h,1,8,trace)!=8||std::fwrite(p,4,n,trace)!=n)throw std::runtime_error("host FT trace");return 0;
}
static void core_log(const char *format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");va_end(args);}
extern "C" void hmm_fail(const char*s){std::fprintf(stderr,"HOST FAIL: %s\n",s);std::fflush(stderr);throw std::runtime_error(s);}
extern "C" int hmm_exception_pending(void){return std::uncaught_exceptions()!=0;}
extern "C" void hmm_window_restore_failed(void){hmm_running=0;std::puts("HOST WINDOW RESTORE FAILURE STOPPED SESSION");}
void hmm_host_files_init(const char*p){folder=p;const char *source=std::getenv("HMM2_HOST_DAT");pack=std::fopen(source?source:"E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.DAT","rb");if(!pack)throw std::runtime_error("host pack");std::fseek(pack,0,SEEK_END);hmm_package_size=std::ftell(pack);
    trace=std::fopen((std::filesystem::path(folder).parent_path()/"frames.ft").string().c_str(),"wb");if(!trace)throw std::runtime_error("host trace");static zm_core_t core{};core.ft_write=ft_write;core.ft_read=[](uint32_t a,uint32_t*p)->int{*p=(a==0x3020f8||a==0x3020fc)?command_position:0;return 0;};
    core.millis=[]()->uint32_t{return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    core.delay=[](uint32_t ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));};core.log=core_log;hmm_core=&core;
    if(const char *test=std::getenv("HMM2_TEST_UI")){FILE*f=std::fopen((std::string(test)+"/catalogue.bin").c_str(),"rb");
        if(f){std::fseek(f,0,SEEK_END);catalogue_override.resize(std::ftell(f));std::rewind(f);std::fread(catalogue_override.data(),1,catalogue_override.size(),f);std::fclose(f);}}
}
extern "C" size_t hmm_remote_read(uint32_t offset,void *data,size_t size){
    if(fail_next_read){fail_next_read=false;throw std::runtime_error("injected OGG SD cancellation");}
    if(size>32768||offset>hmm_package_size||size>hmm_package_size-offset)throw std::runtime_error("host read bounds");
    ++requests;bytes+=unsigned(size);std::fseek(pack,long(offset),SEEK_SET);const auto n=std::fread(data,1,size,pack);
    if(offset<catalogue_override.size())std::memcpy(data,catalogue_override.data()+offset,std::min(n,catalogue_override.size()-offset));
    return n;
}
extern "C" void hmm_remote_graphics(uint32_t offset,uint32_t address,uint32_t size){
    // Codex: родной стенд проверяет DL; такты Z80 и SPI измеряются совместным стендом.
    uint32_t buffer[1024];
    while(size){unsigned n=size>sizeof(buffer)?sizeof(buffer):size;
        if(hmm_remote_read(offset,buffer,n)!=n)throw std::runtime_error("host graphics read");
        ft_write(address|0x80000000u,buffer,n/4);offset+=n;address+=n;size-=n;
    }
}
extern "C" int hmm_fs_call(unsigned op,const char *name,const char *other,uint32_t offset,void *data,unsigned length,uint32_t *size){
    const auto path=std::filesystem::path(folder)/name;std::error_code e;
    if(op==5)return std::filesystem::remove(path,e)&&!e?0:1;
    if(op==4){std::filesystem::rename(path,std::filesystem::path(folder)/other,e);return e?1:0;}
    auto f=std::fopen(path.string().c_str(),op==2?"wb":op==3?"r+b":"rb");if(!f)return 1;
    std::fseek(f,0,SEEK_END);if(size)*size=uint32_t(std::ftell(f));std::fseek(f,long(offset),SEEK_SET);int result=0;
    if(op==1&&std::fread(data,1,length,f)!=length)result=1;
    if(op==3&&std::fwrite(data,1,length,f)!=length)result=1;
    std::fclose(f);return result;
}

extern "C" int hmm_audio_busy(){return 0;}
