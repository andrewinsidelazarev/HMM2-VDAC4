/* Codex: измеряем существующий путь E4/71 без записи на SD и без блокировки команд ядра.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "zm_app.h"
#include <stdint.h>
#include <stddef.h>

static const zm_core_t *core;
static void *lock;
static uint32_t file_size,offset,started,request_at,phase_bytes,requests,hash,seed;
static uint32_t latency[4096],longest;
static uint16_t sequence,wanted;
static uint8_t state,pending,phase;
static uint16_t rd16(const uint8_t*p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t*p){return rd16(p)|((uint32_t)rd16(p+2)<<16);}
static void wr16(uint8_t*p,uint16_t x){p[0]=x;p[1]=x>>8;}
static void wr32(uint8_t*p,uint32_t x){wr16(p,x);wr16(p+2,x>>16);}
static void fail(const char *s){state=255;pending=0;core->log("HSD: ошибка: %s",s);}
static uint32_t quantile(uint32_t n){uint32_t sum=0;for(uint32_t i=0;i<4096;i++){sum+=latency[i];if(sum>=n)return i;}return 4095;}
static void reset_phase(uint32_t now){
    started=now;phase_bytes=requests=longest=0;hash=2166136261u;
    for(unsigned i=0;i<4096;i++)latency[i]=0;
}
static void finish_phase(uint32_t now){
    uint32_t ms=now-started;
    /* Codex: целочисленные десятые КиБ/с; длительность и байты сохраняются для точного пересчёта. */
    uint32_t speed=ms?(uint32_t)((uint64_t)phase_bytes*10000/((uint64_t)ms*1024)):0;
    core->log("HSD: %s; байт=%u мс=%u КиБ/с=%u.%u запросов=%u p50=%u p95=%u p99=%u max=%u мс FNV1a=%08x",
      phase?"случайно":"последовательно",(unsigned)phase_bytes,(unsigned)ms,(unsigned)(speed/10),(unsigned)(speed%10),
      (unsigned)requests,(unsigned)quantile((requests+1)/2),(unsigned)quantile((requests*95+99)/100),
      (unsigned)quantile((requests*99+99)/100),(unsigned)longest,(unsigned)hash);
    if(!phase){phase=1;seed=0x484d4d32u;reset_phase(now);}else{state=3;core->log("HSD: тест завершён; SD не изменялась");}
}
static int command(uint8_t cmd,const uint8_t *p,uint16_t n){
    if(cmd<0x70 || cmd>0x75)return ZM_ERROR;
    core->mutex_lock(lock);
    if(cmd==0x70){
        uint8_t ok=0;
        if(state==0 && n>=5 && rd32(p)>=1016){file_size=rd32(p);offset=0;sequence=0;phase=0;state=1;reset_phase(core->millis());ok=1;core->log("HSD: файл %u байт; порция 1016; случайных запросов 2048",(unsigned)file_size);}
        core->reply(0xF0,&ok,1);
    }else if(cmd==0x71){
        uint8_t ok=0;
        if(pending && n>=4 && rd16(p)==sequence){
            uint32_t took=core->millis()-request_at;
            if(rd16(p+2)!=wanted || n!=wanted+4)fail("неполный ответ SD");
            else{
                for(unsigned i=4;i<n;i++){hash^=p[i];hash*=16777619u;}
                latency[took<4096?took:4095]++;if(took>longest)longest=took;
                requests++;phase_bytes+=wanted;offset+=wanted;pending=0;ok=1;
            }
        }
        core->reply(0xF1,&ok,1);
    }else if(cmd==0x73){uint8_t pstatus[6]={state,1,0,0,0,0};wr32(pstatus+2,requests);core->reply(0xF3,pstatus,6);}
    else if(cmd==0x75){uint8_t ok=1;core->reply(0xF5,&ok,1);}
    core->mutex_unlock(lock);return ZM_OK;
}
static void poll(uint32_t now){
    core->mutex_lock(lock);
    if(state==1 && pending && (uint32_t)(now-request_at)>30000)fail("ответ не пришёл за 30 секунд");
    if(state==1 && !pending){
        if((!phase && offset==file_size)||(phase && requests==2048))finish_phase(now);
        if(state==1){
            uint32_t at=offset,next_seed=seed;wanted=1016;
            if(phase){
                next_seed^=next_seed<<13;next_seed^=next_seed>>17;next_seed^=next_seed<<5;at=next_seed%(file_size-1016+1);
                if(requests<3 && file_size>33555448)at=33554431+requests;
                if(requests==3)at=file_size-1016;
            }else if(file_size-at<wanted)wanted=(uint16_t)(file_size-at);
            uint8_t request[8];wr16(request,++sequence);wr32(request+2,at);wr16(request+6,wanted);
            request_at=now;pending=1;
            if(core->event(0xE4,request,8,0)!=ZM_OK){pending=0;sequence--;}
            else seed=next_seed;
        }
    }
    core->mutex_unlock(lock);
}
static int start(const zm_core_t*c){
    core=c;state=pending=0;
    if(c->abi_major!=1 || c->size<offsetof(zm_core_t,event)+sizeof(c->event))return ZM_ERROR;
    lock=c->mutex_create();if(!lock)return ZM_NO_MEMORY;
    core->log("HSD: модуль стартовал; ожидание HEROES2.AGG");return ZM_OK;
}
static void stop(void){if(lock){core->mutex_lock(lock);state=0;pending=0;core->mutex_unlock(lock);}}
int main(int argc,char **argv){
    static const zm_app_t app={sizeof(zm_app_t),1,0,"hsd","0.1",0x70,0x75,start,stop,command,poll};
    if(argc!=3)return 0;*(const zm_app_t**)argv[2]=&app;return 1;
}
