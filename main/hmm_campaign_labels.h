#pragma once
#include "hmm_locale.h"
#include "agg_image.h"
#include "icn.h"
#include "image.h"
inline void hmm_campaign_labels(int background,fheroes2::Image &output,const fheroes2::Point &offset){
    if(!hmm_ru_active())return;
    if(hmm_ru_count(ICN::X_CMPEXT)<33)return;
    const unsigned first=background==ICN::CAMPBKGE?19:background==ICN::CAMPBKGG?24:29;
    const unsigned count=background==ICN::X_CMPBKG?4:5;
    for(unsigned i=0;i<count;++i){
        const auto &sprite=fheroes2::AGG::GetICN(ICN::X_CMPEXT,first+i);
        fheroes2::Blit(sprite,output,offset.x+sprite.x(),offset.y+sprite.y());
    }
}
