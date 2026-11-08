/* Codex: логический дисплей и DL 640×480; готовый кадр масштабирует скейлер HDMI в Tang.
 * Общая часть Display ниже берётся без замены алгоритмов из screen.cpp.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "screen.h"
#include "image_palette.h"
#include "math_tools.h"
#include "hmm_platform.h"
#include <algorithm>
#include <cassert>
namespace {
bool getActiveArea(fheroes2::Rect &r,int32_t w,int32_t h){
    if(r.width<=0||r.height<=0||r.x>=w||r.y>=h)return false;
    if(r.x<0){r.width+=r.x;r.x=0;}if(r.y<0){r.height+=r.y;r.y=0;}
    r.width=std::min(r.width,w-r.x);r.height=std::min(r.height,h-r.y);
    return r.width>0&&r.height>0;
}
std::vector<uint8_t> StandardPaletteIndexes(){std::vector<uint8_t> p(256);for(unsigned i=0;i<256;i++)p[i]=uint8_t(i);return p;}
const uint8_t *PALPalette(bool update=false){
    static uint8_t rgb[768];static bool ready=false;
    if(!ready||update){const auto*p=fheroes2::getGamePalette();for(unsigned i=0;i<768;i++)rgb[i]=uint8_t(p[i]<<2);ready=true;}return rgb;
}
const uint8_t *currentPalette=nullptr;
class RenderEngine:public fheroes2::BaseRenderEngine {
    uint8_t rgb[768]{};
public:
    static RenderEngine *create(){return new RenderEngine;}
    std::vector<fheroes2::ResolutionInfo> getAvailableResolutions()const override{return {{640,480}};}
    fheroes2::Rect getActiveWindowROI()const override{return {0,0,640,480};}
    fheroes2::Size getCurrentScreenResolution()const override{return {640,480};}
protected:
    bool allocate(fheroes2::ResolutionInfo &r,bool)override{r={640,480};currentPalette=PALPalette();updatePalette(StandardPaletteIndexes());return hmm_video_open()!=0;}
    void clear()override{}
    bool isMouseCursorActive()const override{return true;}
    void render(const fheroes2::Display &d,const fheroes2::Rect&)override{
#ifdef HMM2_PACKED
        // Codex: показ DL не требует чтения пикселей Display и распаковки его исходных слоёв.
        hmm_gpu_message_present(rgb,d.width(),d.height());
#else
        hmm_video_frame(d.image(),rgb,d.width(),d.height());
#endif
    }
    void updatePalette(const std::vector<uint8_t>&ids)override{if(!currentPalette)currentPalette=PALPalette();for(unsigned i=0;i<256;i++)for(unsigned c=0;c<3;c++)rgb[i*3+c]=currentPalette[ids[i]*3+c];}
};
class RenderCursor:public fheroes2::Cursor {
public:
    RenderCursor(){_emulation=true;}
    static RenderCursor *create(){return new RenderCursor;}
    // Codex: системного курсора нет; настройки игры не должны отключать программный.
    void enableSoftwareEmulation(bool)override{_emulation=true;}
};
}
