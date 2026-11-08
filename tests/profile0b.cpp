// Codex: наблюдатель хранит свои таблицы через VirtualAlloc и не входит в измеряемую кучу.
#include "profile0b.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "image.h"
#include "screen.h"
namespace {
struct Entry{const void*p;size_t bytes;};
Entry *table=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
constexpr size_t count=1u<<19;
thread_local bool pixels=false;
long long pixelLive=0,otherPeak=0;
struct Sprite{const void*p;int id;unsigned index,w,h,single;};
Sprite *sprites=nullptr;unsigned used=0;
const char *tracePhase="startup";bool traceDone=false;FILE *trace=nullptr;unsigned phaseFrames=0;
size_t slot(const void*p){return (reinterpret_cast<uintptr_t>(p)>>4)*2654435761u&(count-1);}
void ensure(){if(!table)table=static_cast<Entry*>(VirtualAlloc(nullptr,count*sizeof(Entry),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!table)std::abort();}
}
uint8_t *profile0b_pixels(size_t n){
    pixels=true;auto*p=new uint8_t[n];pixels=false;
    AcquireSRWLockExclusive(&lock);ensure();
    size_t i=slot(p);while(table[i].p && table[i].p!=reinterpret_cast<void*>(1))i=(i+1)&(count-1);
    table[i]={p,n};ReleaseSRWLockExclusive(&lock);return p;
}
void profile0b_hook(int kind,void *data,size_t bytes,long long live){
    AcquireSRWLockExclusive(&lock);
    if(kind==_HOOK_ALLOC && pixels)pixelLive+=bytes;
    if(kind==_HOOK_FREE && data && table){
        size_t i=slot(data);while(table[i].p && table[i].p!=data)i=(i+1)&(count-1);
        if(table[i].p==data){pixelLive-=table[i].bytes;table[i].p=reinterpret_cast<void*>(1);}
    }
    if(live-pixelLive>otherPeak)otherPeak=live-pixelLive;
    ReleaseSRWLockExclusive(&lock);
}
void profile0b_phase(const char*name,bool finish){
    if(finish)std::printf("NOPIXEL\t%s\t%lld\t%lld\n",name,pixelLive,otherPeak);
    else {otherPeak=0;tracePhase=name;traceDone=false;phaseFrames=0;if(trace){std::fclose(trace);trace=nullptr;}}
}
void profile0b_sprite(int id,unsigned index,const uint8_t*p,int w,int h,bool single){
    if(!p||w<=0||h<=0)return;
    if(!sprites)sprites=static_cast<Sprite*>(VirtualAlloc(nullptr,65536*sizeof(Sprite),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    for(unsigned i=0;i<used;i++)if(sprites[i].p==p)return;
    if(used==65536)std::abort();sprites[used++]={p,id,index,(unsigned)w,(unsigned)h,(unsigned)single};
}
void profile0b_frame(const char *phase,const uint8_t*p,int w,int h){
    ++phaseFrames;
    unsigned long long n=0;
    for(unsigned i=0;i<used;i++){const auto&s=sprites[i];n+=(unsigned long long)s.w*s.h*(s.single?1:2);}
    std::printf("WORKSET\t%s\t%u\t%llu\n",phase,used,n);used=0;
    if(trace){
        traceDone=true;std::fclose(trace);trace=nullptr;
        char name[64];std::snprintf(name,sizeof(name),"%s.trace.idx",phase);FILE*f=std::fopen(name,"wb");if(!f)std::abort();
        std::fwrite(p,1,(size_t)w*h,f);std::fclose(f);
    }
}
static bool begin_trace(fheroes2::Image &out){
    if(traceDone || out.image()!=fheroes2::Display::instance().image())return false;
    // Codex: первые кадры боя принадлежат затемнению через ApplyPalette; берём кадр после перехода.
    if(!std::strcmp(tracePhase,"battle") && phaseFrames<100)return false;
    if(std::strcmp(tracePhase,"menu")&&std::strcmp(tracePhase,"map")&&std::strcmp(tracePhase,"town")&&std::strcmp(tracePhase,"battle"))return false;
    if(!trace){
        char name[64];std::snprintf(name,sizeof(name),"%s.blits",tracePhase);trace=std::fopen(name,"wb");if(!trace)std::abort();
        uint32_t header[]={0x30304248,(uint32_t)out.width(),(uint32_t)out.height()};std::fwrite(header,4,3,trace);
        std::fwrite(out.image(),1,(size_t)out.width()*out.height(),trace);
    }
    return true;
}
void profile0b_fill(fheroes2::Image &out,int x,int y,int w,int h,uint8_t color){
    if(!begin_trace(out))return;
    static uint8_t *buffer=nullptr;
    if(!buffer)buffer=static_cast<uint8_t*>(VirtualAlloc(nullptr,640*480,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!buffer||size_t(w)*h>640*480)std::abort();
    std::memset(buffer,color,size_t(w)*h);
    uint32_t op[]={(uint32_t)w,(uint32_t)h,0,0,(uint32_t)x,(uint32_t)y,(uint32_t)w,(uint32_t)h,0,1,1,0};
    std::fwrite(op,4,12,trace);std::fwrite(buffer,1,size_t(w)*h,trace);
}
void profile0b_blit(const fheroes2::Image &in,int ix,int iy,fheroes2::Image &out,int ox,int oy,int w,int h,bool flip,bool copy){
    if(!begin_trace(out))return;
    uint32_t identified=0;for(unsigned i=0;i<used;i++)if(sprites[i].p==in.image()){identified=1;break;}
    uint32_t op[]={(uint32_t)in.width(),(uint32_t)in.height(),(uint32_t)ix,(uint32_t)iy,(uint32_t)ox,(uint32_t)oy,(uint32_t)w,(uint32_t)h,(uint32_t)flip,(uint32_t)copy,(uint32_t)in.singleLayer(),identified};
    std::fwrite(op,4,12,trace);size_t n=(size_t)in.width()*in.height();std::fwrite(in.image(),1,n,trace);
    if(!in.singleLayer())std::fwrite(in.transform(),1,n,trace);
}
