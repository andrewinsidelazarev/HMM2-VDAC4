/* Codex: реальные io/input читают пакеты 75; подменены только часы и синхронизация стенда. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const zm_core_t*hmm_core;volatile int hmm_running=1,hmm_done,hmm_state;uint32_t hmm_frames,hmm_package_size;char hmm_error[96];
static uint32_t now=1000;static uint32_t millis(void){return now;}
// The input fixture has no GPU task, audio task or RTC synchronization.
void hmm_gpu_busy_enter(void){}void hmm_gpu_busy_leave(void){}
void hmm_loading_tick(void){}void hmm_status_input_seen(void){}
extern void hmm_loading_mouse_begin(int,int);
int hmm_audio_self(void){return 0;}
int hmm_clock_sync(const uint8_t *rtc,size_t n){(void)rtc;(void)n;return 0;}
static void *create(void){return (void*)1;}static void lock(void*p){(void)p;}
static unsigned accepted,rejected;
static int reply(uint8_t c,const void*p,uint16_t n){if(c==0xf1){assert(n==1);if(*(const uint8_t*)p)++accepted;else ++rejected;}return 0;}
static uint8_t sd_packet[8193],sd_expected[32768],sd_actual[32768];
static int sd_mode;static unsigned sd_base,sd_calls;
static void wr16(uint8_t*p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void sd_send(unsigned seq,unsigned at,unsigned n){wr16(sd_packet,seq);wr16(sd_packet+2,at);wr16(sd_packet+4,n);memcpy(sd_packet+6,sd_expected+sd_base+at,n);assert(hmm_command(0x31,sd_packet,(uint16_t)(n+6))==ZM_OK);}
static int sd_event(uint8_t command,const void*data,uint16_t n,uint32_t timeout){
    (void)timeout;assert(command==0xe4&&n==8);const uint8_t*p=data;unsigned seq=p[0]|p[1]<<8;
    if(sd_mode==1){wr16(sd_packet,seq);wr16(sd_packet+2,32);memcpy(sd_packet+4,sd_expected,32);hmm_command(0x71,sd_packet,36);return ZM_OK;}
    const unsigned offset=p[2]|p[3]<<8|p[4]<<16|p[5]<<24;
    const unsigned size=p[6]|p[7]<<8;assert(size==8192);sd_base=offset-85950464;++sd_calls;accepted=rejected=0;
    // Codex: чужая последовательность, пропуск и превышение максимума не меняют буфер.
    sd_send(seq+1,0,32);sd_send(seq,10,32);hmm_command(0x31,sd_packet,8193);assert(rejected==3);
    sd_send(seq,0,7680);sd_send(seq,0,7680);assert(!accepted&&rejected==3);
    sd_packet[6]^=1;hmm_command(0x31,sd_packet,7686);assert(rejected==4);
    // Codex: хвост за границей запроса отвергается; точный хвост завершает поток один раз.
    wr16(sd_packet,seq);wr16(sd_packet+2,7680);wr16(sd_packet+4,size-7680+1);hmm_command(0x31,sd_packet,(uint16_t)(size-7680+7));assert(rejected==5&&!accepted);
    sd_send(seq,7680,size-7680);assert(accepted==1);return ZM_OK;
}
static int signal_wait(void*p,uint32_t ms){(void)p;now+=ms;return ZM_OK;}
void hmm_fail(const char*s){puts(s);abort();}void hmm_delay(uint32_t ms){now+=ms;}
static void packet(const uint8_t*p,unsigned n){assert(hmm_command(0x75,p,n)==ZM_OK);}
static SDL_Event next(void){static unsigned call;SDL_Event e;int got=hmm_poll_event(&e);++call;if(!got)fprintf(stderr,"Missing expected input event %u\n",call);assert(got);return e;}
static void empty(void){SDL_Event e;assert(!hmm_poll_event(&e));}
int main(void){
    _set_error_mode(_OUT_TO_STDERR);
    setvbuf(stdout,NULL,_IONBF,0);
    zm_core_t c={0};c.millis=millis;c.mutex_create=c.signal_create=create;c.mutex_lock=c.mutex_unlock=lock;c.reply=reply;hmm_core=&c;assert(hmm_io_init());
    const uint8_t a[]={0,255,255,255,0x12,0x1c,0xf0,0x1c,0xf0,0x12};packet(a,sizeof(a));
    SDL_Event e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_LSHIFT);
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym=='a'&&(e.key.keysym.mod&KMOD_LSHIFT));
    e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym=='a');e=next();assert(e.type==SDL_KEYUP&&!(e.key.keysym.mod&KMOD_LSHIFT));empty();
    const uint8_t b[]={17,255,255,255,0xe0,0x75,0xe0,0xf0,0x75};packet(b,sizeof(b));packet(b,4);
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_UP);e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_UP);
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_RIGHT);e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_RETURN);empty();
    now+=501;e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_RIGHT);e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_RETURN);empty();
    uint8_t m[]={0,255,250,10};packet(m,sizeof(m));empty();m[1]=254;m[2]=5;m[3]=7;packet(m,sizeof(m));
    e=next();assert(e.type==SDL_MOUSEMOTION&&e.motion.x==342&&e.motion.y==246&&e.motion.xrel==22&&e.motion.yrel==6);
    empty();packet(m,sizeof(m));
    e=next();assert(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT&&e.button.x==342);empty();
    m[1]=255;packet(m,sizeof(m));empty();packet(m,sizeof(m));e=next();assert(e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_LEFT);empty();
    const uint8_t repeat[]={0,255,5,7,0x3a,0x3a,0xf0,0x3a,0x3a,0xf0,0x3a};packet(repeat,sizeof(repeat));
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym=='m'&&!e.key.repeat);
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym=='m'&&e.key.repeat);
    e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym=='m');
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym=='m'&&!e.key.repeat);
    e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym=='m');empty();
    uint8_t held_m[]={0,255,5,7,0x3a};packet(held_m,sizeof(held_m));
    e=next();assert(e.type==SDL_KEYDOWN&&!e.key.repeat);empty();
    now+=501;e=next();assert(e.type==SDL_KEYUP);empty();
    packet(held_m,sizeof(held_m));e=next();assert(e.type==SDL_KEYDOWN&&e.key.repeat);empty();
    const uint8_t break_m[]={0,255,5,7,0xf0,0x3a};packet(break_m,sizeof(break_m));e=next();assert(e.type==SDL_KEYUP);empty();
    packet(held_m,sizeof(held_m));e=next();assert(e.type==SDL_KEYDOWN&&!e.key.repeat);empty();
    packet(break_m,sizeof(break_m));e=next();assert(e.type==SDL_KEYUP);empty();
    m[1]=254;packet(m,sizeof(m));empty();packet(m,sizeof(m));e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);empty();
    now+=501;e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
    m[1]=255;packet(m,sizeof(m));empty();packet(m,sizeof(m));empty();
    /* Silence releases SDL state; resuming the SAME held button is not a
     * physical new press and must not cancel a running hero's route. */
    if(!getenv("HMM2_INPUT_SKIP_HELD_MOUSE")){
        puts("CASE held mouse across input gap");
        m[1]=254;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);empty();
        now+=501;e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
        packet(m,sizeof(m));empty();packet(m,sizeof(m));empty();
        m[1]=255;packet(m,sizeof(m));packet(m,sizeof(m));empty();
        m[1]=254;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);empty();
        m[1]=255;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
    }
    {
        puts("CASE held Kempston across input gap");
        uint8_t j[]={16,255,5,7};packet(j,sizeof(j));packet(j,sizeof(j));
        e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_RETURN);empty();
        now+=501;e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_RETURN);empty();
        packet(j,sizeof(j));empty();packet(j,sizeof(j));empty();
        j[0]=0;packet(j,sizeof(j));packet(j,sizeof(j));empty();
        j[0]=16;packet(j,sizeof(j));packet(j,sizeof(j));
        e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_RETURN&&!e.key.repeat);empty();
        j[0]=0;packet(j,sizeof(j));packet(j,sizeof(j));
        e=next();assert(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_RETURN);empty();
    }
    puts("INPUT OK held mouse/Kempston across silence: no new press until confirmed release");
    {
        puts("CASE held mouse across a single all-FF packet");
        m[1]=254;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);empty();
        const uint8_t missing[]={0,255,255,255};packet(missing,sizeof(missing));
        if(getenv("HMM2_INPUT_ALLOW_RESET_RELEASE")){e=next();assert(e.type==SDL_MOUSEBUTTONUP);}else empty();
        puts("CASE resume held after a single all-FF packet");
        packet(m,sizeof(m));empty();packet(m,sizeof(m));empty();
        m[1]=255;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
        m[1]=254;packet(m,sizeof(m));packet(m,sizeof(m));
        e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);empty();
        // Two equal releases still work even at the valid counter value FF/FF.
        packet(missing,sizeof(missing));empty();packet(missing,sizeof(missing));
        e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
        m[1]=255;packet(m,sizeof(m));packet(m,sizeof(m));empty();
        puts("INPUT OK all-FF sample obeys button debounce; real release/repress preserved");
    }
    /* A complete click occurs while rendering; cursor polling must not eat
     * either edge or move the recorded click to the later pointer position. */
    int cx,cy;const int start_x=342;uint32_t metrics[6];hmm_input_frame();hmm_input_stats(metrics,1);
    m[1]=254;m[2]=6;packet(m,sizeof(m));packet(m,sizeof(m));
    hmm_loading_mouse(&cx,&cy);assert(cx==start_x+2);
    m[1]=255;m[2]=9;packet(m,sizeof(m));packet(m,sizeof(m));
    hmm_loading_mouse(&cx,&cy);assert(cx==start_x+8);
    hmm_loading_mouse_begin(1,1);hmm_loading_mouse(&cx,&cy);assert(cx==start_x+8);
    now+=80;
    e=next();assert(e.type==SDL_MOUSEMOTION&&e.motion.x==start_x+2);
    e=next();assert(e.type==SDL_MOUSEBUTTONDOWN&&e.button.x==start_x+2);
    e=next();assert(e.type==SDL_MOUSEMOTION&&e.motion.x==start_x+8);
    e=next();assert(e.type==SDL_MOUSEBUTTONUP&&e.button.x==start_x+8);empty();
    now+=17;hmm_input_frame();hmm_input_stats(metrics,1);assert(metrics[0]==2&&metrics[2]==80&&metrics[3]==1&&metrics[5]==97);
    /* Mouse transitions fence keyboard bytes from later packets. */
    m[1]=254;packet(m,sizeof(m));packet(m,sizeof(m));
    uint8_t shift_later[]={0,255,9,7,0x12};packet(shift_later,sizeof(shift_later));packet(shift_later,4);
    e=next();assert(e.type==SDL_MOUSEBUTTONDOWN);
    e=next();assert(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_LSHIFT);
    e=next();assert(e.type==SDL_MOUSEBUTTONUP);empty();
    const uint8_t shift_up[]={0,255,9,7,0xf0,0x12};packet(shift_up,sizeof(shift_up));e=next();assert(e.type==SDL_KEYUP);empty();
    /* More than a SDL queue of clicks is drained in bounded portions. */
    for(unsigned i=0;i<100;++i){m[1]=(uint8_t)(i&1?255:254);packet(m,sizeof(m));packet(m,sizeof(m));}
    for(unsigned i=0;i<100;++i){e=next();assert(e.type==(i&1?SDL_MOUSEBUTTONUP:SDL_MOUSEBUTTONDOWN));}empty();
    puts("INPUT OK delayed complete click, exact click positions, keyboard/mouse order, burst edges");
    puts("INPUT OK timeout retains key repeat; fresh break/repress; no stale mouse/Kempston press");
    puts("INPUT OK movement make/repeat/break/new make");
    puts("INPUT OK PS2 shift/extended/release; Kempston; mouse wrap/buttons; stale release");
#ifdef HMM2_INPUT_ONLY
    return 0;
#endif
    c.signal_give=lock;c.signal_wait=signal_wait;c.event=sd_event;hmm_package_size=547492852;
    for(unsigned i=0;i<sizeof(sd_expected);++i)sd_expected[i]=(uint8_t)(i*37);
    assert(hmm_remote_read(85950464,sd_actual,sizeof(sd_actual))==sizeof(sd_actual));assert(sd_calls==4);assert(!memcmp(sd_actual,sd_expected,sizeof(sd_actual)));
    sd_mode=1;assert(hmm_remote_read(0,sd_actual,32)==32);assert(!memcmp(sd_actual,sd_expected,32));
    puts("SD STREAM OK 32 KiB via four8KiB requests, duplicate and sequence/offset/length guards, legacy32 bytes");return 0;
}
