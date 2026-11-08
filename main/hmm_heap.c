/* Codex: мелкие объекты C++ делят страницы; штатный трекер VDAC4 владеет каждой страницей.
 * Это убирает отдельный AllocHeader ядра у каждого узла списка мира.
 * Выделяет только игровой поток; конструкторы выполняются до его запуска.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_core.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "hmm_platform.h"
/* Клод: pages[k] — только страницы со свободными местами. Прежде malloc шёл по цепочке от начала, пока не встретит свободную, а free искал опустевшую страницу
 * в той же цепочке. На плате страниц сотни (журнал: 192:85 64:50 32:41), и каждый шаг читал заголовок другой страницы в PSRAM, то есть промах кэша.
 * Codex: обе операции O(1); пустые страницы теперь общие для классов и списков Draw. */
typedef struct page {struct page *next,*prev;void *available;unsigned count,kind,pad;} page;
typedef union header {struct {page *owner;size_t size;} h;uint64_t alignment;} header;
/* Codex: измеренный отказ хеш-таблицы 20348 байт при 295184 свободных показывает,
 * что средние массивы нельзя перемежать со страницами Draw. 104 КиБ становятся
 * отдельной областью с объединением соседних свободных блоков. В 91 живые снимки
 * занимали 78336 байт, и прежние 88 КиБ не оставляли цельного места для снимка 9800 Б.
 * Она живёт до выгрузки модуля: таблицы ИИ переживают загрузку нового мира. */
typedef struct medium_block {size_t span,pad;header h;} medium_block;
#define MEDIUM_BYTES (104u*1024u)
#define MEDIUM_OWNER ((page*)(uintptr_t)1)
#define SCRATCH_OWNER ((page*)(uintptr_t)2)
/* Codex: после долгого пути страницы 4 и 3 КиБ не помещались в промежутки
 * 2880–4096 байт между страницами Draw. Мелкие объекты делят страницы по 2 КиБ. */
#define SMALL_PAGE_BYTES 2048u
extern void *hmm_scratch_take(size_t bytes);
extern void hmm_scratch_give(void);
extern void *hmm_gpu_heap_page(size_t bytes);
static uint8_t *medium_region;
size_t hmm_heap_mid_bytes,hmm_heap_mid_peak;
void hmm_heap_plan(void){
    if(medium_region)return;
    medium_region=hmm_core->alloc(MEDIUM_BYTES,ZM_MEM_PSRAM);
    if(medium_region){medium_block *b=(medium_block*)medium_region;b->span=MEDIUM_BYTES;b->h.h.size=0;}
}
static header *medium_take(size_t n){
    if(!medium_region||n<4096||n>65536)return 0;
    const size_t need=sizeof(medium_block)+((n+7)&~(size_t)7);
    medium_block *best=0;
    for(uint8_t *p=medium_region;p<medium_region+MEDIUM_BYTES;){
        medium_block *b=(medium_block*)p;
        if(!b->h.h.size&&b->span>=need&&(!best||b->span<best->span))best=b;
        p+=b->span;
    }
    if(!best)return 0;
    const size_t rest=best->span-need;
    if(rest>=sizeof(medium_block)+8){
        medium_block *tail=(medium_block*)((uint8_t*)best+need);tail->span=rest;tail->h.h.size=0;best->span=need;
    }
    best->h.h.owner=MEDIUM_OWNER;
    hmm_heap_mid_bytes+=n;if(hmm_heap_mid_bytes>hmm_heap_mid_peak)hmm_heap_mid_peak=hmm_heap_mid_bytes;
    return &best->h;
}
static void medium_give(header *h){
    hmm_heap_mid_bytes-=h->h.size;h->h.size=0;
    for(uint8_t *p=medium_region;p<medium_region+MEDIUM_BYTES;){
        medium_block *b=(medium_block*)p;uint8_t *next=p+b->span;
        if(next<medium_region+MEDIUM_BYTES&&!b->h.h.size&&!((medium_block*)next)->h.h.size)b->span+=((medium_block*)next)->span;
        else p=next;
    }
}
static const unsigned capacities[]={8,16,24,32,40,48,64,80,96,128,192,256};
static page *pages[sizeof(capacities)/sizeof(*capacities)];
/* Codex: пустая страница подходит любому классу. Движение Trodo одновременно
 * добавляло до трёх страниц классов 128/192; четыре общие запасные страницы
 * заменяют выдачу/free при каждом кадре и не требуют нового куска PSRAM. */
static page *spare_pages;static unsigned spare_count;
size_t hmm_heap_small_bytes;
/* Codex: младшие биты владельца отмечают большой буфер из общей страницы.
 * Её полный размер сохраняется, чтобы после free вернуть тот же блок в пул. */
static int pooled(page *p){return ((uintptr_t)p&7)==3;}
static page *spare_take(size_t need,int buffer){
    /* Codex: буфер не занимает страницу с лишним местом больше четверти запроса.
     * Иначе палитры по 2 КиБ удерживали страницы Draw по 3336 Б; GateHell терял
     * более 5 КиБ минимума. Мелкие объекты заполняют всю страницу без этого ограничения. */
    page **best=0;for(page **at=&spare_pages;*at;at=&(*at)->next)if((*at)->pad>=need&&(!buffer||(*at)->pad-need<=need/4)&&(!best||(*at)->pad<(*best)->pad))best=at;
    if(!best)return 0;page *p=*best;*best=p->next;--spare_count;return p;
}
static void spare_give(page *p){
    if(spare_count<4){p->next=spare_pages;spare_pages=p;++spare_count;}
    else {hmm_heap_small_bytes-=p->pad;hmm_core->free(p);}
}
/* Клод: учёт для строки HMM2 MEMF (hmm_video.c): байты крупных блоков (>256 Б) сейчас и в пике, их число. */
size_t hmm_heap_big_bytes,hmm_heap_big_peak,hmm_heap_big_count;
static unsigned page_total[sizeof(capacities)/sizeof(*capacities)];
extern void hmm_cache_trim(void);
static void page_unlink(page *p){
    if(p->prev)p->prev->next=p->next;else pages[p->kind]=p->next;
    if(p->next)p->next->prev=p->prev;
    p->next=p->prev=0;
}
static void page_link(page *p){
    p->prev=0;p->next=pages[p->kind];if(p->next)p->next->prev=p;pages[p->kind]=p;
}
static void *allocate(size_t n){
    if(n<=256){
        unsigned k=0;while(capacities[k]<n)++k;
        page *p=pages[k];
        if(!p){
            p=spare_take(SMALL_PAGE_BYTES,0);
            if(!p){
                /* Codex: свободная страница Draw может быть меньше штатных 2 КиБ.
                 * Её реальный размер хранится в заголовке; достаточно двух объектов класса. */
                void *render=hmm_gpu_heap_page(sizeof(page)+2*(capacities[k]+sizeof(header)));
                if(render){header *h=(header*)render-1;const size_t bytes=pooled(h->h.owner)?((uintptr_t)h->h.owner&~(uintptr_t)7):h->h.size+sizeof(header);
                    hmm_heap_big_bytes-=h->h.size;--hmm_heap_big_count;p=(page*)h;p->pad=(unsigned)bytes;}
                else {p=hmm_core->alloc(SMALL_PAGE_BYTES,ZM_MEM_PSRAM);if(!p)return 0;p->pad=SMALL_PAGE_BYTES;}
                hmm_heap_small_bytes+=p->pad;
            }
            p->kind=k;p->count=0;p->available=0;page_link(p);++page_total[k];
            const unsigned stride=capacities[k]+sizeof(header);
            for(unsigned offset=sizeof(page);offset+stride<=p->pad;offset+=stride){
                header *h=(header*)((uint8_t*)p+offset);h->h.owner=p;void *v=h+1;
                *(void**)v=p->available;p->available=v;
            }
        }
        void *v=p->available;p->available=*(void**)v;++p->count;
        if(!p->available)page_unlink(p);
        ((header*)v-1)->h.size=n;return v;
    }
    if(n>SIZE_MAX-sizeof(header))return 0;
    header *h=medium_take(n);
    if(!h&&n>=300*1024){h=hmm_scratch_take(n+sizeof(header));if(h)h->h.owner=SCRATCH_OWNER;}
    if(!h){page *p=spare_take(n+sizeof(header),1);if(p){const size_t bytes=p->pad;hmm_heap_small_bytes-=bytes;h=(header*)p;h->h.owner=(page*)(uintptr_t)(bytes|3u);}}
    if(!h){h=hmm_core->alloc(n+sizeof(header),ZM_MEM_PSRAM);if(!h)return 0;h->h.owner=0;}
    hmm_heap_big_bytes+=n;++hmm_heap_big_count;if(hmm_heap_big_bytes>hmm_heap_big_peak)hmm_heap_big_peak=hmm_heap_big_bytes;
    h->h.size=n;return h+1;
}
/* Клод: при первых отказах пишем в журнал, сколько страниц мелких объектов занято по классам.
 * noinline: строка журнала не должна увеличивать кадр стека самого malloc. */
__attribute__((noinline)) static void report_pages(size_t n){
    static unsigned reports;if(reports>=4)return;++reports;
    char text[200];int at=snprintf(text,sizeof(text),"HMM2 HEAP: request=%u pages",(unsigned)n);
    for(unsigned k=0;k<sizeof(capacities)/sizeof(*capacities)&&at>0&&at<(int)sizeof(text);k++){
        at+=snprintf(text+at,sizeof(text)-at," %u:%u",capacities[k],page_total[k]);
    }
    hmm_log(text);
}
/* Клод: пустые страницы про запас отдаются перед сбросом кэшей. */
static void release_spare_pages(void){
    while(spare_pages){page *p=spare_pages;spare_pages=p->next;hmm_heap_small_bytes-=p->pad;hmm_core->free(p);}spare_count=0;
}
extern void hmm_cache_last_resort(void);
/* Codex: отказ отдаёт свободные страницы рендера; области кэшей мира сохраняются.
 * Последняя попытка больше не превращает рабочую арену спрайтов в невосстановимый резерв. */
void *malloc(size_t n){void *p=allocate(n?n:1);if(!p){report_pages(n);release_spare_pages();hmm_cache_trim();p=allocate(n?n:1);
    if(!p){hmm_cache_last_resort();p=allocate(n?n:1);}}return p;}
void free(void *v){
    if(!v)return;header *h=(header*)v-1;page *p=h->h.owner;
    if(!p||p==MEDIUM_OWNER||p==SCRATCH_OWNER||pooled(p)){hmm_heap_big_bytes-=h->h.size;--hmm_heap_big_count;
        if(p==MEDIUM_OWNER)medium_give(h);else if(p==SCRATCH_OWNER)hmm_scratch_give();
        else if(pooled(p)){const size_t bytes=(uintptr_t)p&~(uintptr_t)7;page *v=(page*)h;v->pad=(unsigned)bytes;hmm_heap_small_bytes+=bytes;spare_give(v);}
        else hmm_core->free(h);return;}
    if(!p->available)page_link(p);
    *(void**)v=p->available;p->available=v;
    if(--p->count==0){page_unlink(p);--page_total[p->kind];
        spare_give(p);}
}
void *realloc(void *p,size_t n){
    if(!p)return malloc(n);if(!n){free(p);return 0;}header *h=(header*)p-1;
    if(h->h.owner==MEDIUM_OWNER||h->h.owner==SCRATCH_OWNER||pooled(h->h.owner)){if(n<=h->h.size)return p;}
    else if(h->h.owner&&n<=capacities[h->h.owner->kind]){h->h.size=n;return p;}
    void *q=malloc(n);if(q){memcpy(q,p,n<h->h.size?n:h->h.size);free(p);}return q;
}
void *calloc(size_t n,size_t size){if(size&&n>SIZE_MAX/size)return 0;size_t bytes=n*size;void*p=malloc(bytes);if(p)memset(p,0,bytes);return p;}
/* Codex: число страниц мелких объектов по 2 КиБ — для строки HMM2 MEMF. */
unsigned hmm_heap_small_pages(void){unsigned n=spare_count;for(unsigned k=0;k<sizeof(capacities)/sizeof(*capacities);k++)n+=page_total[k];return n;}
