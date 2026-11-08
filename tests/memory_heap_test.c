/* Codex: исходный выделитель проверяется с подменой только ядра VDAC4.
 * Содержимое живых блоков проверяется после каждого перемешанного действия. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ZM_MEM_PSRAM 0
static unsigned core_calls,fail_core;
static void *core_alloc(size_t n,int caps){(void)caps;++core_calls;return fail_core?NULL:malloc(n);}
static struct {void *(*alloc)(size_t,int);void (*test_free)(void*);} mock_core={core_alloc,free};
#define hmm_core (&mock_core)
static void hmm_log(const char *s){(void)s;}
void hmm_cache_trim(void){}
void hmm_cache_last_resort(void){}
static uint64_t scratch[40000];static unsigned scratch_enabled,scratch_busy;
static void *donation;
void *hmm_gpu_heap_page(size_t n){void *p=donation;if(p){assert(n<=3328);donation=NULL;}return p;}
void *hmm_scratch_take(size_t n){if(!scratch_enabled||scratch_busy||n>sizeof(scratch))return NULL;scratch_busy=1;return scratch;}
void hmm_scratch_give(void){assert(scratch_busy);scratch_busy=0;}
#include "memory_heap_impl.inc"
typedef struct {unsigned char *ptr;size_t size;unsigned char value;} Live;
static Live live[96];
static uint32_t rng=0x452781;
static unsigned next_random(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void verify(void){
    size_t sum=0;unsigned blocks=0;
    for(unsigned i=0;i<96;++i){
        Live *v=live+i;if(!v->ptr)continue;
        assert(((uintptr_t)v->ptr&7)==0);
        for(size_t j=0;j<v->size;++j)assert(v->ptr[j]==v->value);
        for(unsigned k=i+1;k<96;++k)if(live[k].ptr)assert((uintptr_t)v->ptr+v->size<=(uintptr_t)live[k].ptr||(uintptr_t)live[k].ptr+live[k].size<=(uintptr_t)v->ptr);
    }
    for(uint8_t *p=medium_region;p<medium_region+MEDIUM_BYTES;){
        medium_block *b=(medium_block*)p;assert(b->span>=sizeof(*b)&&!(b->span&7));
        assert(p+b->span<=medium_region+MEDIUM_BYTES);sum+=b->h.h.size;p+=b->span;++blocks;
    }
    assert(sum==hmm_heap_mid_bytes&&blocks<64);
}
int main(void){
    hmm_heap_plan();assert(medium_region);unsigned calls=core_calls;hmm_heap_plan();assert(calls==core_calls);
    void *a=test_malloc(20348),*b=test_malloc(32768),*c=test_malloc(8192);
    assert(a&&b&&c&&core_calls==calls);test_free(b);test_free(a);test_free(c);
    assert(((medium_block*)medium_region)->span==MEDIUM_BYTES&&!hmm_heap_mid_bytes);
    // Codex: пик живых снимков 91 плюс новый снимок не должен обращаться к
    // фрагментированной куче ядра. Старые 88 КиБ не вмещали эту комбинацию.
    a=test_malloc(20480);b=test_malloc(32768);c=test_malloc(30792);assert(a&&b&&c);
    calls=core_calls;fail_core=1;void *snapshot=test_malloc(9800);assert(snapshot&&core_calls==calls);
    test_free(snapshot);test_free(a);test_free(b);test_free(c);fail_core=0;
    assert(((medium_block*)medium_region)->span==MEDIUM_BYTES&&!hmm_heap_mid_bytes);
    for(unsigned step=0;step<20000;++step){
        Live *v=live+next_random()%96;
        if(v->ptr){
            if(next_random()%3){test_free(v->ptr);v->ptr=NULL;}
            else {size_t n=1+next_random()%70000;unsigned char *q=test_realloc(v->ptr,n);assert(q);for(size_t j=0;j<(n<v->size?n:v->size);++j)assert(q[j]==v->value);v->ptr=q;v->size=n;memset(q,v->value,n);}
        }else {v->size=1+next_random()%70000;v->value=(unsigned char)next_random();v->ptr=test_malloc(v->size);assert(v->ptr);memset(v->ptr,v->value,v->size);}
        verify();
    }
    for(unsigned i=0;i<96;++i)test_free(live[i].ptr);
    assert(!hmm_heap_mid_bytes&&!hmm_heap_big_bytes&&!hmm_heap_big_count);
    assert(((medium_block*)medium_region)->span==MEDIUM_BYTES);
    for(unsigned kind=0;kind<sizeof(capacities)/sizeof(*capacities);++kind){
        unsigned char *items[512];const unsigned n=capacities[kind];
        for(unsigned i=0;i<512;++i){items[i]=test_malloc(n);assert(items[i]);memset(items[i],i,n);}
        for(unsigned i=1;i<512;i+=2)test_free(items[i]);
        for(unsigned i=0;i<512;i+=2)for(unsigned j=0;j<n;++j)assert(items[i][j]==(unsigned char)i);
        for(unsigned i=1;i<512;i+=2){items[i]=test_malloc(n);assert(items[i]);memset(items[i],i,n);}
        for(unsigned i=0;i<512;++i){for(unsigned j=0;j<n;++j)assert(items[i][j]==(unsigned char)i);test_free(items[i]);}
    }
    calls=core_calls;fail_core=1;
    for(unsigned repeat=0;repeat<1000;++repeat)for(unsigned kind=0;kind<sizeof(capacities)/sizeof(*capacities);++kind){
        void *items[4];for(unsigned i=0;i<4;++i){items[i]=test_malloc(capacities[kind]);assert(items[i]);}
        for(unsigned i=0;i<4;++i)test_free(items[i]);
    }
    assert(core_calls==calls&&spare_count<=4);fail_core=0;
    release_spare_pages();assert(!hmm_heap_small_pages());
    a=test_calloc(2,10000);assert(a);for(unsigned i=0;i<20000;++i)assert(!((unsigned char*)a)[i]);
    fail_core=1;assert(!test_realloc(a,100000));assert(test_realloc(a,100)==a);test_free(a);fail_core=0;
    scratch_enabled=1;calls=core_calls;a=test_malloc(307200);assert(a&&scratch_busy&&core_calls==calls);
    memset(a,0x5a,307200);assert(test_realloc(a,300000)==a);
    fail_core=1;assert(!test_malloc(307200));for(unsigned i=0;i<300000;++i)assert(((unsigned char*)a)[i]==0x5a);fail_core=0;
    test_free(a);assert(!scratch_busy&&!hmm_heap_big_bytes&&!hmm_heap_big_count);
    assert(!test_calloc(SIZE_MAX,2));release_spare_pages();
    // Codex: бывшая страница Draw меняет назначение без единого обращения к ядру.
    donation=test_malloc(3328);assert(donation);calls=core_calls;fail_core=1;
    void *small[4];for(unsigned i=0;i<4;++i){small[i]=test_malloc(128);assert(small[i]);memset(small[i],i+1,128);}
    assert(!donation&&core_calls==calls);for(unsigned i=0;i<4;++i)test_free(small[i]);
    // Codex: постоянный буфер 2 КиБ не забирает слишком большую страницу;
    // она остаётся доступна следующему запросу 3 КиБ без работающей кучи ядра.
    fail_core=0;void *palette=test_malloc(2048);assert(palette&&((header*)palette-1)->h.owner==0&&spare_count==1);
    test_free(palette);fail_core=1;calls=core_calls;
    a=test_malloc(3000);assert(a&&core_calls==calls);memset(a,0x59,3000);assert(test_realloc(a,2900)==a);test_free(a);
    a=test_malloc(192);assert(a&&core_calls==calls);test_free(a);fail_core=0;
    release_spare_pages();assert(!hmm_heap_small_bytes&&!hmm_heap_big_bytes&&!hmm_heap_big_count);free(medium_region);
    printf("Heap: 20000 mixed operations, data preservation, realloc failure, coalescing OK; medium peak=%zu\n",hmm_heap_mid_peak);
}
