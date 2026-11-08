/* Codex: приложение ABI VDAC4, собственный PIC runtime C++.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "zm_app.h"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

const zm_core_t *hmm_core;
#define core hmm_core
#include "hmm_core.h"
#include "hmm_platform.h"
#include <string.h>
static void *registry_lock;
typedef void (*function)(void);
extern function __hmm_ctors_begin[],__hmm_ctors_end[],__hmm_init_begin[],__hmm_init_end[];
extern function __hmm_dtors_begin[],__hmm_dtors_end[],__hmm_fini_begin[],__hmm_fini_end[];
extern unsigned char __hmm_eh_begin[],__hmm_eh_end[];
extern void __register_frame_info(const void*,void*);
extern void *__deregister_frame_info(const void*);
static uintptr_t frame_object[32];
volatile int32_t hmm_linked_delta=0x4d324548;
static int initialized;
/* Codex: threadptr читается как идентификатор задачи; TLS ядра не изменяется. */
static uintptr_t task_id(void){uintptr_t id;__asm__ volatile("rur.threadptr %0":"=a"(id));return id;}
static struct {uintptr_t id;void *values[16];} tasks[16];
static struct {int used;void(*destroy)(void*);} keys[16];
int pthread_key_create(pthread_key_t*k,void(*destroy)(void*)){
    core->mutex_lock(registry_lock);int result=EAGAIN;
    for(unsigned i=0;i<16;i++)if(!keys[i].used){keys[i].used=1;keys[i].destroy=destroy;*k=i;result=0;break;}
    core->mutex_unlock(registry_lock);return result;
}
int pthread_key_delete(pthread_key_t k){
    if(k>=16)return EINVAL;core->mutex_lock(registry_lock);keys[k].used=0;
    for(unsigned i=0;i<16;i++)tasks[i].values[k]=0;
    core->mutex_unlock(registry_lock);return 0;
}
int pthread_setspecific(pthread_key_t k,const void*v){
    if(k>=16||!keys[k].used)return EINVAL;
    uintptr_t id=task_id();if(!id)return ENOTSUP;
    core->mutex_lock(registry_lock);int result=EAGAIN;
    for(unsigned i=0;i<16;i++)if(tasks[i].id==id||!tasks[i].id){tasks[i].id=id;tasks[i].values[k]=(void*)v;result=0;break;}
    core->mutex_unlock(registry_lock);return result;
}
void *pthread_getspecific(pthread_key_t k){
    if(k>=16)return 0;uintptr_t id=task_id();void *v=0;
    core->mutex_lock(registry_lock);for(unsigned i=0;i<16;i++)if(tasks[i].id==id){v=tasks[i].values[k];break;}
    core->mutex_unlock(registry_lock);return v;
}
int pthread_mutex_init(pthread_mutex_t*m,const pthread_mutexattr_t*a){
    if(a && a->recursive)return ENOTSUP;
    void *p=core->mutex_create();if(!p)return ENOMEM;*m=(uintptr_t)p;return 0;
}
int pthread_mutex_lock(pthread_mutex_t*m){
    if(*m==_PTHREAD_MUTEX_INITIALIZER){
        core->mutex_lock(registry_lock);
        if(*m==_PTHREAD_MUTEX_INITIALIZER && pthread_mutex_init(m,0)){core->mutex_unlock(registry_lock);return ENOMEM;}
        core->mutex_unlock(registry_lock);
    }
    core->mutex_lock((void*)(uintptr_t)*m);return 0;
}
int pthread_mutex_unlock(pthread_mutex_t*m){core->mutex_unlock((void*)(uintptr_t)*m);return 0;}
int pthread_mutex_destroy(pthread_mutex_t*m){*m=_PTHREAD_MUTEX_INITIALIZER;return 0;}
/* Codex: ABI освобождает созданные mutex при выгрузке; повторный init в этом стенде не выполняется. */
char *getenv(const char*n){(void)n;return 0;}
extern const char *hmm_exception_type(void);
void abort(void){hmm_state=255;if(!hmm_error[0])strcpy(hmm_error,"C++ runtime abort");core->log("HMM2: аварийное завершение runtime: %s, exception=%s caller=%p",hmm_error,hmm_exception_type(),__builtin_return_address(0));for(;;)core->delay(1000);}
void __cxa_pure_virtual(void){strcpy(hmm_error,"Pure virtual function call");abort();}
static void anchor(void){}
static int relocate_frames(void){
    int32_t bias=(intptr_t)anchor-(intptr_t)__hmm_eh_begin-hmm_linked_delta;
    unsigned char*p=__hmm_eh_begin;unsigned count=0;
    while(p+4<=__hmm_eh_end){
        uint32_t n=*(uint32_t*)p;if(!n)break;
        if(n==0xffffffffu || n<8 || p+4+n>__hmm_eh_end)return 0;
        if(*(uint32_t*)(p+4)){*(int32_t*)(p+8)+=bias;count++;}
        p+=4+n;
    }
    __register_frame_info(__hmm_eh_begin,frame_object);
    core->log("HMM2: исправлено FDE=%u, смещение=%d",count,(int)bias);return 1;
}

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
static int start(const zm_core_t*c){
    (void)c;hmm_session_reset();
    if(!hmm_io_init()){hmm_running=0;hmm_done=1;return ZM_ERROR;}
    /* Codex: один штатный стек FreeRTOS во внутренней RAM; расход проверяется стендом.
     * Клод: приоритет 1, как у Arduino loopTask на этом же ядре: только он владеет каналом SPI к Evo. При приоритете 4 любой счёт игры без ожидания
     * останавливал канал: запросы задачи звука не уходили (журнал сборки 68: игра 99 %, loopTask нет, Evo ответил на E4 через 29 с). */
    if(core->task_start(hmm_session_task,0,16384,1,1)!=ZM_OK){hmm_running=0;hmm_done=1;return ZM_ERROR;}
    return ZM_OK;
}
void hmm_runtime_close(void){
    if(!initialized)return;
    {
        while(exit_count)exit_functions[--exit_count]();
        for(function*p=__hmm_fini_end;p!=__hmm_fini_begin;)(*--p)();
        for(function*p=__hmm_dtors_begin;p!=__hmm_dtors_end;++p)(*p)();
    }
    __deregister_frame_info(__hmm_eh_begin);initialized=0;
}
static void stop(void){
    hmm_session_stop();
    if(hmm_done)hmm_runtime_close();
}
int hmm_module_main(int argc,char**argv){
    /* Codex: ModuleApp предлагает 31 активному ELF раньше AudioApp; транспорт DMA штатный. */
    static const zm_app_t app={sizeof(zm_app_t),1,4,"hmm2","0.1",0x31,0x77,start,stop,hmm_command,hmm_video_poll};
    if(argc!=3)return 0;core=(const zm_core_t*)argv[1];
    if(core->abi_major!=1||core->size<offsetof(zm_core_t,link_write)+sizeof(core->link_write))return 0;
    registry_lock=core->mutex_create();if(!registry_lock||!task_id())return 0;
    if(!relocate_frames())return 0;
    for(function*p=__hmm_ctors_end;p!=__hmm_ctors_begin;)(*--p)();
    for(function*p=__hmm_init_begin;p!=__hmm_init_end;++p)(*p)();
    initialized=1;*(const zm_app_t**)argv[2]=&app;return 1;
}
