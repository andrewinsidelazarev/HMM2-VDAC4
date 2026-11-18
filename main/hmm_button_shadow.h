#pragma once
#include "image.h"
#include "hmm_locale.h"
#include "hmm_resources.h"
#include "agg_image.h"
#include "icn.h"
inline bool hmm_prepared_button_shadow(const fheroes2::Sprite &in,fheroes2::Image &out,
        const fheroes2::Point &position,const fheroes2::Point &offset){
    if(!hmm_ru_active()||offset.x!=-5||offset.y!=5)return false;
    uint32_t key;if(!hmm_resource_id(in,key)||(key&65535))return false;
    const unsigned group=(key>>16)&~HMM_RU_GROUP;
    const int buttons[]={ICN::BUTTON_SMALL_OKAY_GOOD,ICN::BUTTON_SMALL_OKAY_EVIL,
        ICN::BUTTON_SMALL_CANCEL_GOOD,ICN::BUTTON_SMALL_CANCEL_EVIL,
        ICN::BUTTON_SMALL_MIN_GOOD,ICN::BUTTON_SMALL_MIN_EVIL,
        ICN::BUTTON_SMALL_MAX_GOOD,ICN::BUTTON_SMALL_MAX_EVIL};
    for(unsigned i=0;i<8;++i)if(group==unsigned(buttons[i])){
        if(hmm_ru_count(ICN::X_CMPEXT)<=33+i)return false;
        const auto &shadow=fheroes2::AGG::GetICN(ICN::X_CMPEXT,33+i);
        fheroes2::Blit(shadow,out,position.x+shadow.x(),position.y+shadow.y());return true;
    }
    return false;
}
