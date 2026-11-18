/* Russian uses stable translation pointers and a distinct immutable sprite bank.
 * Neither changing language nor releasing pixels invalidates existing owners.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_locale.h"
#include "hmm_platform.h"
#include "hmm_resources.h"
#include "hmm_loading.h"
#include "translations.h"
#include "icn.h"
extern "C" {
#include "hmm_core.h"
void hmm_gpu_busy_enter(void);
void hmm_gpu_busy_leave(void);
void hmm_gpu_upload_commands(const uint32_t*,unsigned);
}
#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
unsigned hmm_locale_io_busy;
namespace {
struct Group {uint32_t id,offset,count;};
struct Entry {uint32_t hash,offset;};
struct MapEntry {uint32_t hash,offset,bytes,checksum;};
struct MapCache {
    struct Item {uint32_t hash=0,age=0,bytes=0;char text[1024];};
    Item items[16];uint32_t age=0;
};
static hmm_file *file,*pixel_file;
static uint32_t header[16];
static uint32_t pixel_start,map_pool,map_count;
static uint32_t direct_base,direct_bytes;
static std::unique_ptr<MapEntry[]> map_entries;
static std::unique_ptr<MapCache> map_cache;
static std::unique_ptr<uint8_t[]> metadata;
static const Group *groups;
static uint8_t group_slots[ICN::LASTICN];
static const Entry *entries;
static const char *strings;
static const uint8_t *overrides;
static bool russian;
static uint32_t hash_bytes(const void *p,size_t n){uint32_t v=2166136261u;auto *s=static_cast<const uint8_t*>(p);while(n--)v=(v^*s++)*16777619u;return v;}
static bool range(uint32_t at,uint32_t n,uint32_t size){return at<=size&&n<=size-at;}
static bool space(unsigned char c){return c==' '||(c>=9&&c<=13);}
static uint32_t map_hash(const char *text){
    uint32_t value=2166136261u;bool pending=false,started=false;
    for(auto *p=reinterpret_cast<const unsigned char*>(text);*p;++p){
        if(space(*p)){if(started)pending=true;continue;}
        if(pending){value=(value^' ')*16777619u;pending=false;}
        value=(value^*p)*16777619u;started=true;
    }
    return value;
}
static const Group *group(unsigned id){
    return groups&&id<ICN::LASTICN&&group_slots[id]?groups+group_slots[id]-1:nullptr;
}
static const char *strip(const char *s){auto *p=std::strchr(s,'|');return p?p+1:s;}
static const char *lookup(const char *key,unsigned form){
    if(!russian)return nullptr;
    const uint32_t h=hash_bytes(key,std::strlen(key));
    auto *end=entries+header[6];auto *p=std::lower_bound(entries,end,h,[](const Entry &a,uint32_t b){return a.hash<b;});
    if(p==end||p->hash!=h)return nullptr;
    unsigned count=p->offset>>30;auto *text=strings+(p->offset&0x3fffffffu);
    if(form>=count)form=0;
    while(form--)text+=std::strlen(text)+1;
    return text;
}
}
extern "C" int hmm_ru_prepare(){
    if(file)return 1;
    const HmmLocaleReadGuard reading;
    struct Closer {void operator()(hmm_file *p)const{if(p)hmm_fclose(p);}};
    std::unique_ptr<hmm_file,Closer> input(hmm_fopen("HMM2.RUS","rb"));if(!input)return 0;
    uint32_t h[16];
    if(hmm_fread(h,1,sizeof(h),input.get())!=sizeof(h)||h[0]!=0x39525548||h[1]-1>4||h[2]!=ICN::LASTICN||h[3]>=HMM_RU_INFLATE
       ||!h[4]||h[4]>255||h[5]!=64||!h[6]||h[6]>4096||h[7]!=h[5]+12*h[4]||h[9]!=h[7]+8*h[6]
       ||!h[8]||h[8]>192*1024||!range(h[9],h[8],h[3])||h[11]>65536
       ||h[10]!=(((((h[9]+h[8]+3)&~3u)+(h[1]>=4?(h[11]+7)/8:0)+3)&~3u)+(h[1]==5?32u:0u))
       ||!range(h[10],100*h[11],h[3])||(h[1]==1&&h[15]))return 0;
    if(hmm_fseek(input.get(),0,2)||uint32_t(hmm_ftell(input.get()))!=h[3]||hmm_fseek(input.get(),64,0))return 0;
    const unsigned size=h[10]-64;
    std::unique_ptr<uint8_t[]> raw(new(std::nothrow) uint8_t[size]);if(!raw)return 0;
    if(hmm_fread(raw.get(),1,size,input.get())!=size||hash_bytes(raw.get(),h[1]>=4?size:12*h[4])!=h[12]
       ||hash_bytes(raw.get()+12*h[4],8*h[6]+h[8])!=h[13]||hash_bytes(hmm_resource_palette(),256)!=h[14])return 0;
    auto *g=reinterpret_cast<const Group*>(raw.get());auto *e=reinterpret_cast<const Entry*>(raw.get()+h[7]-64);
    const char *pool=reinterpret_cast<const char*>(raw.get()+h[9]-64);
    for(unsigned i=0;i<h[4];++i){
        if(!g[i].id||g[i].id>=ICN::LASTICN||(i&&g[i-1].id>=g[i].id)||!g[i].count||g[i].count>65536
           ||g[i].offset<h[10]||(g[i].offset-h[10])%100||!range(g[i].offset,100*g[i].count,h[10]+100*h[11]))return 0;
    }
    for(unsigned i=0;i<h[6];++i){
        const unsigned count=e[i].offset>>30,at=e[i].offset&0x3fffffffu;
        if((i&&e[i-1].hash>=e[i].hash)||(count!=1&&count!=3)||at>=h[8])return 0;
        const char *p=pool+at,*end=pool+h[8];
        for(unsigned j=0;j<count;++j){auto *next=static_cast<const char*>(std::memchr(p,0,end-p));if(!next||next==p)return 0;p=next+1;}
    }
    uint32_t pixels=(h[10]+100*h[11]+511)&~511u,mpool=0,mcount=0;
    std::unique_ptr<MapEntry[]> maps;
    if(h[1]>=2&&h[15]){
        uint32_t d[8];
        if(h[15]!=pixels||!range(h[15],sizeof(d),h[3])||hmm_fseek(input.get(),long(h[15]),0)
           ||hmm_fread(d,1,sizeof(d),input.get())!=sizeof(d)||d[0]!=0x50414d52||d[1]!=1||!d[2]||d[2]>8192
           ||d[3]!=h[15]+32||d[4]!=d[3]+16*d[2]||!d[5]||d[5]>4*1024*1024||!range(d[4],d[5],h[3])||d[7])return 0;
        maps.reset(new(std::nothrow) MapEntry[d[2]]);if(!maps)return 0;
        if(hmm_fread(maps.get(),16,d[2],input.get())!=d[2]||hash_bytes(maps.get(),16*d[2])!=d[6])return 0;
        uint32_t end=0;
        for(unsigned i=0;i<d[2];++i){
            const auto &v=maps[i];
            if((i&&maps[i-1].hash>=v.hash)||v.offset!=end||v.bytes<2||v.bytes>1024||!range(v.offset,v.bytes,d[5]))return 0;
            end+=v.bytes;
        }
        if(end!=d[5])return 0;
        mpool=d[4];mcount=d[2];pixels=(d[4]+d[5]+511)&~511u;
    }
    else if(h[1]==2)return 0;
    if(pixels>h[3])return 0;
    uint32_t ftbase=0,ftbytes=0;
    if(h[1]==5){
        std::unique_ptr<hmm_file,Closer> ft(hmm_fopen("hmm2.ruft","rb"));
        uint32_t d[16];
        if(!ft||hmm_fread(d,1,sizeof(d),ft.get())!=sizeof(d)||d[0]!=0x54465552u||d[1]!=1
           ||d[2]<512||d[3]!=hmm_file_offset(ft.get())||std::memcmp(d+4,raw.get()+size-32,32)
           ||d[12]!=h[14]||!d[13]||d[14]!=h[11]||d[15]
           ||hmm_fseek(ft.get(),0,2)||uint32_t(hmm_ftell(ft.get()))!=d[2]
           ||!range(d[3],d[2],hmm_package_size))return 0;
        ftbase=d[3]+512;ftbytes=d[2]-512;
    }
    std::memcpy(header,h,sizeof(h));metadata=std::move(raw);groups=g;entries=e;strings=pool;
    overrides=h[1]>=4?metadata.get()+((h[9]+h[8]+3)&~3u)-64:nullptr;
    for(unsigned i=0;i<h[4];++i)group_slots[g[i].id]=uint8_t(i+1);
    map_entries=std::move(maps);map_pool=mpool;map_count=mcount;pixel_start=pixels;
    direct_base=ftbase;direct_bytes=ftbytes;file=input.release();return 1;
}
extern "C" int hmm_ru_active(){return russian;}
extern "C" const char *hmm_ru_lookup(const char *text){return lookup(text,0);}
extern "C" const char *hmm_ru_map_lookup(const char *text){
    if(!russian||!map_count)return nullptr;
    const auto key=map_hash(text);auto *end=map_entries.get()+map_count;
    const auto *entry=std::lower_bound(map_entries.get(),end,key,[](const MapEntry &a,uint32_t b){return a.hash<b;});
    if(entry==end||entry->hash!=key)return nullptr;
    if(!map_cache)map_cache.reset(new MapCache);
    if(++map_cache->age==0){for(auto &item:map_cache->items)item.age=0;map_cache->age=1;}
    auto *slot=&map_cache->items[0];
    for(auto &item:map_cache->items){
        if(item.age&&item.hash==key){item.age=map_cache->age;return item.text;}
        if(item.age<slot->age)slot=&item;
    }
    // An interrupted or corrupt read must never turn into a cached translation.
    slot->age=0;
    hmm_ru_read(map_pool+entry->offset,slot->text,entry->bytes);
    if(slot->text[entry->bytes-1]||hash_bytes(slot->text,entry->bytes)!=entry->checksum)throw std::runtime_error("HMM2.RUS map text");
    for(const char *p=slot->text,*end=p+entry->bytes;p<end;){
        const char *next=static_cast<const char*>(std::memchr(p,0,end-p));
        if(!next||next==p)throw std::runtime_error("HMM2.RUS map answer");p=next+1;
    }
    slot->hash=key;slot->bytes=entry->bytes;slot->age=map_cache->age;return slot->text;
}
extern "C" int hmm_ru_map_answer(const char *original,const char *answer,int truncated){
    const char *text=hmm_ru_map_lookup(original);if(!text)return 0;
    auto fold=[&](const char *value){
        auto s=Translation::StringLower(value);
        for(char &c:s)if(uint8_t(c)==0xb8)c=char(0xe5); // ё/е are both valid in an answer.
        if(truncated&&s.size()>4)s.resize(4);return s;
    };
    const auto expected=fold(answer);
    for(const auto &item:map_cache->items)if(item.text==text){
        for(const char *p=text,*end=p+item.bytes;p<end;p+=std::strlen(p)+1)if(fold(p)==expected)return 1;
        break;
    }
    return 0;
}
extern "C" uint32_t hmm_ru_count(unsigned id){auto *p=group(id);return p?p->count:0;}
extern "C" int hmm_ru_frame(unsigned id,unsigned index){
    auto *p=group(id);if(!p||index>=p->count)return 0;
    const unsigned bit=(p->offset-header[10])/100+index;
    return !overrides||(overrides[bit/8]&(1u<<(bit%8)));
}
extern "C" void hmm_ru_read(uint32_t at,void *data,size_t n){
    const HmmLocaleReadGuard reading;
    if(!file||n>UINT32_MAX||!range(at,uint32_t(n),header[3]))throw std::runtime_error("HMM2.RUS read failed");
    // Small compressed atlas chunks share the existing32KiB tagged cache too.
    // Reopening nearby textures must not discard every previous file window.
    if(n<=4096&&hmm_package_size<0x80000000u){
        if(hmm_file_cached_read(file,at,data,n)!=n)throw std::runtime_error("HMM2.RUS read failed");
        return;
    }
    /* A texture upload must not evict the file buffer holding adjacent frame
     * descriptions. Two read positions, both through the existing file layer. */
    hmm_file *input=file;
    if(at>=pixel_start){
        if(!pixel_file){pixel_file=hmm_fopen("HMM2.RUS","rb");if(!pixel_file)throw std::runtime_error("HMM2.RUS pixels open failed");}
        input=pixel_file;
    }
    if(hmm_fseek(input,long(at),0)||hmm_fread(data,1,n,input)!=n)throw std::runtime_error("HMM2.RUS read failed");
}
extern "C" void hmm_ru_record(unsigned id,unsigned index,void *record){
    auto *p=group(id);if(!p||index>=p->count)throw std::runtime_error("HMM2.RUS frame");
    hmm_ru_read(p->offset+index*100,record,100);
    auto *r=static_cast<uint32_t*>(record);
    if(r[2]>2048||r[3]>2048||r[6]>1||r[1]!=r[2]*r[3]*(r[6]?1u:2u)||r[0]<pixel_start||!range(r[0],r[1],header[3]))throw std::runtime_error("HMM2.RUS pixels");
    r[0]|=HMM_RU_SOURCE;
    for(unsigned pos:{7u,11u})if(r[pos+1]){
        auto &ft=*reinterpret_cast<HmmFtImage*>(r+pos);
        const bool compressed=(ft.flags&4)!=0;
        const bool direct=(ft.flags&8)!=0;
        if((r[pos]&511)||(r[pos+1]&511)||r[pos+1]>0x300000
           ||(direct?(header[1]!=5||compressed||r[pos]<direct_base||!range(r[pos]-direct_base,r[pos+1],direct_bytes)):
                       (r[pos]<pixel_start||!range(r[pos],compressed?8u:r[pos+1],header[3])))
           ||ft.stride<r[2]||ft.stride>2048||(ft.stride&3)||ft.height<r[3]||ft.height>2048||ft.cell>=128||(ft.flags&~15u)||(compressed&&header[1]<3)
           ||uint32_t(ft.stride)*ft.height*(ft.cell+1)>ft.bytes)throw std::runtime_error("HMM2.RUS atlas");
        if(direct){ft.flags&=~8u;continue;}
        r[pos]|=HMM_RU_SOURCE;
        if(compressed){r[pos]|=HMM_RU_INFLATE;ft.flags&=~4u;}
    }
}
extern "C" void hmm_ru_graphics(uint32_t at,uint32_t address,uint32_t size){
    const bool compressed=(at&HMM_RU_INFLATE)!=0;
    at&=~(HMM_RU_SOURCE|HMM_RU_INFLATE);
    if(!size||(at&511)||(size&511)||address>=0x300000||size>0x300000-address)throw std::runtime_error("HMM2.RUS transfer");
    struct Guard {Guard(){hmm_gpu_busy_enter();}~Guard(){hmm_gpu_busy_leave();}} guard;
    alignas(4) static uint32_t data[512];
    if(compressed){
        uint32_t h[2];hmm_ru_read(at,h,sizeof(h));
        if(!h[0]||h[0]>size||h[1]!=size||!range(at+8,(h[0]+3)&~3u,header[3]))throw std::runtime_error("HMM2.RUS compressed atlas");
        const uint32_t command[]={0xffffff22u,address};hmm_gpu_upload_commands(command,2);
        size=(h[0]+3)&~3u;at+=8;
        while(size){unsigned n=size<sizeof(data)?unsigned(size):unsigned(sizeof(data));hmm_ru_read(at,data,n);
            hmm_gpu_upload_commands(data,n/4);size-=n;at+=n;hmm_loading_tick();}
        hmm_gpu_upload_commands(nullptr,0);
        return;
    }
    while(size){unsigned n=size<sizeof(data)?unsigned(size):unsigned(sizeof(data));hmm_ru_read(at,data,n);
        if(hmm_core->ft_write(address,data,n/4)!=ZM_OK)throw std::runtime_error("HMM2.RUS FT write");
        size-=n;at+=n;address+=n;hmm_loading_tick();}
}
namespace Translation {
std::pair<bool,bool> setLanguage(std::string_view lang){const bool ok=lang=="ru"&&hmm_ru_prepare();if(ok)russian=true;return {lang=="ru",ok};}
bool setLanguage(const std::string &lang,std::string_view){return setLanguage(std::string_view(lang)).second;}
void reset(){russian=false;}
const char *gettext(const char *s){auto *p=lookup(s,0);return p?p:strip(s);}
const char *gettext(const std::string &s){return gettext(s.c_str());}
const char *getNonTranslated(const char *s){return strip(s);}
const char *ngettext(const char *s,const char *plural,size_t n){
    unsigned form=n%10==1&&n%100!=11?0:n%10>=2&&n%10<=4&&(n%100<10||n%100>=20)?1:2;
    auto *p=lookup(s,form);return p?p:strip(n==1?s:plural);
}
std::string StringLower(std::string s){for(char &v:s){unsigned char c=v;if(c>='A'&&c<='Z')v=char(c+32);else if(russian&&c>=0xc0&&c<=0xdf)v=char(c+32);else if(russian&&c==0xa8)v=char(0xb8);}return s;}
}
