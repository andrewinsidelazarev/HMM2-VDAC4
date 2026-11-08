"""Codex: загрузка C++ и unwinder из уже исполненного стенда HRT, запуск игры — отдельной задачей ABI."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    s=(ROOT/'bench/runtime_bootstrap.c').read_text(encoding='utf-8')
    s=s[s.index('#include "zm_app.h"'):s.index('extern int app_entry')]
    s=s.replace('static const zm_core_t *core;','const zm_core_t *hmm_core;\n#define core hmm_core\n#include "hmm_core.h"\n#include "hmm_platform.h"\n#include <string.h>')
    s=s.replace('void abort(void){core->log("HRT: аварийное завершение runtime");for(;;)core->delay(1000);}','void abort(void){hmm_state=255;strcpy(hmm_error,"C++ runtime abort");core->log("HMM2: аварийное завершение runtime");for(;;)core->delay(1000);}')
    s=s.replace('HRT:','HMM2:')
    s+='''
/* Codex: игровые C++ объекты доступны только задаче игры; ввод и ответы SD — на ядре 0. */
volatile int hmm_running,hmm_done,hmm_state;
uint32_t hmm_package_size,hmm_frames;
char hmm_error[96];
static function exit_functions[128];static unsigned exit_count;
int atexit(function f){if(exit_count==128)return -1;exit_functions[exit_count++]=f;return 0;}
/* Codex: ленивые статические объекты имеют штатный guard, ожидание не занимает CPU. */
int __cxa_guard_acquire(uint64_t *g){
    for(;;){core->mutex_lock(registry_lock);unsigned char*p=(unsigned char*)g;
        if(p[0]){core->mutex_unlock(registry_lock);return 0;}
        if(!p[1]){p[1]=1;core->mutex_unlock(registry_lock);return 1;}
        core->mutex_unlock(registry_lock);core->delay(1);
    }
}
void __cxa_guard_release(uint64_t*g){core->mutex_lock(registry_lock);((unsigned char*)g)[0]=1;((unsigned char*)g)[1]=0;core->mutex_unlock(registry_lock);}
void __cxa_guard_abort(uint64_t*g){core->mutex_lock(registry_lock);((unsigned char*)g)[1]=0;core->mutex_unlock(registry_lock);}
static void game_task(void *arg){
    (void)arg;while(hmm_running && !hmm_state)core->delay(1);
    if(hmm_running){char name[]="HMM2.ELF";char *args[]={name,0};
        core->log("HMM2: вход в main OpenHMM2 1.1.17");
        int result=hmm_game_main(1,args);core->log("HMM2: main завершён, код=%d",result);
        if(hmm_state!=255)hmm_state=result?255:3;
    }
    hmm_done=1;
}
static int start(const zm_core_t*c){
    (void)c;hmm_state=0;hmm_frames=0;hmm_done=0;hmm_running=1;
    if(!hmm_io_init())return ZM_ERROR;
    if(core->task_start(game_task,0,32768,4,1)!=ZM_OK){hmm_running=0;hmm_done=1;return ZM_ERROR;}
    return ZM_OK;
}
static void stop(void){
    hmm_running=0;uint32_t t=core->millis();while(!hmm_done && (uint32_t)(core->millis()-t)<3000)core->delay(1);
    if(!initialized||!hmm_done)return;
    if(hmm_done){
        while(exit_count)exit_functions[--exit_count]();
        for(function*p=__hmm_fini_end;p!=__hmm_fini_begin;)(*--p)();
        for(function*p=__hmm_dtors_begin;p!=__hmm_dtors_end;++p)(*p)();
    }
    __deregister_frame_info(__hmm_eh_begin);initialized=0;
}
int main(int argc,char**argv){
    static const zm_app_t app={sizeof(zm_app_t),1,4,"hmm2","0.1",0x70,0x76,start,stop,hmm_command,0};
    if(argc!=3)return 0;core=(const zm_core_t*)argv[1];
    if(core->abi_major!=1||core->size<offsetof(zm_core_t,link_write)+sizeof(core->link_write))return 0;
    registry_lock=core->mutex_create();if(!registry_lock||!task_id())return 0;
    if(!relocate_frames())return 0;
    for(function*p=__hmm_ctors_end;p!=__hmm_ctors_begin;)(*--p)();
    for(function*p=__hmm_init_begin;p!=__hmm_init_end;++p)(*p)();
    initialized=1;*(const zm_app_t**)argv[2]=&app;return 1;
}
'''
    (ROOT/'main/hmm_app.c').write_text('/* Codex: приложение ABI VDAC4, собственный PIC runtime C++.\n * SPDX-License-Identifier: GPL-2.0-or-later */\n'+s,encoding='utf-8')
if __name__=='__main__':main()
