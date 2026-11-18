// Translate known bundled-map text at presentation, preserving saved originals.
#pragma once
#include "ui_text.h"
#include "game_language.h"
#include <string>
#include <optional>
#include <utility>
#ifdef HMM2_PACKED
#include "hmm_locale.h"
#endif
namespace fheroes2 {
#if defined(HMM2_PACKED) && !defined(HMM2_HOST)
__attribute__((noinline))
#endif
inline Text hmm_map_text(const std::string &value,const FontType font,const std::optional<SupportedLanguage> language){
#ifdef HMM2_PACKED
    const char *translated=hmm_ru_map_lookup(value.c_str());
    if(!translated)translated=hmm_ru_lookup(value.c_str());
    if(translated)return Text(translated,font,SupportedLanguage::Russian);
#endif
    return Text(value,font,language);
}
}
