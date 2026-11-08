/* Codex: постоянные дескрипторы Sprite сохраняют ссылки игры; пиксели подгружаются по кадру.
 * Кэш освобождает только незакреплённые пиксели, адрес самого Sprite никогда не меняется.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_resources.h"
#include "hmm_platform.h"
#include "hmm_loading.h"
#include "agg_image.h"
#include "image.h"
#include "icn.h"
#include "til.h"
#include <map>
#include <memory>
#include <new>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef HMM2_HOST
#include <cstdlib>
#else
#include "hmm_core.h"
#endif
#include <zlib.h>
// Codex: подготовка CPU-фаз войска при первом реальном чтении его пикселей.
extern void hmm_warm_battle_group(unsigned,bool);
extern void hmm_warm_battle_pending();
namespace {
constexpr size_t limit=128*1024,page_size=256;
// Клод: silhouette — отпечаток маски 0/1 (pack_stage1_ft.py, версия FTG 3); 0 — у кадра тени или один слой.
struct Record{uint32_t offset,compressed;int32_t w,h,x,y;uint32_t single;HmmFtImage ft[2];HmmOpaqueRect opaque[4];uint32_t silhouette[2];};
struct Group{uint32_t offset,count;};
static_assert(sizeof(Record)==100&&sizeof(Group)==8&&sizeof(HmmFtImage)==16);
static hmm_file *file,*legacy;
static uint32_t file_base;static bool raw_pixels;
static uint8_t palette_map[256];
static Group groups[ICN::LASTICN+TIL::LASTTIL*4];
static uint32_t depth,epoch=1,age;
static size_t live,peak,menu_live;
// Codex: декодированные фоны нужны меню; перед миром весь этот кэш отдаётся.
static bool menu_decode_enabled;
constexpr size_t menu_limit=1280*1024,menu_pages=~size_t(0);
// Codex: town_depth считает вложенные OpenDialog. Город использует уже назначенную
// область DAT; ни вход, ни выход из окна не меняют её размер и содержимое.
static unsigned town_depth;
/* Codex: отдельная арена выделяется до загрузки мира. Пиксельные блоки не дробят игровую кучу. */
static std::unique_ptr<uint8_t[]> arena;
constexpr size_t extra_limit=256*1024,base_pages=limit/page_size;
static uint8_t *extra,*cache_region,*world_arena;
static size_t scratch_end;
constexpr size_t sprite_pages=(limit+extra_limit)/page_size,background_limit=240*1024;
static uint8_t *background;
static unsigned background_bytes,background_generation;
static bool background_migrate;
extern "C" void *hmm_audio_idle_region(unsigned*,unsigned*);
// Codex: zlib просит состояние до 8 КиБ и окно ровно 32 КиБ. В длинной трассе
// запрос окна 32768 упал при 124636 свободных. После плана мира оба блока постоянные.
constexpr unsigned decode_window=32768,decode_bytes=decode_window+7168;
static uint8_t *decode_region;
voidpf decode_alloc(voidpf,uInt count,uInt width){
    const size_t n=size_t(count)*width;
    if(!decode_region)return std::calloc(count,width);
    if(n==decode_window)return decode_region;
    return n<=decode_bytes-decode_window?decode_region+decode_window:nullptr;
}
void decode_free(voidpf,voidpf p){if(!decode_region||uintptr_t(p)<uintptr_t(decode_region)||uintptr_t(p)>=uintptr_t(decode_region)+decode_bytes)std::free(p);}
// Codex: сохранение не рисует кадры. Его zlib временно использует ту же область;
// окно сжатия 4 КиБ и memLevel=5 требуют меньше 40 КиБ, формат SAV остаётся zlib.
static unsigned save_used;
voidpf save_alloc(voidpf,uInt count,uInt width){
    const size_t n=size_t(count)*width,aligned=(n+7)&~size_t(7);
    if(aligned>decode_bytes-save_used)return nullptr;
    auto *p=decode_region+save_used;save_used+=aligned;std::memset(p,0,n);return p;
}
extern "C" void hmm_save_plan(z_stream *z){if(decode_region){save_used=0;z->zalloc=save_alloc;z->zfree=decode_free;}}
static uint8_t used[sprite_pages+background_limit/page_size];
class StoredSprite;
// Клод: все загруженные спрайты — хеш-таблица по ключу (группа<<16|номер), открытая адресация.
// Прежде — std::map: около 3000 поисков на кадр карты, каждый — десяток узлов дерева в PSRAM.
// Спрайты не удаляются никогда (адрес Sprite хранит игра), поэтому таблица только растёт.
static StoredSprite **table;
static unsigned table_bits,table_count;
// Клод: кадры, распакованные во временный буфер (spill) внутри текущей операции. hmm_cache_leave
// отдаёт только их; прежде он на выходе из каждой операции с изображением (около 420 за кадр карты)
// обходил все загруженные спрайты — сотни узлов std::map в PSRAM, почти каждый — промах кэша.
static std::vector<const StoredSprite*> spilled;
static fheroes2::Sprite empty;
void read(uint32_t offset,void *p,size_t n){if(hmm_fseek(file,long(offset),0)||hmm_fread(p,1,n,file)!=n)throw std::runtime_error("HMM2.GFX: short read");}
void open(){if(file)return;file=hmm_fopen("hmm2.ftg","rb");if(!file)throw std::runtime_error("HMM2.FTG: not found");
    file_base=hmm_file_offset(file);
    uint32_t h[4];read(0,h,sizeof(h));raw_pixels=h[1]==4;if(h[0]!=0x35475448||(h[1]!=3&&h[1]!=4)||h[2]!=ICN::LASTICN||h[3]!=TIL::LASTTIL)throw std::runtime_error("HMM2.FTG: incompatible version");
    read(16,palette_map,sizeof(palette_map));read(272,groups,sizeof(groups));
}
void read_legacy(uint32_t offset,void *p,size_t n){
    if(!legacy)legacy=hmm_fopen("hmm2.gfx","rb");
    if(!legacy||hmm_fseek(legacy,long(offset),0)||hmm_fread(p,1,n,legacy)!=n)throw std::runtime_error("HMM2.GFX: short read");
}
class StoredSprite:public fheroes2::Sprite {
public:
    uint32_t id;
    uint32_t _hmmResourceKey()const override{return id;}
    Record rec;
    mutable uint8_t *pixels=nullptr,*mask=nullptr;
    mutable size_t first=0,pages=0;
    mutable uint32_t stamp=0,pin=0;
    // Codex: сжатый цикл фонаря меньше декодированных кадров; SD читается один раз.
    mutable std::unique_ptr<uint8_t[]> compressed;
    mutable std::unique_ptr<uint8_t[]> spill;
    explicit StoredSprite(const Record&r,uint32_t key):id(key),rec(r){_hmmSetDimensions(r.w,r.h,r.single!=0);setPosition(r.x,r.y);}
    size_t bytes()const{return size_t(rec.w)*rec.h*(rec.single?1:2);}
    uint8_t *image()override{return const_cast<uint8_t*>(static_cast<const StoredSprite*>(this)->image());}
    const uint8_t *image()const override;
    void decode(uint8_t*,int,int,int,int)const;
    bool cache_menu()const;
    uint8_t *transform()override{image();return mask;}
    const uint8_t *transform()const override{image();return mask;}
    void release()const{if(!pixels)return;if(pages==menu_pages)menu_live-=bytes();else std::memset(used+first,0,pages);live-=bytes();pixels=mask=nullptr;pages=0;spill.reset();}
};
template<class F> static void each_sprite(F f){if(table)for(unsigned i=0;i<(1u<<table_bits);++i)if(table[i])f(table[i]);}
// Клод: первый свободный отрезок need страниц в одной арене (отрезок не переходит из основной в добавку); иначе ~0.
static size_t free_run(size_t need,bool bg=false){
    const bool borrowed=bg&&background;
    const size_t end=borrowed?sprite_pages+background_bytes/page_size:scratch_end?scratch_end:extra?sprite_pages:base_pages;size_t run=0;
    for(size_t scan=borrowed?sprite_pages:0;scan<end;scan++){if(scan==base_pages&&!world_arena)run=0;run=used[scan]?0:run+1;if(run==need)return scan+1-need;}
    return ~size_t(0);
}
static uint8_t *page_address(size_t page){return page>=sprite_pages?background+(page-sprite_pages)*page_size:world_arena?world_arena+page*page_size:arena.get()+page*page_size;}
// Codex: большой фон читается участками. В длинном бою один CBKG давал 84 МБ
// повторных чтений: каждый маленький участок заново распаковывал весь фон.
// Codex: в бою участки занимают свободную область петель; при активных петлях — арену спрайтов.
struct Region {uint32_t id=0,stamp=0;int x=0,y=0,w=0,h=0;size_t first=0,pages=0;bool busy=false;};
static Region regions[16];
static void release_region(Region &r){std::memset(used+r.first,0,r.pages);r.pages=0;r.stamp=0;}
// Codex: свободные страницы внутри постоянной арены тоже могут раздробиться.
// Перед вытеснением сдвигаем незакреплённые блоки, сохраняя указатели текущей операции.
static void compact_cached_pages(bool bg){
    const bool borrowed=bg&&background;
    const size_t begin=borrowed?sprite_pages:0,end=borrowed?sprite_pages+background_bytes/page_size:scratch_end?scratch_end:extra?sprite_pages:base_pages;
    size_t scan=begin,dest=begin;
    while(scan<end){
        StoredSprite *sprite=nullptr;Region *region=nullptr;size_t first=end,pages=0;
        each_sprite([&](StoredSprite *p){if(p->pixels&&p->pages&&p->pages!=menu_pages&&p->first>=scan&&p->first<first){sprite=p;first=p->first;pages=p->pages;}});
        for(auto &r:regions)if(r.pages&&r.first>=scan&&r.first<first){sprite=nullptr;region=&r;first=r.first;pages=r.pages;}
        if(!pages)break;
        if((sprite&&sprite->pin==epoch)||(region&&region->busy))dest=first;
        if(!world_arena&&first>=base_pages&&dest<base_pages)dest=base_pages;
        if(dest<first){
            auto *data=page_address(dest);std::memmove(data,page_address(first),pages*page_size);
            std::memset(used+first,0,pages);std::memset(used+dest,1,pages);
            if(sprite){if(sprite->mask)sprite->mask=data+(sprite->mask-sprite->pixels);sprite->pixels=data;sprite->first=dest;}
            else region->first=dest;
        }
        scan=first+pages;dest+=pages;
    }
}
static size_t cache_run(size_t need,bool bg=false){
    size_t at=free_run(need,bg);if(at!=~size_t(0))return at;
    const bool borrowed=bg&&background;
    const size_t begin=borrowed?sprite_pages:0,end=borrowed?sprite_pages+background_bytes/page_size:scratch_end?scratch_end:extra?sprite_pages:base_pages;
    size_t free=0;for(size_t i=begin;i<end;++i)free+=!used[i];
    if(free>=need){compact_cached_pages(bg);at=free_run(need,bg);}
    return at;
}
static bool refresh_background(){
    unsigned capacity,generation;auto *idle=static_cast<uint8_t*>(hmm_audio_idle_region(&capacity,&generation));
    if(idle!=background||generation!=background_generation){
        each_sprite([](StoredSprite *p){if(p->pages&&p->pages!=menu_pages&&p->first>=sprite_pages)p->release();});
        for(auto &r:regions)if(r.pages&&r.first>=sprite_pages)release_region(r);
        background=idle;background_bytes=std::min<unsigned>(capacity,background_limit);background_generation=generation;
        background_migrate=background!=nullptr;
        // Codex: петли карты могут закончиться уже после первых кадров боя.
        // Переносим готовые фоны в освободившийся банк, прежде чем готовить спрайты.
    }
    // Codex: занятая область переносится при следующем обращении после decode,
    // даже если поколение банка больше не менялось. Иначе она навсегда теснит спрайты.
    if(background_migrate){background_migrate=false;
        for(auto &r:regions)if(r.pages&&r.first<sprite_pages){
            if(r.busy){background_migrate=true;continue;}
            const size_t at=free_run(r.pages,true);
            if(at==~size_t(0)){release_region(r);continue;}
            std::memcpy(page_address(at),page_address(r.first),r.pages*page_size);
            std::memset(used+r.first,0,r.pages);std::memset(used+at,1,r.pages);r.first=at;
        }
    }
    return background!=nullptr;
}
static bool evict_sprite(bool bg=false){
    const bool borrowed=bg&&background;
    StoredSprite *victim=nullptr;
    each_sprite([&](StoredSprite *p){if(p->pixels&&p->pages&&p->pages!=menu_pages&&p->pin!=epoch&&(!bg||(p->first>=sprite_pages)==borrowed)&&(!victim||p->stamp<victim->stamp))victim=p;});
    Region *region=nullptr;for(auto &r:regions)if(r.pages&&!r.busy&&(!bg||(r.first>=sprite_pages)==borrowed)&&(!region||r.stamp<region->stamp))region=&r;
    if(region&&(!victim||region->stamp<victim->stamp)){release_region(*region);return true;}
    if(victim){victim->release();return true;}return false;
}
// Codex: общий LRU двух постоянных банков вытесняет старые окна раньше фаз боя.
// Новый звук отзывает и фоны, и спрайты; доступ к заёмным пикселям проверяет поколение.
static size_t sprite_run(size_t need){
    do{
        size_t at=cache_run(need);if(at==~size_t(0)&&background)at=cache_run(need,true);
        if(at!=~size_t(0))return at;
    }while(evict_sprite());
    return ~size_t(0);
}
// Codex: полный CPU-снимок 307200 байт не ищет новый кусок раздробленной кучи.
// На время снимка занимает хвост непрерывной арены спрайтов; закреплённые данные
// не вытесняются. После free вся прежняя ёмкость кэша снова доступна без malloc.
extern "C" void *hmm_scratch_take(size_t n){
    if(!world_arena||scratch_end||n>limit+extra_limit-64*1024)return nullptr;
    const size_t keep=(limit+extra_limit-n)/page_size;
    bool pinned=false;
    each_sprite([&](StoredSprite *p){if(p->pixels&&p->pages&&p->pages!=menu_pages&&p->first<sprite_pages&&p->first+p->pages>keep&&depth&&p->pin==epoch)pinned=true;});
    for(const auto &r:regions)if(r.pages&&r.busy&&r.first<sprite_pages&&r.first+r.pages>keep)pinned=true;
    if(pinned)return nullptr;
    each_sprite([&](StoredSprite *p){if(p->pixels&&p->pages&&p->pages!=menu_pages&&p->first<sprite_pages&&p->first+p->pages>keep)p->release();});
    for(auto &r:regions)if(r.pages&&r.first<sprite_pages&&r.first+r.pages>keep)release_region(r);
    scratch_end=keep;return world_arena+keep*page_size;
}
extern "C" void hmm_scratch_give(){scratch_end=0;}
static void *extra_alloc(size_t n){
#ifdef HMM2_HOST
    return std::malloc(n);
#else
    return hmm_core->alloc(n,ZM_MEM_PSRAM);
#endif
}
static void extra_free(void *p){
#ifdef HMM2_HOST
    std::free(p);
#else
    hmm_core->free(p);
#endif
}
static unsigned slot_of(uint32_t key){return (key*2654435761u)>>(32-table_bits);}
static StoredSprite *find_sprite(uint32_t key){
    if(!table)return nullptr;const unsigned mask=(1u<<table_bits)-1;
    for(unsigned i=slot_of(key);table[i];i=(i+1)&mask)if(table[i]->id==key)return table[i];
    return nullptr;
}
static void add_sprite(StoredSprite *sprite){
    // Заполнение не выше половины; при росте все записи раскладываются заново.
    if(!table||(table_count+1)*2>(1u<<table_bits)){
        StoredSprite **old=table;const unsigned old_size=table?1u<<table_bits:0;
        table_bits=table?table_bits+1:10;table=new StoredSprite*[1u<<table_bits]();table_count=0;
        for(unsigned i=0;i<old_size;++i)if(old[i])add_sprite(old[i]);
        delete[] old;
    }
    const unsigned mask=(1u<<table_bits)-1;unsigned i=slot_of(sprite->id);while(table[i])i=(i+1)&mask;
    table[i]=sprite;++table_count;
}
bool StoredSprite::cache_menu()const{
    if(pixels)return true;
    if(!menu_decode_enabled||bytes()>menu_limit)return false;
    const unsigned group=id>>16;
    if(group!=ICN::HEROES&&group!=ICN::STONEBAK&&group!=ICN::STONEBAK_EVIL&&group!=ICN::SURDRBKG&&group!=ICN::WINLOSE)return false;
    if(!depth)++epoch;pin=epoch;stamp=++age;
    while(menu_live+bytes()>menu_limit){
        StoredSprite *victim=nullptr;
        each_sprite([&](StoredSprite *p){if(p->pages==menu_pages&&p->pin!=epoch&&(!victim||p->stamp<victim->stamp))victim=p;});
        if(!victim)return false;victim->release();
    }
    std::unique_ptr<uint8_t[]> decoded(new(std::nothrow) uint8_t[bytes()]);
    if(!decoded){menu_decode_enabled=false;return false;}
    decode(decoded.get(),0,0,rec.w,rec.h);
    spill=std::move(decoded);pixels=spill.get();mask=rec.single?nullptr:pixels+size_t(rec.w)*rec.h;pages=menu_pages;
    menu_live+=bytes();live+=bytes();if(live>peak)peak=live;return true;
}
const uint8_t *StoredSprite::image()const{
    if(!rec.w||!rec.h)return nullptr;
    if(pixels&&pages&&pages!=menu_pages&&first>=sprite_pages)refresh_background();
    if(!depth)++epoch;pin=epoch;stamp=++age;
    if(!pixels&&refresh_background()&&world_arena&&!scratch_end)hmm_warm_battle_group(id>>16,(rec.ft[0].flags&2)!=0);
    if(!pixels&&menu_decode_enabled)cache_menu();
    if(!pixels){
        // Codex: арена нужна только для операций, требующих точных CPU-пикселей.
        // Codex: внутри загруженного мира арена не освобождается и не переезжает:
        // повторное выделение дробило PSRAM перед окном героя GateHell. На границе
        // миров hmm_release_for_world освобождает её после всех владельцев пикселей.
        if(!arena&&!world_arena)arena.reset(new uint8_t[limit]);
        const size_t bytes= this->bytes();

        const size_t need=(bytes+page_size-1)/page_size;
        if(bytes>limit){spill.reset(new uint8_t[bytes]);pages=0;spilled.push_back(this);}
        else{
            const size_t at=sprite_run(need);
            if(at!=~size_t(0)){first=at;pages=need;std::memset(used+first,1,pages);}
            else{
#ifdef HMM2_HOST
                std::printf("PINNED request=%u bytes=%zu epoch=%u depth=%u\n",id,bytes,epoch,depth);
                each_sprite([](const StoredSprite *p){if(p->pixels)std::printf("PINNED id=%u w=%d h=%d bytes=%zu pin=%u\n",p->id,p->rec.w,p->rec.h,p->bytes(),p->pin);});
#endif
                // Codex: закреплённый кадр может разделить арену на два коротких
                // свободных отрезка. Временный буфер живёт только до конца примитива.
                spill.reset(new uint8_t[bytes]);pages=0;spilled.push_back(this);
            }
        }
        const size_t area=size_t(rec.w)*rec.h;
        uint8_t *decoded=spill?spill.get():page_address(first),*transforms=rec.single?nullptr:decoded+area;
        decode(decoded,0,0,rec.w,rec.h);
        pixels=decoded;mask=transforms;
        live+=bytes;if(live>peak)peak=live;
    }
    return pixels;
}
// Codex: распаковка исходных плоскостей zlib; в PSRAM остаются только запрошенные строки.
// Клод: hmm_decode_bytes (hmm_video.c, HMM2 PERF dec_B) — байты кадров, распакованных для процессора: при вытеснении арены
// они читаются снова.
/* Клод: полоса строк широкого спрайта (несжатый GFX) — одна на всё: клетки 32×32 при запекании экрана (окно поверх плотной
 * карты переполняет список FT812) вырезали из рамки карты ADVBORD 640×480 и фона окна STONEBAK каждая свой кусочек, читая
 * целые строки обеих плоскостей — 32 строки × 640 × 2 ≈ 40 КБ ради 1 КБ клетки, двадцать раз на ряд клеток (стенд, DAT карты:
 * 6 МБ с SD на открытие окна загрузки, из них ADVBORD 2,5 МБ). Полоса до 64 КиБ запоминается; клетки того же ряда берут её
 * из памяти. Высокие узкие полосы (узор рамки окна) в неё не входят и читаются как прежде. */
static struct {uint32_t id=~0u,offset=0;int y=0,h=0;std::unique_ptr<uint8_t[]> data;} band;
constexpr size_t BAND_BYTES=64*1024;
static bool decode_band(const StoredSprite &s,uint8_t *dst,int x,int y,int w,int h){
    const unsigned planes=s.rec.single?1u:2u;const size_t row=size_t(s.rec.w)*planes;
    if(row*size_t(h)>BAND_BYTES)return false;
    if(band.id!=s.id||band.offset!=s.rec.offset||y<band.y||y+h>band.y+band.h){
        if(!band.data){band.data.reset(new(std::nothrow) uint8_t[BAND_BYTES]);if(!band.data)return false;}
        // Codex: сохраняем уточнение основной 91: полоса читает только запрошенные строки.
        // Запас вниз не покрывал следующий ряд клеток и увеличивал чтение ADVBORD.
        const int rows=h;
        band.id=~0u;
        for(unsigned plane=0;plane<planes;++plane)read_legacy(s.rec.offset+plane*s.rec.w*s.rec.h+y*s.rec.w,band.data.get()+plane*size_t(rows)*s.rec.w,size_t(rows)*s.rec.w);
        band.id=s.id;band.offset=s.rec.offset;band.y=y;band.h=rows;
    }
    for(unsigned plane=0;plane<planes;++plane)for(int k=0;k<h;++k)
        std::memcpy(dst+(plane*h+k)*w,band.data.get()+(plane*size_t(band.h)+(y-band.y)+k)*s.rec.w+x,w);
    return true;
}
void StoredSprite::decode(uint8_t *dst,int x,int y,int w,int h)const{
    hmm_decode_bytes+=uint32_t(w)*uint32_t(h)*(rec.single?1u:2u);
#ifdef HMM2_HOST
    if(std::getenv("HMM2_DECODE_TRACE"))std::printf("DECODE group=%u frame=%u bytes=%zu roi=%d,%d,%d,%d\n",id>>16,id&65535,bytes(),x,y,w,h);
#endif
    if(raw_pixels){
        // Codex: исходные плоскости адресуются непосредственно; кадры выровнены по 512 байтам.
        if(rec.compressed!=bytes())throw std::runtime_error("HMM2.GFX: raw size");
        if(!x&&!y&&w==rec.w&&h==rec.h)read_legacy(rec.offset,dst,bytes());
        else if(decode_band(*this,dst,x,y,w,h)){}
        else{
            // Клод: участок читается целыми строками блоками до 4 КиБ, нужные столбцы вырезаются. Узор края окна
            // (CreateDitheringTransition) и растеризация под ним брали полосы шириной в десяток точек: каждая строка была
            // отдельным чтением файла, сотни вызовов на полосу. Байты те же; с карты всё равно читаются окна по 2 КиБ.
            static uint8_t rows[4096];const int per=std::max<int>(1,int(sizeof(rows))/int(rec.w));
            for(unsigned plane=0;plane<(rec.single?1u:2u);++plane)for(int row=0;row<h;row+=per){
                const int n=std::min(per,h-row);const uint32_t at=rec.offset+plane*rec.w*rec.h+(y+row)*rec.w;
                if(n>1){read_legacy(at,rows,size_t(n)*rec.w);for(int k=0;k<n;++k)std::memcpy(dst+(plane*h+row+k)*w,rows+k*rec.w+x,w);}
                else read_legacy(at+x,dst+(plane*h+row)*w,w);
                if(!((row/per)&3))hmm_loading_tick();
            }
        }
        return;
    }
    z_stream z{};z.zalloc=decode_alloc;z.zfree=decode_free;if(inflateInit(&z)!=Z_OK)throw std::runtime_error("HMM2.GFX: inflate init");
    if((id>>16)==ICN::SHNGANIM&&!compressed){compressed.reset(new uint8_t[rec.compressed]);read_legacy(rec.offset,compressed.get(),rec.compressed);}
    static uint8_t input[1024],output[1024];uint32_t at=rec.offset,left=rec.compressed;int result=Z_OK;
    const unsigned area=unsigned(rec.w)*rec.h;const bool full=!x&&!y&&w==rec.w&&h==rec.h;
    while(result==Z_OK&&left){unsigned n=std::min<unsigned>(left,sizeof(input));if(compressed)std::memcpy(input,compressed.get()+at-rec.offset,n);else read_legacy(at,input,n);at+=n;left-=n;z.next_in=input;z.avail_in=n;
        while(z.avail_in&&result==Z_OK){unsigned before=z.total_out;z.next_out=output;z.avail_out=sizeof(output);result=inflate(&z,Z_NO_FLUSH);unsigned produced=z.total_out-before;
            if(full){if(before+produced<=bytes())std::memcpy(dst+before,output,produced);}
            else for(unsigned copied=0;copied<produced;){unsigned offset=before+copied,plane=offset/area,point=offset%area,row=point/rec.w,col=point%rec.w,span=std::min<unsigned>(produced-copied,rec.w-col);
                int lo=std::max<int>(col,x),hi=std::min<int>(col+span,x+w);
                if(row>=unsigned(y)&&row<unsigned(y+h)&&hi>lo&&plane<(rec.single?1u:2u))std::memcpy(dst+plane*w*h+(row-y)*w+lo-x,output+copied+lo-col,hi-lo);
                copied+=span;
            }
            // Codex: длинная CPU-распаковка не должна осушать PCM, даже когда DAT уже в кэше.
            if((z.total_out&8191u)<produced)hmm_loading_tick();
        }
    }
    bool okay=result==Z_STREAM_END&&z.total_out==bytes()&&z.total_in==rec.compressed;inflateEnd(&z);if(!okay)throw std::runtime_error("HMM2.GFX: damaged frame");
}
const fheroes2::Sprite &get(unsigned group,uint32_t index){
    open();if(group>=sizeof(groups)/sizeof(*groups)||index>=groups[group].count)return empty;
    if(index>65535)throw std::runtime_error("HMM2.GFX: invalid index");uint32_t key=(group<<16)|index;
    if(auto *found=find_sprite(key))return *found;
    Record r;read(groups[group].offset+index*sizeof(r),&r,sizeof(r));
    for(auto &ft:r.ft)if(ft.bytes)ft.offset+=file_base;
    if(r.w<0||r.h<0||r.w>2048||r.h>2048||r.single>1)throw std::runtime_error("HMM2.GFX: invalid dimensions");
    auto *image=new StoredSprite(r,key);add_sprite(image);return *image;
}
}
void hmm_cache_enter(){if(!depth++)++epoch;}
void hmm_cache_leave(){if(depth&&!--depth){for(auto *p:spilled)if(p->spill)p->release();spilled.clear();if(world_arena&&!scratch_end)hmm_warm_battle_pending();}}
/* Codex: при давлении на кучу освобождаем только пиксели вне текущей операции; ссылки Sprite сохраняются. */
extern "C" void hmm_gpu_history_reset();
extern "C" void hmm_gpu_trim_unused();
extern "C" void hmm_files_trim();
extern "C" void hmm_files_menu_cache(int);
extern "C" void hmm_gpu_memory(unsigned*,unsigned*,unsigned*,unsigned*);
extern "C" void hmm_gpu_memory_quiet(unsigned*,unsigned*,unsigned*,unsigned*);
extern "C" unsigned hmm_files_cached();
extern "C" void hmm_gpu_release_pixels();
extern "C" void hmm_audio_drop_loops();
// Клод: кто держит PSRAM (слои, снимки, буферы изображений, арена спрайтов, кэш DAT) —
// по этой строке журнала находится причина нехватки, а не только место отказа.
static void report_memory(const char *when){
    unsigned lists,draws,bitmaps,pixels;hmm_gpu_memory(&lists,&draws,&bitmaps,&pixels);
    size_t sprite_live=0,packed=0;each_sprite([&](const StoredSprite *p){if(p->pixels||p->spill)sprite_live+=p->bytes();if(p->compressed)packed+=p->rec.compressed;});
    char message[256];std::snprintf(message,sizeof(message),"HMM2 MEM %s: lists=%u draws=%u bitmaps=%u image_pixels=%u arena=%u+%u sprites=%u/%u packed=%u dat=%u",
        when,lists,draws,bitmaps,pixels,(arena||world_arena)?unsigned(limit):0u,extra?unsigned(extra_limit):0u,table_count,unsigned(sprite_live),unsigned(packed),hmm_files_cached());
    hmm_log(message);
}
/* Клод: короткая сводка памяти раз в 100 кадров (HMM2 MEMF, hmm_video.c), КиБ: мелкие объекты, крупные блоки (сейчас/пик/число),
 * списки слоёв, снимки CPU, пиксели изображений, арена спрайтов, распакованные кадры, кэш HMM2.DAT. Консоль режет строку на 136 знаках. */
extern "C" size_t hmm_heap_big_bytes,hmm_heap_big_peak,hmm_heap_big_count,hmm_heap_small_bytes;
extern "C" void hmm_memory_line(unsigned frame){
    unsigned lists,draws,bitmaps,pixels;hmm_gpu_memory_quiet(&lists,&draws,&bitmaps,&pixels);
    size_t sprite_live=0;each_sprite([&](const StoredSprite *p){if(p->pixels||p->spill)sprite_live+=p->bytes();});
    // Codex: видно, остался ли фон в арене спрайтов после освобождения звукового банка.
    unsigned bg0=0,bg1=0;for(const auto &r:regions)(r.first<sprite_pages?bg0:bg1)+=unsigned(r.pages*page_size);
    char m[136];std::snprintf(m,sizeof(m),"HMM2 MEMF f=%u sm=%u big=%u/%u/%u ls=%u dr=%u bm=%u px=%u ar=%u sp=%u dat=%u bg=%u/%u",frame,unsigned(hmm_heap_small_bytes>>10),
        unsigned(hmm_heap_big_bytes>>10),unsigned(hmm_heap_big_peak>>10),unsigned(hmm_heap_big_count),lists,draws>>10,bitmaps>>10,pixels>>10,
        unsigned((((arena||world_arena)?limit:0)+(extra?extra_limit:0))>>10),unsigned(sprite_live>>10),hmm_files_cached()>>10,bg0>>10,bg1>>10);
    hmm_log(m);hmm_heap_big_peak=hmm_heap_big_bytes;
}
extern "C" void hmm_cache_trim(){
    hmm_monster_parts_clear();
    static unsigned reports;
    if(reports<4){++reports;report_memory("trim");}
    hmm_gpu_history_reset();
    hmm_gpu_trim_unused();
    each_sprite([](StoredSprite *p){if(p->pin!=epoch){if(p->pixels&&p->spill)p->release();p->compressed.reset();}});

}
/* Клод: перед чтением мира из сохранения отдаём кэши заранее, пока мир ещё не занял PSRAM:
 * кэш HMM2.DAT, распакованные кадры вне текущей операции и буферы пикселей изображений
 * со списком слоёв. Арена остаётся на месте. Отданные во время загрузки по нехватке, те же
 * 1,5 МиБ кэша уже не помогали: мир GateHell к тому времени раздробил кучу. */
void hmm_resource_menu_cache(bool enable){
    if(enable)open();
    hmm_files_menu_cache(enable?(raw_pixels?2:1):0);
    menu_decode_enabled=enable;
    if(!enable)each_sprite([](StoredSprite *p){if(p->pages==menu_pages)p->release();});
}
// Codex: вложенные окна меняют только режим чтения; область DAT остаётся у мира.
extern "C" void hmm_files_town_cache(int);
void hmm_resource_town_cache(bool enable){
    if(enable){if(!town_depth++)hmm_files_town_cache(1);return;}
    if(town_depth&&!--town_depth)hmm_files_town_cache(0);
}
extern "C" void hmm_files_plan(void*,unsigned);
extern "C" void hmm_audio_plan(void*,unsigned);
extern "C" void hmm_heap_plan();
// Codex: один план после загрузки мира. Измеренный пик петель 236894 байта;
// рабочие спрайты Alteris занимают до 345458 байт вместе с основной ареной.
// Из первоначальных 128 КиБ DAT 40 КиБ отданы zlib, 88 КиБ — средним блокам кучи.
// На тесном мире остаются база
// спрайтов 128 КиБ и база DAT 32 КиБ. Области живут до следующей загрузки мира.
extern "C" void hmm_audio_pool(int);
static bool world_fresh;
bool hmm_world_take_fresh(){const bool fresh=world_fresh;world_fresh=false;return fresh;}
static bool plan_pending=true;
extern "C" void hmm_memory_plan(){
    if(!plan_pending)return;plan_pending=false;
#ifndef HMM2_HOST
    hmm_heap_plan();
#endif
    constexpr unsigned loops=240*1024,full=limit+extra_limit+loops+decode_bytes;
    unsigned bytes=full,loop_bytes=loops;
    void *probe=extra_alloc(full+384*1024);
    // Codex: GateHell с 256 КиБ петель исчерпал бюджет (19640 при 18160 свободных).
    // Codex: тесный мир сохраняет общий бюджет 128 КиБ: 40 КиБ распаковщика
    // и отдельные 88 КиБ средних блоков. Петли читаются штатными окнами DAT.
    if(probe)extra_free(probe);else {bytes=decode_bytes;loop_bytes=0;}
    cache_region=static_cast<uint8_t*>(extra_alloc(bytes));
    if(cache_region){
        hmm_audio_plan(cache_region,loop_bytes);
        decode_region=cache_region+bytes-decode_bytes;
        if(bytes==full){
            each_sprite([](StoredSprite *p){if(p->pixels)p->release();});
            for(auto &r:regions)release_region(r);
            arena.reset();world_arena=cache_region+loops;extra=world_arena+limit;
        }
    }
    hmm_audio_pool(1);
    report_memory("plan");
}
extern "C" void hmm_cache_last_resort(){report_memory("last");}
void hmm_release_for_world(){
    plan_pending=true;world_fresh=true;hmm_audio_pool(0);
    hmm_gpu_release_pixels();
    hmm_audio_plan(nullptr,0);hmm_files_plan(nullptr,0);
    hmm_resource_menu_cache(false);
    each_sprite([](StoredSprite *p){if(p->pixels)p->release();p->compressed.reset();});
    for(auto &r:regions)release_region(r);
    // Codex: на границе мира все владельцы пикселей уже освобождены. Пустые
    // 128 КиБ нужны загрузчику регионов GateHell; после загрузки новый план
    // выделяет свою область, а тесный мир получает арену при первом чтении.
    arena.reset();
    extra=decode_region=world_arena=nullptr;scratch_end=0;extra_free(cache_region);cache_region=nullptr;
    report_memory("load");
}
unsigned hmm_resource_flags(const fheroes2::Image &im){return static_cast<const StoredSprite&>(im).rec.ft[0].flags;}
bool hmm_resource_id(const fheroes2::Image&im,uint32_t&key){key=im._hmmResourceKey();return key!=0;}
const fheroes2::Image &hmm_resource_get(uint32_t key){return get(key>>16,key&65535);}
// Codex: регистрация войск и GPU-атласов допустима во время временного снимка.
// CPU-подготовка ждёт возврата снимка отдельно в StoredSprite::image.
bool hmm_resource_warm_allowed(){return world_arena!=nullptr;}
bool hmm_resource_region(uint32_t key,int x,int y,int w,int h,fheroes2::Image &out){
    const auto &in=static_cast<const StoredSprite&>(get(key>>16,key&65535));
    // Codex: готовый фон меню читается повторно без распаковки каждой полосы окна.
    // Codex: в тесной арене 125780 байт городского слоя не помещались рядом
    // с закреплённым входом Blit. Читаем нужную область, как у больших фонов.
    if(in.pixels||in.bytes()<=(world_arena?128:64)*1024||(!raw_pixels&&in.cache_menu()))return false;
    // Codex: сырой GFX сохраняет построчный путь 91. Расширение ROI до 128 строк
    // обходило полосу 64 КиБ и читало лишние мегабайты при открытии окна на карте.
    if(raw_pixels){
        if(x<0||y<0||w<=0||h<=0||x+w>in.width()||y+h>in.height())throw std::runtime_error("HMM2.GFX: region bounds");
        if(in.singleLayer())out._disableTransformLayer();out.resize(w,h);
        in.decode(out.image(),x,y,w,h);return true;
    }
    refresh_background();
    if(x<0||y<0||w<=0||h<=0||x+w>in.width()||y+h>in.height())throw std::runtime_error("HMM2.GFX: region bounds");
    if(in.singleLayer())out._disableTransformLayer();out.resize(w,h);
    const size_t bytes=size_t(w)*h*(in.singleLayer()?1:2);
    Region *cached=nullptr;
    for(auto &r:regions)if(r.pages&&!r.busy&&r.id==key&&r.x<=x&&r.y<=y&&r.x+r.w>=x+w&&r.y+r.h>=y+h){cached=&r;break;}
#ifdef HMM2_HOST
    // Codex: замер рабочего набора областей в тяжёлом исходном бою, в ELF не входит.
    if(std::getenv("HMM2_BG_TRACE"))std::printf("BG key=%u rect=%d,%d,%d,%d bytes=%zu hit=%u\n",key,x,y,w,h,bytes,cached?1u:0u);
#endif
    if(!cached&&bytes<=limit){
        Region *r=nullptr;for(auto &v:regions)if(!v.busy&&(!r||!v.pages||v.stamp<r->stamp))r=&v;
        if(!r){in.decode(out.image(),x,y,w,h);return true;}
        release_region(*r);if(!arena&&!world_arena)arena.reset(new uint8_t[limit]);
        // Codex: полосы тени по 16 строк используют общий блок: замер тяжёлого боя
        // дал 1108 запросов и ноль попаданий у восьми точных прямоугольников.
        // Codex: замер IDLE дал охват x=64..192 и y=128..443. Шаг 128
        // включает соседние фазы в первые запросы; область ограничена банком 240 КиБ.
        const int rx=x&~127,ry=y&~127,rw=std::min<int>((x+w+127)&~127,in.width())-rx,rh=std::min<int>((y+h+127)&~127,in.height())-ry;
        const size_t cached_bytes=size_t(rw)*rh*(in.singleLayer()?1:2);
        const size_t need=(cached_bytes+page_size-1)/page_size;
        size_t at;while((at=cache_run(need,true))==~size_t(0)&&evict_sprite(true)){}
        if(at!=~size_t(0)){
            std::memset(used+at,1,need);*r={key,0,rx,ry,rw,rh,at,need};
            r->busy=true;
            try{in.decode(page_address(at),rx,ry,rw,rh);}catch(...){r->busy=false;release_region(*r);throw;}
            r->busy=false;cached=r;
        }
    }
    if(cached){cached->stamp=++age;auto *dst=out.image();const auto *src=page_address(cached->first);
        for(unsigned p=0;p<(in.singleLayer()?1u:2u);++p)for(int row=0;row<h;++row)
            std::memcpy(dst+p*w*h+row*w,src+p*cached->w*cached->h+(y-cached->y+row)*cached->w+x-cached->x,w);
    }
    else in.decode(out.image(),x,y,w,h);
    return true;
}
bool hmm_resource_ready(uint32_t key){auto *p=find_sprite(key);return p&&p->pixels!=nullptr;}
const HmmFtImage &hmm_resource_ft(uint32_t key,bool copy){return static_cast<const StoredSprite&>(get(key>>16,key&65535)).rec.ft[copy?1:0];}
const HmmOpaqueRect *hmm_resource_opaque(uint32_t key){return static_cast<const StoredSprite&>(get(key>>16,key&65535)).rec.opaque;}
uint64_t hmm_resource_silhouette(uint32_t key){const auto &r=static_cast<const StoredSprite&>(get(key>>16,key&65535)).rec;return uint64_t(r.silhouette[1])<<32|r.silhouette[0];}
const uint8_t *hmm_resource_palette(){open();return palette_map;}
uint32_t hmm_resource_pin(uint32_t key){return static_cast<const StoredSprite&>(get(key>>16,key&65535)).pin;}
void hmm_resource_restore_pin(uint32_t key,uint32_t previous){static_cast<const StoredSprite&>(get(key>>16,key&65535)).pin=previous;}
namespace fheroes2::AGG {
const Sprite &GetICN(int id,uint32_t index){return id>0&&id<ICN::LASTICN?get(unsigned(id),index):empty;}
uint32_t GetICNCount(int id){open();return id>0&&id<ICN::LASTICN?groups[id].count:0;}
const Image &GetTIL(int id,uint32_t index,uint32_t shape){return id>0&&id<TIL::LASTTIL&&shape<4?get(ICN::LASTICN+unsigned(id)*4+shape,index):empty;}
void updateLanguageDependentResources(SupportedLanguage,bool){/* Codex: пакет содержит английские ресурсы 640×480, подготовленные оригиналом. */}
}
