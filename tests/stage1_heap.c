/* Codex: случайные выделения, перенос realloc и полный возврат страниц ядру. */
#include "hmm_core.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
void *hmm_malloc(size_t);void hmm_free(void*);void *hmm_realloc(void*,size_t);void *hmm_calloc(size_t,size_t);
static size_t live,peak,limit=8*1024*1024;static unsigned trims;
static void *allocate(size_t n,uint32_t flags){(void)flags;if(n+16>limit-live)return 0;uint32_t*p=malloc(n+16);assert(p);p[0]=(uint32_t)n;live+=n+16;if(live>peak)peak=live;return p+4;}
static void release(void*p){if(p){uint32_t*q=(uint32_t*)p-4;live-=q[0]+16;free(q);}}
static zm_core_t core={0};const zm_core_t*hmm_core=&core;
void hmm_cache_trim(void){++trims;}
static uint32_t seed=12345;
static unsigned random_value(void){seed=seed*1664525+1013904223;return seed;}
int main(void){
    core.alloc=allocate;core.free=release;
    void *slots[4096]={0};size_t sizes[4096]={0};uint8_t values[4096]={0};
    for(unsigned step=0;step<200000;step++){
        unsigned i=random_value()%4096;uint8_t*p=slots[i];
        if(p){for(size_t j=0;j<sizes[i];j++)assert(p[j]==values[i]);}
        if(step%3==0){hmm_free(p);slots[i]=0;sizes[i]=0;continue;}
        size_t n=random_value()%1024+1;uint8_t*q=hmm_realloc(p,n);assert(q);assert((uintptr_t)q%8==0);
        if(p){for(size_t j=0;j<(sizes[i]<n?sizes[i]:n);j++)assert(q[j]==values[i]);}
        slots[i]=q;sizes[i]=n;values[i]=(uint8_t)random_value();memset(q,values[i],n);
    }
    for(unsigned i=0;i<4096;i++)hmm_free(slots[i]);assert(!live);
    unsigned char*z=hmm_calloc(257,3);assert(z);for(unsigned i=0;i<771;i++)assert(!z[i]);hmm_free(z);
    assert(!hmm_calloc(SIZE_MAX,2));limit=4095;assert(!hmm_malloc(4));assert(trims==1);assert(!live);
    printf("HEAP OK operations=200000 peak=%zu returned=%zu\n",peak,live);return 0;
}
