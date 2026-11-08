/* Codex: подготовка отдельных кадров исходным декодером и коррекциями OpenHMM2.
 * Пакет создаётся локально из данных пользователя; распространение не выполняется.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "agg_image.h"
#include "icn.h"
#include "til.h"
#include "image.h"
#include "hmm_platform.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <zlib.h>
namespace {
void u32(FILE *f,uint32_t v){uint8_t p[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};if(std::fwrite(p,1,4,f)!=4)throw std::runtime_error("pack write");}
struct Rec {uint32_t offset,size;int32_t w,h,x,y;uint32_t single;};
Rec writeImage(FILE *f,const fheroes2::Image &img,int32_t x,int32_t y){
    Rec r{uint32_t(std::ftell(f)),0,img.width(),img.height(),x,y,img.singleLayer()?1u:0u};
    if(img.empty())return r;
    size_t n=size_t(img.width())*img.height();std::vector<uint8_t> raw(img.image(),img.image()+n);
    /* Codex: нулевая маска полностью непрозрачна; single-layer даёт те же пиксели без второго буфера. */
    if(!img.singleLayer()){
        if(std::all_of(img.transform(),img.transform()+n,[](uint8_t v){return v==0;}))r.single=1;
        else raw.insert(raw.end(),img.transform(),img.transform()+n);
    }
    uLongf len=compressBound(uLong(raw.size()));std::vector<uint8_t> data(len);
    if(compress2(data.data(),&len,raw.data(),uLong(raw.size()),9)!=Z_OK)throw std::runtime_error("pack compress");
    if(std::fwrite(data.data(),1,len,f)!=len)throw std::runtime_error("pack write");r.size=uint32_t(len);return r;
}
}
extern "C" int hmm_host_ready(){
    const char *path=std::getenv("HMM2_PACK_OUT");if(!path){extern int hmm_scene_run();return hmm_scene_run();}
    FILE *f=std::fopen(path,"wb+");if(!f)throw std::runtime_error("pack open");
    constexpr unsigned groups=ICN::LASTICN+TIL::LASTTIL*4;
    u32(f,0x4b503248);u32(f,1);u32(f,ICN::LASTICN);u32(f,TIL::LASTTIL);
    for(unsigned i=0;i<groups*2;i++)u32(f,0);
    auto group=[&](unsigned g,const std::vector<Rec>&records){
        auto offset=std::ftell(f);for(const auto &r:records){u32(f,r.offset);u32(f,r.size);u32(f,uint32_t(r.w));u32(f,uint32_t(r.h));u32(f,uint32_t(r.x));u32(f,uint32_t(r.y));u32(f,r.single);}
        auto end=std::ftell(f);std::fseek(f,16+g*8,SEEK_SET);u32(f,uint32_t(offset));u32(f,uint32_t(records.size()));std::fseek(f,end,SEEK_SET);
    };
    unsigned total=0;
    for(int id=1;id<ICN::LASTICN;id++){
        std::vector<Rec> r;auto count=fheroes2::AGG::GetICNCount(id);
        for(unsigned i=0;i<count;i++){const auto &s=fheroes2::AGG::GetICN(id,i);r.push_back(writeImage(f,s,s.x(),s.y()));}
        total+=count;group(unsigned(id),r);std::printf("pack ICN=%d frames=%u total=%u bytes=%ld\n",id,count,total,std::ftell(f));std::fflush(stdout);
    }
    for(int id=1;id<TIL::LASTTIL;id++)for(unsigned shape=0;shape<4;shape++){
        std::vector<Rec> r;for(unsigned i=0;;i++){const auto &s=fheroes2::AGG::GetTIL(id,i,shape);if(s.empty())break;r.push_back(writeImage(f,s,0,0));}
        group(ICN::LASTICN+id*4+shape,r);total+=unsigned(r.size());
    }
    std::printf("pack done frames=%u bytes=%ld\n",total,std::ftell(f));std::fclose(f);return 1;
}
