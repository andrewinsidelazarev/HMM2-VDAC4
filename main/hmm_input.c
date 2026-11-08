/* Codex: PS/2 set 2, мышь и Kempston из пакетов загрузчика DOOM → события оригинала.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
#include "hmm_core.h"
#include "hmm_loading.h"
#include <SDL.h>
#include <string.h>
#include <stdio.h>
static SDL_Event queue[256];static uint32_t queue_when[256];static unsigned head,tail;
/* edges,total queue delay,max queue delay,presented samples,total present delay,max present delay */
static uint32_t input_stats[6],input_pending;
static int extended,released,joy_old,mouse_old,mx=320,my=240,emx=320,emy=240,mods,stale_released;
static SDL_Keycode held[2][132];
static void push_at(const SDL_Event*e,uint32_t when){
    // Codex: движение при загрузке не занимает всю очередь и не вытесняет клавиши.
    if(e->type==SDL_MOUSEMOTION&&head!=tail&&queue[(head-1)&255].type==SDL_MOUSEMOTION){queue[(head-1)&255]=*e;queue_when[(head-1)&255]=when;return;}
    if(head-tail<256){queue[head&255]=*e;queue_when[head++&255]=when;}
}
static void push(const SDL_Event *e){push_at(e,0);}
void hmm_input_stats(uint32_t *out,int reset){memcpy(out,input_stats,sizeof(input_stats));if(reset)memset(input_stats,0,sizeof(input_stats));}
void hmm_input_frame(void){if(input_pending){const uint32_t ms=hmm_core->millis()-input_pending;input_pending=0;
    ++input_stats[3];input_stats[4]+=ms;if(ms>input_stats[5])input_stats[5]=ms;}}
// Клод: мышь читают две задачи: игра (hmm_poll_event) и курсор (cursor_task в hmm_gpu.cpp), который живёт, пока игра занята.
// Положение и очередь событий общие, поэтому всё это под одним мьютексом.
static void *input_lock;static volatile uint32_t input_polled;
static void input_enter(void){if(input_lock)hmm_core->mutex_lock(input_lock);}
static void input_leave(void){if(input_lock)hmm_core->mutex_unlock(input_lock);}
void hmm_input_init(void){if(!input_lock)input_lock=hmm_core->mutex_create();input_polled=hmm_core->millis();}
uint32_t hmm_input_polled(void){return input_polled;}
static void mouse_live(void){
    hmm_mouse_position(&mx,&my);
}
static unsigned mouse_update(void){
    unsigned count=0;
    int dx,dy,buttons;uint32_t when;
    while(head-tail<=252&&hmm_mouse_next(&dx,&dy,&buttons,&emx,&emy,&when)){
        ++count;
        if(dx||dy){SDL_Event e={0};e.type=SDL_MOUSEMOTION;e.motion.x=emx;e.motion.y=emy;e.motion.xrel=dx*2;e.motion.yrel=-dy*2;push(&e);}
        const uint8_t map[3]={SDL_BUTTON_LEFT,SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE};
        for(unsigned i=0;i<3;i++)if((buttons^mouse_old)&(1<<i)){SDL_Event e={0};int down=buttons&(1<<i);e.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;e.button.state=down?SDL_PRESSED:SDL_RELEASED;e.button.button=map[i];e.button.x=emx;e.button.y=emy;push_at(&e,when);}mouse_old=buttons;
    }
    return count;
}
void hmm_loading_mouse_begin(int x,int y){(void)x;(void)y;input_enter();mouse_live();input_leave();}
void hmm_loading_mouse(int *x,int *y){input_enter();mouse_live();*x=mx;*y=my;input_leave();}
static SDL_Keycode normal(unsigned code){
    static const SDL_Keycode keys[132]={
        [0x01]=SDLK_F9,[0x03]=SDLK_F5,[0x04]=SDLK_F3,[0x05]=SDLK_F1,[0x06]=SDLK_F2,[0x07]=SDLK_F12,
        [0x09]=SDLK_F10,[0x0a]=SDLK_F8,[0x0b]=SDLK_F6,[0x0c]=SDLK_F4,[0x0d]=SDLK_TAB,[0x0e]='`',
        [0x11]=SDLK_LALT,[0x12]=SDLK_LSHIFT,[0x14]=SDLK_LCTRL,[0x15]='q',[0x16]='1',
        [0x1a]='z',[0x1b]='s',[0x1c]='a',[0x1d]='w',[0x1e]='2',
        [0x21]='c',[0x22]='x',[0x23]='d',[0x24]='e',[0x25]='4',[0x26]='3',
        [0x29]=' ',[0x2a]='v',[0x2b]='f',[0x2c]='t',[0x2d]='r',[0x2e]='5',
        [0x31]='n',[0x32]='b',[0x33]='h',[0x34]='g',[0x35]='y',[0x36]='6',
        [0x3a]='m',[0x3b]='j',[0x3c]='u',[0x3d]='7',[0x3e]='8',
        [0x41]=',',[0x42]='k',[0x43]='i',[0x44]='o',[0x45]='0',[0x46]='9',
        [0x49]='.',[0x4a]='/',[0x4b]='l',[0x4c]=';',[0x4d]='p',[0x4e]='-',
        [0x52]='\'',[0x54]='[',[0x55]='=',[0x58]=SDLK_CAPSLOCK,[0x59]=SDLK_RSHIFT,[0x5a]=SDLK_RETURN,[0x5b]=']',[0x5d]='\\',
        [0x66]=SDLK_BACKSPACE,[0x69]=SDLK_KP_1,[0x6b]=SDLK_KP_4,[0x6c]=SDLK_KP_7,[0x70]=SDLK_KP_0,[0x71]=SDLK_KP_PERIOD,
        [0x72]=SDLK_KP_2,[0x73]=SDLK_KP_5,[0x74]=SDLK_KP_6,[0x75]=SDLK_KP_8,[0x76]=SDLK_ESCAPE,[0x77]=SDLK_NUMLOCKCLEAR,
        [0x78]=SDLK_F11,[0x79]=SDLK_KP_PLUS,[0x7a]=SDLK_KP_3,[0x7b]=SDLK_KP_MINUS,[0x7c]=SDLK_KP_MULTIPLY,[0x7d]=SDLK_KP_9,[0x83]=SDLK_F7};
    return code<132?keys[code]:0;
}
static SDL_Keycode ext(unsigned c){switch(c){case 0x11:return SDLK_RALT;case 0x14:return SDLK_RCTRL;case 0x4a:return SDLK_KP_DIVIDE;case 0x5a:return SDLK_KP_ENTER;case 0x69:return SDLK_END;case 0x6b:return SDLK_LEFT;case 0x6c:return SDLK_HOME;case 0x70:return SDLK_INSERT;case 0x71:return SDLK_DELETE;case 0x72:return SDLK_DOWN;case 0x74:return SDLK_RIGHT;case 0x75:return SDLK_UP;case 0x7a:return SDLK_PAGEDOWN;case 0x7d:return SDLK_PAGEUP;default:return 0;}}
static void key(SDL_Keycode k,int down,int repeat){
    int bit=k==SDLK_LSHIFT?KMOD_LSHIFT:k==SDLK_RSHIFT?KMOD_RSHIFT:k==SDLK_LCTRL?KMOD_LCTRL:k==SDLK_RCTRL?KMOD_RCTRL:k==SDLK_LALT?KMOD_LALT:k==SDLK_RALT?KMOD_RALT:0;
    if(down)mods|=bit;else mods&=~bit;
    if(k==SDLK_CAPSLOCK&&down&&!repeat)mods^=KMOD_CAPS;
    SDL_Event e={0};e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.state=down?SDL_PRESSED:SDL_RELEASED;e.key.repeat=repeat;e.key.keysym.sym=k;e.key.keysym.mod=(Uint16)mods;push(&e);
}
// A timeout releases SDL state. Keep physical make codes until a real break,
// so the next autorepeat is not mistaken for a new movement-stop command.
static void release_all(int forget){for(unsigned e=0;e<2;e++)for(unsigned c=0;c<132;c++)if(held[e][c]){key(held[e][c],0,0);if(forget)held[e][c]=0;}extended=released=0;}
static void ps2(unsigned c){
    if(c==0xff){release_all(1);return;}if(c==0xe0){extended=1;return;}if(c==0xf0){released=1;return;}
    unsigned e=extended;int up=released;extended=released=0;if(c>=132)return;
    SDL_Keycode k=e?ext(c):normal(c);if(!k)return;
    if(up){if(held[e][c])key(held[e][c],0,0);held[e][c]=0;}
    else{key(k,1,held[e][c]!=0);held[e][c]=k;}
}
extern void hmm_status_input_seen(void);
int hmm_poll_event(void *out){
    input_enter();if(hmm_core)input_polled=hmm_core->millis();
    // Клод: игра ждёт ввода — «Map loading...» после загрузки мира больше не нужна (hmm_gpu.cpp).
    hmm_status_input_seen();
    if(head==tail){
        if(!hmm_running){SDL_Event e={0};e.type=SDL_QUIT;push(&e);}
        mouse_live();
        unsigned progress;
        do{
        uint8_t bytes[64];int joy,stale;size_t n=hmm_keys_take(bytes,sizeof(bytes),&joy,&stale);
        for(size_t i=0;i<n;i++)ps2(bytes[i]);if(stale&&!stale_released)release_all(0);stale_released=stale;
        static const SDL_Keycode keys[5]={SDLK_RIGHT,SDLK_LEFT,SDLK_DOWN,SDLK_UP,SDLK_RETURN};
        for(unsigned i=0;i<5;i++)if((joy^joy_old)&(1<<i))key(keys[i],(joy>>i)&1,0);joy_old=joy;
        progress=(unsigned)n+mouse_update();
        }while(progress&&head-tail<=184);
    }
    int got=0;if(head!=tail){const unsigned at=tail++&255;memcpy(out,&queue[at],sizeof(SDL_Event));got=1;
        if(queue_when[at]&&(queue[at].type==SDL_MOUSEBUTTONDOWN||queue[at].type==SDL_MOUSEBUTTONUP)){
            const uint32_t ms=hmm_core->millis()-queue_when[at];++input_stats[0];input_stats[1]+=ms;if(ms>input_stats[2])input_stats[2]=ms;
            if(!input_pending)input_pending=queue_when[at];
        }
    }
    input_leave();return got;
}
int hmm_modifiers(void){return mods;}
SDL_Keymod SDL_GetModState(void){return (SDL_Keymod)mods;}
void SDL_Delay(Uint32 ms){hmm_delay(ms);}
Uint8 SDL_EventState(Uint32 type,int state){(void)type;return state==SDL_IGNORE?SDL_IGNORE:SDL_ENABLE;}
SDL_bool SDL_SetHint(const char *name,const char *value){(void)name;(void)value;return SDL_TRUE;}
int SDL_ShowCursor(int show){(void)show;return SDL_DISABLE;}
int SDL_GetNumTouchDevices(void){return 0;}
SDL_GameController *SDL_GameControllerOpen(int index){(void)index;return 0;}
void SDL_GameControllerClose(SDL_GameController *c){(void)c;}
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID id){(void)id;return 0;}
const char *SDL_GetError(void){return "Input device is unavailable";}
const char *SDL_GetKeyName(SDL_Keycode k){
    static char name[20];if(k>=32&&k<127){name[0]=(char)k;name[1]=0;return name;}
    if(k>=SDLK_F1&&k<=SDLK_F12){snprintf(name,sizeof(name),"F%d",(int)(k-SDLK_F1+1));return name;}
    switch(k){case SDLK_RETURN:return "Return";case SDLK_ESCAPE:return "Escape";case SDLK_TAB:return "Tab";case SDLK_BACKSPACE:return "Backspace";case SDLK_UP:return "Up";case SDLK_DOWN:return "Down";case SDLK_LEFT:return "Left";case SDLK_RIGHT:return "Right";case SDLK_DELETE:return "Delete";case SDLK_HOME:return "Home";case SDLK_END:return "End";case SDLK_PAGEUP:return "PageUp";case SDLK_PAGEDOWN:return "PageDown";case SDLK_LSHIFT:return "Left Shift";case SDLK_RSHIFT:return "Right Shift";case SDLK_LCTRL:return "Left Ctrl";case SDLK_RCTRL:return "Right Ctrl";case SDLK_LALT:return "Left Alt";case SDLK_RALT:return "Right Alt";default:snprintf(name,sizeof(name),"Key %ld",(long)k);return name;}
}
