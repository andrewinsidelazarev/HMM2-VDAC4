/* Клод: SMKVideoSequence поверх video/<имя>.smk из HMM2.DAT (tools/build_stage1_clips.py).
 * Логика показа остаётся оригинальной в game_video.cpp и game_newgame.cpp, заменён только источник кадров:
 * JPEG декодирует сопроцессор FT812, кадр рисуется слоем экрана, поэтому палитра кадра не нужна.
 * Звук — дескриптор OGG, Mixer::Play в hmm_audio.cpp отдаёт его пути музыки.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "smk_decoder.h"
#include "hmm_platform.h"
#include "hmm_resources.h"
#include "image.h"
#include <cstring>
namespace {
unsigned clip_users,clip_slot;
}
SMKVideoSequence::SMKVideoSequence(const std::string &path){
    hmm_file *f=hmm_fopen(path.c_str(),"rb");if(!f)return;
    uint32_t h[6];
    if(hmm_fread(h,4,6,f)==6&&h[0]==0x31564d48u){
        _width=int32_t(h[1]);_height=int32_t(h[2]);_frameCount=h[3];_microsecondsPerFrame=h[4];
        _base=hmm_file_offset(f);_slot=clip_slot++%HMM_CLIP_SLOTS;++clip_users;
        if(!(h[5]&2)){_table.resize(_frameCount*2);if(hmm_fread(_table.data(),8,_frameCount,f)!=_frameCount)_frameCount=0;}
        if(h[5]&1){
            std::vector<uint8_t> name(40);std::strncpy(reinterpret_cast<char*>(name.data()),path.c_str(),35);
            std::strcpy(reinterpret_cast<char*>(name.data())+path.size()-3,"ogg");_audioChannel.push_back(std::move(name));
        }
    }
    hmm_fclose(f);
}
SMKVideoSequence::~SMKVideoSequence(){
    if(_base&&!--clip_users){clip_slot=0;hmm_gpu_clip_end();}
}
void SMKVideoSequence::resetFrame(){_currentFrameId=0;_clockFrom=0;}
void SMKVideoSequence::skipFrame(){if(_currentFrameId<_frameCount)++_currentFrameId;}
void SMKVideoSequence::getCurrentFrame(fheroes2::Image &image,int32_t x,int32_t y,int32_t &width,int32_t &height,std::vector<uint8_t> &palette)const{
    width=_width;height=_height;palette.clear();
    if(_currentFrameId>=_frameCount||_table.empty())return;
    // Клод: кадры независимы, поэтому при отставании лишние пропускаются без чтения с карты: ролик идёт по
    // часам вместе со звуком, а не растягивается. Отсчёт идёт от готового первого кадра, перед стартом звука.
    // Последние два кадра не перепрыгиваются: конец и цикл проверяет ShowVideo перед обычным ходом кадра.
    if(_clockFrom&&_frameCount>2){
        const uint64_t wanted=(hmm_millis()-(_clockFrom-1))*1000/uint64_t(_microsecondsPerFrame);
        const unsigned long limit=_frameCount-2;
        if(wanted>_currentFrameId&&limit>_currentFrameId)_currentFrameId=wanted<limit?(unsigned long)wanted:limit;
    }
    const uint32_t offset=_table[_currentFrameId*2];
    // Клод: одинаковые кадры упаковщик сводит к одному смещению: повторно их не читаем и не декодируем.
    if(offset!=_decoded){
        const unsigned next=_shown^1;
        const uint32_t target=hmm_gpu_clip_buffer(_slot,next,unsigned(_width),unsigned(_height));
        hmm_gpu_clip_decode(_base+offset,_table[_currentFrameId*2+1],target);
        _decoded=offset;_shown=next;_address=target;
    }
    if(!_clockFrom)_clockFrom=hmm_millis()+1;
    hmm_gpu_clip_show(image,x,y,_slot,_address,unsigned(_width),unsigned(_height));
}
std::vector<uint8_t> SMKVideoSequence::getCurrentPalette()const{return {};}
