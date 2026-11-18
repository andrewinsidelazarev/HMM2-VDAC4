/* Optional Russian speech captions, drawn above the existing video layer. */
#pragma once
#include "game_video.h"
#include <memory>
#include <vector>

class HmmVideoSubtitles {
    std::vector<uint8_t> data;
    unsigned count=0,shown=~0u;
    std::unique_ptr<Video::Subtitle> image;
    fheroes2::Rect band;
public:
    explicit HmmVideoSubtitles(const std::vector<Video::VideoInfo>&);
    bool active()const{return count!=0;}
    void render(fheroes2::Image&,const fheroes2::Rect&,uint32_t timeMS);
};
