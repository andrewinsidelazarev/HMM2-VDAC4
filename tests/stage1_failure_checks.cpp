#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
extern "C" {
#include "hmm_core.h"
#include "hmm_platform.h"
const zm_core_t *hmm_core;
volatile int hmm_running,hmm_done,hmm_state;
uint32_t hmm_package_size=100000,hmm_frames;
char hmm_error[96];
uint32_t hmm_slept_ms,hmm_gfx_bytes,hmm_decode_bytes,hmm_sfx_bytes,hmm_loop_bytes;
}
static unsigned tick,personality=1,cfg,cancelled,closed,back,shutdowns,runtime_closed;
static bool broken,audio_task,inject;
static void require(bool v,const char *why){if(!v){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
extern "C" {
void hmm_io_cancel(){++cancelled;}
int hmm_audio_self(){return audio_task;}
void hmm_audio_shutdown(){}
void hmm_clock_reset(){}
void hmm_runtime_close(){++runtime_closed;}
int hmm_game_main(int,char**){return 0;}
void hmm_gpu_init(){}
void hmm_gpu_shutdown(){++shutdowns;}
void hmm_gpu_frame(const uint8_t*,const uint8_t*,int,int){if(inject)hmm_fail("FT CMD fault");}
void hmm_gpu_stats(uint32_t *a,uint32_t *b,uint32_t *c){*a=*b=*c=0;}
uint32_t hmm_gpu_pending_ms(){return 0;}
void hmm_sd_stats(uint32_t *a,uint32_t *b,uint32_t *c){*a=*b=*c=0;}
void hmm_memory_line(unsigned){}
}
static void logline(const char *text,...){std::fprintf(stderr,"LOG %s\n",text);}
// A real noexcept window destructor requests a real video frame while the
// first transport exception unwinds. Old code terminates on the second fault.
struct RestoreWindow {~RestoreWindow(){broken=true;hmm_video_frame(nullptr,nullptr,640,480);}};
int main(){
    std::set_terminate([]{std::fprintf(stderr,"SECOND EXCEPTION DURING WINDOW UNWIND\n");std::exit(71);});
    zm_core_t core{};hmm_core=&core;core.size=sizeof(core);core.log=logline;
    core.millis=[]()->uint32_t{return tick;};core.delay=[](uint32_t n){tick+=n;};
    core.ft_open=[](uint32_t)->int{personality=2;return ZM_OK;};
    core.ft_close=[]{++closed;};
    core.ft_read=[](uint32_t,uint32_t *p)->int{*p=0;return broken?ZM_ERROR:ZM_OK;};
    core.link_read=[](uint32_t a,uint32_t *p,uint32_t n)->int{
        for(unsigned i=0;i<n;++i,a+=4)p[i]=a==0?0x56444334u:a==4?(personality<<24)|0x0120u:a==0x5c&&personality==2?(cfg&1u)<<27:0;
        return ZM_OK;
    };
    core.link_write=[](uint32_t a,const uint32_t *p,uint32_t)->int{if(a==0x5c)cfg=*p;return ZM_OK;};
    core.core_back=[](uint32_t)->int{++back;personality=1;broken=false;return ZM_OK;};
    std::fprintf(stderr,"OPEN\n");hmm_session_reset();hmm_state=1;require(hmm_video_open()==1,"video open");
    hmm_video_frame(nullptr,nullptr,640,480);require(hmm_frames==1,"normal frame");
    std::fprintf(stderr,"AUDIO\n");audio_task=true;
    try{hmm_fail("audio only");require(false,"audio throws");}catch(const std::runtime_error&){}
    require(hmm_running&&!hmm_error[0]&&!cancelled,"audio failure must remain isolated");audio_task=false;
    std::fprintf(stderr,"FATAL\n");try{RestoreWindow restore;inject=true;hmm_video_frame(nullptr,nullptr,640,480);require(false,"FT must throw");}
    catch(const std::runtime_error &e){require(!std::strcmp(e.what(),"FT CMD fault"),"original exception");}
    require(!hmm_running&&cancelled==1&&!std::strcmp(hmm_error,"FT CMD fault"),"fatal stop preserves first error");
    require(hmm_frames==1,"restoration frame was suppressed");
    hmm_session_task(nullptr);
    require(hmm_done&&hmm_state==255&&closed==1&&back==1&&shutdowns==1&&runtime_closed==1,"session cleanup completes");
    require(!std::strcmp(hmm_error,"FT CMD fault"),"cleanup preserves cause");
    std::puts("PASS normal frame, isolated audio error, fatal FT error, noexcept window unwind, task cleanup and SLOT0");
}
