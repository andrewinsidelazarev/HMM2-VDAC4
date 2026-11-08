/* Codex: только штатные команды FT812; ближайший тексель и отражение с поправкой минус 1/256. */
#include "ft_sprite.h"
#include <string.h>
uint32_t hft_emit(uint32_t *out,uint32_t capacity,const hft_sprite*s){
    if(capacity<10||s->sw<=0||s->sh<=0||s->w<=0||s->h<=0||s->sw>2047||s->sh>2047||s->w>2047||s->h>2047)return 0;
    out[0]=0x01000000u|(s->source&0x3fffffu);
    out[1]=0x2a000000u|(s->palette&0x3fffffu);
    out[2]=0x07000000u|(16u<<19)|((s->sw&1023)<<9)|(s->sh&511);
    out[3]=0x28000000u|((s->sw>>10)<<2)|(s->sh>>9);
    out[4]=0x08000000u|((s->w&511)<<9)|(s->h&511);
    out[5]=0x29000000u|((s->w>>9)<<2)|(s->h>>9);
    out[6]=0x15000000u|((s->flip?-256:256)&0x1ffff);
    out[7]=0x17000000u|((s->flip?s->sw*256-1-s->ix*256:s->ix*256)&0xffffff);
    out[8]=0x1a000000u|((s->iy*256)&0xffffff);
    out[9]=0x40000000u|(((s->x*16)&0x7fff)<<15)|((s->y*16)&0x7fff);
    return 10;
}
uint32_t hft_compact(uint32_t *words,uint32_t count){
    /* Codex: убираем только повторные установки регистров нашего эмиттера; порядок рисования сохраняется.
     * Не применять к произвольным DL с CALL, SAVE/RESTORE_CONTEXT или чужими настройками. */
    uint32_t state[9]={0},valid=0,written=0;
    const uint8_t opcodes[9]={1,0x2a,7,0x28,8,0x29,0x15,0x17,0x1a};
    for(uint32_t i=0;i<count;i++){
        unsigned slot=9;for(unsigned j=0;j<9;j++)if((words[i]>>24)==opcodes[j]){slot=j;break;}
        if(slot<9){if((valid&(1u<<slot))&&state[slot]==words[i])continue;state[slot]=words[i];valid|=1u<<slot;}
        words[written++]=words[i];
    }
    return written;
}
int hft_precompose(uint8_t *history,int width,int height,const hft_sprite*s,
    const uint8_t*pixels,const uint8_t*mask,int copy,const uint8_t*transform,uint8_t*patch,uint8_t*patch_mask){
    /* Codex: точное применение таблицы оригинала к индексам фона; FT получает готовые цвета теней.
     * История в ESP обязательна: одна альфа не воспроизводит табличное преобразование оригинала. */
    if(s->x<0||s->y<0||s->x+s->w>width||s->y+s->h>height||s->ix<0||s->iy<0||s->ix+s->w>s->sw||s->iy+s->h>s->sh)return -1;
    int transformed=0;
    for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++){
        int sx=s->flip?s->sw-1-s->ix-x:s->ix+x;
        int at=(s->iy+y)*s->sw+sx,dest=(s->y+y)*width+s->x+x,part=y*s->w+x;
        uint8_t k=copy||!mask?0:mask[at];if(k>=16)return -1;
        if(k==0)history[dest]=pixels[at];
        else if(k>=2){history[dest]=transform[k*256+history[dest]];transformed=1;}
        if(patch){patch[part]=history[dest];patch_mask[part]=k==1?1:0;}
    }
    return transformed;
}
void hft_cache_init(hft_cache*c){memset(c,0,sizeof(*c));c->free_low=HFT_SMALL_TOP;c->free_high=HFT_PAGES-HFT_SMALL_TOP;}
void hft_cache_begin(hft_cache*c,int displayed_slot){
    /* Codex: вызывающий подтверждает DLSWAP=0. Показанный слот остаётся закреплённым. */
    c->build_slot=displayed_slot^1;
    for(unsigned i=0;i<HFT_ENTRIES;i++)c->entries[i].pins&=(uint8_t)~(1u<<c->build_slot);
}
/* Клод: хеш-индекс записей; удаление — со сдвигом цепочки назад, без «надгробий». */
static unsigned slot_of(uint64_t key){return (unsigned)(((uint32_t)(key^(key>>32))*2654435761u)>>(32-HFT_INDEX_BITS))&(HFT_INDEX-1);}
static void index_add(hft_cache*c,unsigned i){unsigned h=slot_of(c->entries[i].key);while(c->index[h])h=(h+1)&(HFT_INDEX-1);c->index[h]=(uint16_t)(i+1);}
static void index_remove(hft_cache*c,unsigned i){
    unsigned h=slot_of(c->entries[i].key);while(c->index[h]!=i+1)h=(h+1)&(HFT_INDEX-1);
    for(unsigned j=h;;){
        j=(j+1)&(HFT_INDEX-1);const unsigned v=c->index[j];if(!v)break;
        const unsigned k=slot_of(c->entries[v-1].key);
        if(h<j?(k>h&&k<=j):(k>h||k<=j))continue;
        c->index[h]=(uint16_t)v;h=j;
    }
    c->index[h]=0;
}
/* Клод: страницы помечаются вместе со счётчиками свободных ниже и выше HFT_SMALL_TOP: по ним промах не сканирует заведомо полный регион. */
static void pages_set(hft_cache*c,unsigned at,unsigned n,int used){
    memset(c->pages+at,used,n);const unsigned low=at>=HFT_SMALL_TOP?0:(at+n<=HFT_SMALL_TOP?n:HFT_SMALL_TOP-at);
    if(used){c->free_low-=low;c->free_high-=n-low;}else{c->free_low+=low;c->free_high+=n-low;}
}
static void erase(hft_cache*c,unsigned i){hft_entry*e=c->entries+i;index_remove(c,i);pages_set(c,e->page,e->pages,0);e->valid=0;c->entry_hint=i;}
static void place(hft_cache*c,unsigned i,uint64_t key,uint32_t bytes,unsigned at,unsigned need){
    pages_set(c,at,need,1);hft_entry*e=c->entries+i;
    e->key=key;e->bytes=bytes;e->age=++c->age;e->page=(uint16_t)at;e->pages=(uint16_t)need;e->pins=(uint8_t)(1u<<c->build_slot);e->valid=1;
    index_add(c,i);c->last_entry=i;c->entry_hint=(i+1)&(HFT_ENTRIES-1);
}
int hft_cache_ready(const hft_cache*c,uint64_t key,uint32_t bytes){
    if(!bytes||bytes>HFT_PAGES*HFT_PAGE_BYTES)return 0;
    for(unsigned h=slot_of(key);c->index[h];h=(h+1)&(HFT_INDEX-1)){const hft_entry*e=c->entries+c->index[h]-1;if(e->key==key&&e->bytes==bytes)return 1;}
    const unsigned need=(bytes+HFT_PAGE_BYTES-1)/HFT_PAGE_BYTES;if(c->free_low+c->free_high<need)return 0;
    unsigned run=0;for(unsigned i=HFT_PAGES;i--;){run=c->pages[i]?0:run+1;if(run==need)return 1;}
    return 0;
}
int hft_cache_get(hft_cache*c,uint64_t key,uint32_t bytes,uint32_t *address,int *upload){
    if(!bytes||bytes>HFT_PAGES*HFT_PAGE_BYTES)return 0;
    for(unsigned h=slot_of(key);c->index[h];h=(h+1)&(HFT_INDEX-1)){hft_entry*e=c->entries+c->index[h]-1;if(e->key==key&&e->bytes==bytes){c->last_entry=c->index[h]-1;e->pins|=1u<<c->build_slot;e->age=++c->age;*address=e->page*HFT_PAGE_BYTES;*upload=0;return 1;}}
    unsigned need=(bytes+HFT_PAGE_BYTES-1)/HFT_PAGE_BYTES;
    /* Клод: большие текстуры (от 64 КиБ: фоны и окна 640×480) ищут место сверху вниз, мелкие — снизу вверх и сначала только в нижних
     * HFT_SMALL_TOP страницах, вытесняя старые незакреплённые оттуда же; выше идут, лишь когда внизу не освободить. Кэш жадный:
     * прежде мелкие текстуры со временем занимали весь RAM_G, закреплённые текстуры частей карты оставались разбросанными,
     * и через час игры картинке 640×480 не находилось 300 КиБ подряд при 1,1 МиБ свободных
     * (сборка 76 на плате, окно настроек; tests/test_hft_cache.c).
     * Клод: промах был дорог — на каждом полный перебор 1024 записей ради свободной, затем ради самой старой, и проход по всем страницам
     * (трасса QEMU, Alteris: 10 тысяч команд на промах, 17 % кадра плотной карты). Теперь свободная запись ищется от подсказки,
     * вытесняемая — «часами» (первая не тронутая за последние HFT_ENTRIES обращений, иначе самая старая из просмотренных), страницы
     * сканируются, только если свободных хватает, а мелкая текстура ложится на место вытесненной, если та не меньше. */
    const int big=need>=64;
    for(int wide=big;wide<2;wide++){
        const unsigned top=wide?HFT_PAGES:HFT_SMALL_TOP;
        for(;;){
            unsigned free_entry=HFT_ENTRIES,run=0,at=HFT_PAGES;
            for(unsigned k=0;k<HFT_ENTRIES;k++){const unsigned i=(c->entry_hint+k)&(HFT_ENTRIES-1);if(!c->entries[i].valid){free_entry=i;break;}}
            if((wide?c->free_low+c->free_high:c->free_low)>=need){
                if(big){for(unsigned i=HFT_PAGES;i--;){run=c->pages[i]?0:run+1;if(run==need){at=i;break;}}}
                else for(unsigned i=0;i<top;i++){run=c->pages[i]?0:run+1;if(run==need){at=i+1-need;break;}}
            }
            if(at<HFT_PAGES&&free_entry<HFT_ENTRIES){place(c,free_entry,key,bytes,at,need);*address=at*HFT_PAGE_BYTES;*upload=1;return 1;}
            unsigned victim=HFT_ENTRIES;
            /* Codex: поток временных CPU-снимков сначала вытесняет старые CPU-снимки.
             * В бою 841 КиБ атласов помещались в RAM_G, но повторные полосы по 7104 байта
             * вытесняли редкие фазы анимации. Закреплённые два кадра по-прежнему неприкосновенны. */
            for(unsigned pass=(key>>60)==3?0:1;pass<2&&victim==HFT_ENTRIES;++pass)
            for(unsigned k=0;k<HFT_ENTRIES;k++){const unsigned i=(c->clock+k)&(HFT_ENTRIES-1);const hft_entry*v=c->entries+i;
                if(!v->valid||v->pins||v->page>=top)continue;
                if(!pass&&(v->key>>60)!=3)continue;
                if(c->age-v->age>HFT_ENTRIES){victim=i;break;}
                if(victim==HFT_ENTRIES||v->age<c->entries[victim].age)victim=i;}
            if(victim==HFT_ENTRIES)break;
            c->clock=(victim+1)&(HFT_ENTRIES-1);
            const unsigned vp=c->entries[victim].page,vn=c->entries[victim].pages;erase(c,victim);
            if(!big&&vn>=need&&vp+need<=top){place(c,victim,key,bytes,vp,need);*address=vp*HFT_PAGE_BYTES;*upload=1;return 1;}
        }
    }
    return 0;
}
