/* Codex: запоздалые ответы, молчание Z80, смена ядра и ошибки закрытия на настоящем слое ELF. */
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <cstdlib>
extern "C" {
#include "hmm_core.h"
#include "hmm_platform.h"
void hmm_video_check_core(void);
const zm_core_t *hmm_core;
volatile int hmm_running,hmm_done,hmm_state;
uint32_t hmm_package_size=100000,hmm_frames;
char hmm_error[96];
}
static std::jmp_buf escape;
static unsigned tick,personality=1,ft_cfg,last_ok,last_cmd,request,closes,backs,fences;
static bool cancel_io,read_error,bad_id,back_error,opening_error;
static void require(bool value,const char*message){if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
extern "C" {
void hmm_fail(const char*){hmm_io_cancel();std::longjmp(escape,1);}
int hmm_game_main(int,char**){return 0;}
void hmm_runtime_close(){require(hmm_state!=3,"done before cleanup");}
void hmm_loading_tick(){}
void hmm_gpu_init(){}
void hmm_gpu_frame(const uint8_t*,const uint8_t*,int,int){}
void hmm_gpu_stats(uint32_t*a,uint32_t*b,uint32_t*c){*a=*b=*c=0;}
uint32_t hmm_gpu_pending_ms(){return 0;}
}
static void logline(const char*,...){}
int main(){
    zm_core_t core{};hmm_core=&core;core.size=sizeof(core);core.log=logline;
    core.mutex_create=[]()->void*{return (void*)1;};core.signal_create=core.mutex_create;
    core.mutex_lock=[](void*){};core.mutex_unlock=[](void*){};core.signal_give=[](void*){};
    core.millis=[]()->uint32_t{return tick;};core.delay=[](uint32_t n){tick+=n;};
    core.event=[](uint8_t,const void*p,uint16_t,uint32_t)->int{request=*(const uint16_t*)p;return ZM_OK;};
    core.reply=[](uint8_t c,const void*p,uint16_t)->int{last_cmd=c;last_ok=((const uint8_t*)p)[c==0xf6?2:0];return ZM_OK;};
    core.signal_wait=[](void*,uint32_t)->int{if(cancel_io)hmm_session_request_stop();else tick+=10001;return ZM_TIMEOUT;};
    core.ft_open=[](uint32_t)->int{personality=2;return opening_error?ZM_ERROR:ZM_OK;};
    core.ft_close=[]{++closes;};
    core.ft_read=[](uint32_t a,uint32_t*p)->int{require(personality==2,"FT read in slot0");if(!a)++fences;*p=0;return ZM_OK;};
    core.link_read=[](uint32_t a,uint32_t*p,uint32_t n)->int{
        if(read_error)return ZM_ERROR;
        for(unsigned i=0;i<n;i++,a+=4)p[i]=a==0?(bad_id?0:0x56444334u):a==4?(personality<<24)|0x0120u:a==0x5c&&personality==2?(ft_cfg&1u)<<27:0;
        return ZM_OK;
    };
    core.link_write=[](uint32_t a,const uint32_t*p,uint32_t)->int{if(a==0x5c)ft_cfg=*p;return ZM_OK;};
    core.core_back=[](uint32_t timeout)->int{require(timeout==10000,"core_back deadline");++backs;if(back_error)return ZM_TIMEOUT;personality=1;return ZM_OK;};
    unsigned cases=0;
    for(int stopped=0;stopped<2;++stopped)for(int op=0;op<5;++op){
        cancel_io=stopped!=0;tick=0;hmm_session_reset();require(hmm_io_init()!=0,"io init");
        uint8_t retired[2048]={};uint32_t size=0;
        if(!setjmp(escape)){
            if(op<2)hmm_remote_read(0,retired,op?2048:4);
            else if(op==2)hmm_remote_graphics(0,0,512);
            else hmm_fs_call(op==3?1:3,"TEST.SAV",nullptr,0,retired,4,&size);
            require(false,"request must fail");
        }
        std::memset(retired,0xcc,sizeof(retired));uint8_t late[16]={uint8_t(request),uint8_t(request>>8)};
        if(op==0){late[2]=4;std::memset(late+4,0x11,4);hmm_command(0x71,late,8);}
        if(op==1){late[4]=4;std::memset(late+6,0x11,4);hmm_command(0x31,late,10);}
        if(op==2)hmm_command(0x77,late,3);
        if(op==3){late[3]=4;hmm_command(0x74,late,13);}
        if(op==4)hmm_command(0x76,late,4);
        require(last_ok==0,"late reply accepted");
        for(auto c:retired)require(c==0xcc,"retired buffer overwritten");
        ++cases;
    }
    cancel_io=false;hmm_session_reset();require(hmm_video_open()==1,"video open");
    tick+=60001;hmm_video_poll(tick);require(hmm_running==1,"silence triggered stop");
    read_error=true;tick+=100;hmm_video_poll(tick);require(hmm_running==1,"LINK error triggered stop");read_error=false;
    bad_id=true;personality=1;tick+=100;hmm_video_poll(tick);require(hmm_running==1,"bad ID triggered stop");bad_id=false;
    tick+=100;hmm_video_poll(tick);require(hmm_running==0,"slot0 did not trigger stop");
    hmm_session_task(nullptr);require(hmm_done&&hmm_state==3&&closes==1&&backs==0&&fences==0,"slot0 cleanup");
    hmm_session_reset();require(hmm_video_open()==1,"video reopen before race");
    personality=1;hmm_video_check_core();require(!hmm_running,"slot0 check before poll interval");
    hmm_session_task(nullptr);require(hmm_done&&hmm_state==3,"slot0 before poll cleanup");
    for(int fail=0;fail<2;++fail){
        back_error=fail!=0;hmm_session_reset();require(hmm_video_open()==1,"video reopen");hmm_state=1;
        hmm_session_task(nullptr);require(hmm_done&&hmm_state==(fail?255:3),"completion state");
    }
    back_error=false;opening_error=true;hmm_session_reset();require(!hmm_video_open(),"partial ft_open");
    require(!hmm_video_open(),"partial open reported as ready");
    hmm_session_request_stop();hmm_session_task(nullptr);require(closes==5&&backs==3&&fences==3,"partial open cleanup");
    std::printf("PASS late replies=%u; silence, LINK error, bad ID ignored; SLOT0, Q, failed back, partial open\n",cases);
}

extern "C" void hmm_audio_shutdown(){}

extern "C" void hmm_clock_reset(){}
extern "C" int hmm_clock_sync(const uint8_t*,size_t){return 0;}
