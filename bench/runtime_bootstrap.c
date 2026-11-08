/* Codex: эксперимент загрузки настоящего PIC libsupc++; это стенд runtime, а не подмена игры.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "zm_app.h"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

static const zm_core_t *core;
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
void abort(void){core->log("HRT: аварийное завершение runtime");for(;;)core->delay(1000);}
void __cxa_pure_virtual(void){abort();}
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
    core->log("HRT: исправлено FDE=%u, смещение=%d",count,(int)bias);return 1;
}
extern int app_entry(int,char**);
extern int runtime_destructed(void);
extern void abi_metrics(void(*)(const char*,...));
static int start(const zm_core_t*c){
    (void)c;int result=app_entry(0,0);core->log("HRT: string/vector/throw/catch: %d",result);abi_metrics(core->log);return result==19?ZM_OK:ZM_ERROR;
}
static void stop(void){
    if(!initialized)return;
    for(function*p=__hmm_fini_end;p!=__hmm_fini_begin;)(*--p)();
    for(function*p=__hmm_dtors_begin;p!=__hmm_dtors_end;++p)(*p)();
    core->log("HRT: вызвано деструкторов контрольного объекта=%d",runtime_destructed());
    __deregister_frame_info(__hmm_eh_begin);initialized=0;
}
int main(int argc,char**argv){
    static const zm_app_t app={sizeof(zm_app_t),1,0,"hrt","0.1",0x70,0x70,start,stop,0,0};
    if(argc!=3)return 0;core=(const zm_core_t*)argv[1];
    registry_lock=core->mutex_create();if(!registry_lock||!task_id())return 0;
    if(!relocate_frames())return 0;
    for(function*p=__hmm_ctors_end;p!=__hmm_ctors_begin;)(*--p)();
    for(function*p=__hmm_init_begin;p!=__hmm_init_end;++p)(*p)();
    initialized=1;*(const zm_app_t**)argv[2]=&app;return 1;
}
