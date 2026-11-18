/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_subtitles.h"
#include "hmm_locale.h"
#include "hmm_platform.h"
#include "ui_text.h"
#include "ui_language.h"
#include <algorithm>
#include <cstring>
#ifdef HMM2_HOST
#include <cstdio>
#include <cstdlib>
#endif

namespace {
uint32_t hash(const void *source,size_t n,uint32_t v=2166136261u){
    auto *p=static_cast<const uint8_t*>(source);while(n--)v=(v^*p++)*16777619u;return v;
}
uint32_t video_hash(const std::string &name){
    uint32_t v=2166136261u;
    for(unsigned char c:name){if(c>='A'&&c<='Z')c+=32;v=(v^c)*16777619u;}return v;
}
struct CloseFile {hmm_file *file;~CloseFile(){if(file)hmm_fclose(file);}};
}

HmmVideoSubtitles::HmmVideoSubtitles(const std::vector<Video::VideoInfo> &videos){
    if(!hmm_ru_active())return;
    CloseFile f{hmm_fopen("HMM2.SUB","rb")};if(!f.file)return;
    uint32_t h[6],selected[5]={},indexHash=2166136261u;
    if(hmm_fread(h,4,6,f.file)!=6||h[0]!=0x42555348u||h[1]!=1||h[2]>256*1024
       ||!h[3]||h[3]>128||h[5]||h[2]<24+h[3]*20)return;
    if(hmm_fseek(f.file,0,2)||uint32_t(hmm_ftell(f.file))!=h[2]||hmm_fseek(f.file,24,0))return;
    for(unsigned i=0;i<h[3];++i){
        uint32_t r[5];if(hmm_fread(r,4,5,f.file)!=5)return;
        indexHash=hash(r,sizeof(r),indexHash);
        for(const auto &video:videos)if((video.control&Video::PLAY_AUDIO)&&video_hash(video.fileName)==r[0]){
            if(!selected[2])std::memcpy(selected,r,sizeof(r));
        }
    }
    if(indexHash!=h[4]||!selected[2])return;
    const uint32_t offset=selected[1],bytes=selected[2],cues=selected[3];
    if(offset<24+20*h[3]||offset>h[2]||bytes>h[2]-offset||bytes>65536
       ||!cues||cues>512||cues*12>=bytes)return;
    std::vector<uint8_t> raw(bytes);
    if(hmm_fseek(f.file,long(offset),0)||hmm_fread(raw.data(),1,bytes,f.file)!=bytes
       ||hash(raw.data(),raw.size())!=selected[4])return;
    const auto *items=reinterpret_cast<const uint32_t*>(raw.data());
    uint32_t end=0;
    for(unsigned i=0;i<cues;++i){
        const auto *r=items+i*3;
        if(r[0]<end||r[0]>=r[1]||r[1]>1800000||r[2]<cues*12||r[2]>=bytes)return;
        const auto *text=raw.data()+r[2];const auto *zero=static_cast<const uint8_t*>(std::memchr(text,0,bytes-r[2]));
        if(!zero||zero==text||zero-text>512)return;
        end=r[1];
    }
    data=std::move(raw);count=cues;
}

void HmmVideoSubtitles::render(fheroes2::Image &output,const fheroes2::Rect &roi,uint32_t timeMS){
    if(!count)return;
    const auto *items=reinterpret_cast<const uint32_t*>(data.data());
    unsigned selected=~0u;
    // Keep the previous words visible through pauses. The next start time,
    // or destruction at the end of this clip, replaces the caption.
    for(unsigned i=0;i<count&&items[i*3]<=timeMS;++i)selected=i;
    if(shown!=~0u&&selected<shown)selected=shown;
    if(selected!=shown){
        image.reset();shown=selected;
        if(selected!=~0u){
            const fheroes2::Text text(reinterpret_cast<const char*>(data.data()+items[selected*3+2]),
                fheroes2::FontType::largeWhite(),fheroes2::SupportedLanguage::Russian);
            const int32_t width=std::max<int32_t>(1,std::min<int32_t>(608,roi.width-16));
            const int32_t textWidth=text.width(width),textHeight=text.height(textWidth);
            // Leave the lower80px for the speaker/amulet inset in campaign videos.
            const int32_t bottom=roi.height-(roi.height>=400?80:16);
            const int32_t top=std::max<int32_t>(8,bottom-textHeight-1);
            const fheroes2::Point position{roi.width/2,top};
            band={(roi.width-textWidth-1)/2-6,top-4,textWidth+13,textHeight+9};
#ifdef HMM2_HOST
            if(std::getenv("HMM2_SUBTITLE_LAYOUT"))std::printf("SUBTITLE LAYOUT cue=%u width=%ld height=%ld\n",selected,long(textWidth),long(textHeight));
#endif
            image.reset(new Video::Subtitle(text,0,UINT32_MAX,position,width));
        }
    }
    if(image){
        // Existing FT812 translucent RECTS; no background capture or pixel buffer.
        for(unsigned i=0;i<2;++i)
            fheroes2::ApplyTransform(output,roi.x+band.x,roi.y+band.y,band.width,band.height,2);
        image->render(output,roi);
    }
}
