/* Клод: кэш RAM_G (prototype/ft_sprite.c) на долгой игре: кадры по очереди (hft_cache_begin), в каждом — свой набор
 * мелких текстур (тайлы, спрайты) и иногда большая картинка 640×480 (300 страниц), как окно настроек на плате.
 * Сборка 76 на плате, 58 минут игры: RAMG need=307200 shown=1818 build=1470 free=1103 — места хватало, но не подряд.
 * Ожидание: ни одного отказа при рабочем наборе кадра заметно меньше ёмкости. Печать: отказы и самый длинный свободный кусок. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ft_sprite.h"
static hft_cache cache;
static unsigned rng=12345;
static unsigned next(void){rng=rng*1103515245u+12345u;return (rng>>16)&0x7fff;}
static unsigned longest_free(void){unsigned best=0,run=0;for(unsigned i=0;i<HFT_PAGES;i++){run=cache.pages[i]?0:run+1;if(run>best)best=run;}return best;}
/* Страницы, которые можно получить: свободные и занятые незакреплёнными записями. */
static unsigned reclaimable(void){unsigned n=0;for(unsigned i=0;i<HFT_PAGES;i++)n+=!cache.pages[i];for(unsigned i=0;i<HFT_ENTRIES;i++)if(cache.entries[i].valid&&!cache.entries[i].pins)n+=cache.entries[i].pages;return n;}
// Codex: редкая фаза атласа остаётся в RAM_G после тысяч одноразовых CPU-снимков.
static int cpu_churn(void){
    hft_cache_init(&cache);uint32_t address;int upload;unsigned missed=0;
    for(unsigned i=0;i<70;++i)if(!hft_cache_get(&cache,0x1000000000000000ull|i,15360,&address,&upload))return 1;
    for(unsigned f=0;f<5000;++f){
        hft_cache_begin(&cache,f&1);
        if(!hft_cache_get(&cache,0x3000000000000000ull|f,7104,&address,&upload))return 1;
    }
    for(unsigned i=0;i<70;++i){if(!hft_cache_get(&cache,0x1000000000000000ull|i,15360,&address,&upload))return 1;missed+=(unsigned)upload;}
    printf("CPU churn: 5000 snapshots, atlas reloads=%u\n",missed);return missed!=0;
}
int main(void){
    if(cpu_churn())return 1;
    hft_cache_init(&cache);
    uint32_t address;int upload;unsigned failures=0,frames=60000,displayed=0,min_reclaimable=HFT_PAGES,max_reclaimable_ok=0;
    /* DL, как в hmm_gpu_init: 8 КиБ закреплены навсегда (pins 16). */
    hft_cache_get(&cache,0x4000000000000000ull,8192,&address,&upload);cache.entries[cache.last_entry].pins|=16;
    /* Части карты (map_keep_texture) держат свои текстуры (pins 4) многие кадры, пока часть жива: при прокрутке
     * часть уходит, её текстуры отпускаются, новые закрепляются там, где нашлось место. На плате это cached=1470 страниц. */
    enum {PARTS=450};unsigned part_id[PARTS],part_entry[PARTS];
    for(unsigned i=0;i<PARTS;i++){part_id[i]=100000+i;part_entry[i]=HFT_ENTRIES;}
    unsigned next_part=100000+PARTS;
    for(unsigned f=0;f<frames;f++){
        displayed^=1;hft_cache_begin(&cache,displayed);
        if(f%20==0)for(unsigned k=0;k<30;k++){
            const unsigned i=next()%PARTS;
            if(part_entry[i]<HFT_ENTRIES)cache.entries[part_entry[i]].pins&=(uint8_t)~4u;
            part_id[i]=next_part++;part_entry[i]=HFT_ENTRIES;
        }
        for(unsigned i=0;i<PARTS;i++){
            const unsigned bytes=((part_id[i]*2654435761u>>24)%4u+1u)*1024u;
            if(!hft_cache_get(&cache,0x0800000000000000ull|part_id[i],bytes,&address,&upload)){failures++;continue;}
            part_entry[i]=cache.last_entry;cache.entries[part_entry[i]].pins|=4u;
        }
        /* Набор кадра: «область» карты сдвигается со временем — из 4000 мелких текстур берётся окно в 300 подряд. */
        const unsigned base=(f/40)%3700;
        for(unsigned k=0;k<60;k++){
            const unsigned id=base+next()%300;const unsigned bytes=((id*7919u)%12u+1u)*1024u;
            if(!hft_cache_get(&cache,0x1000000000000000ull|id,bytes,&address,&upload))failures++;
        }
        /* Постоянная большая рамка экрана карты и иногда большие окна (8 разных, 640×480). */
        if(!hft_cache_get(&cache,0x2000000000000000ull,307200,&address,&upload))failures++;
        if(f%97==0){
            const unsigned can=reclaimable();
            if(!hft_cache_get(&cache,0x3000000000000000ull|(f/97)%8,307200,&address,&upload)){
                failures++;if(failures<=3)printf("отказ на кадре %u: доступно страниц %u, самый длинный кусок %u\n",f,can,longest_free());
            }else if(can>max_reclaimable_ok)max_reclaimable_ok=can;
            if(can<min_reclaimable)min_reclaimable=can;
        }
    }
    printf("при запросе 300 страниц доступно было не меньше %u страниц\n",min_reclaimable);
    printf("кадров %u, отказов %u, самый длинный свободный кусок в конце %u страниц\n",frames,failures,longest_free());
    /* Прежнее выделение «первый подходящий кусок» давало здесь 618 отказов. Редкий отказ игра переживает: слой пропускается
     * на кадр (hmm_gpu.cpp, texture_missing). Требование — не чаще одного отказа на 10 000 кадров. */
    return failures*10000u>frames?1:0;
}
