// Native-only preparation of language dependent graphics with the original engine.
#if defined(HMM2_HOST) && !defined(HMM2_PACKED)
#include "translations.h"
#include "ui_language.h"
#include <set>
#include <array>
#include "hmm_ru_menu_export.inc"
#include "hmm_ru_battle_buttons.inc"
#include "hmm_ru_campaign_export.inc"
static int hmm_export_locale(const char *mode){
    const bool russian=!std::strcmp(mode,"ru");
    if(russian){
        if(!Translation::setLanguage(std::string("ru"),std::string_view("HMM2RU.MO")))throw std::runtime_error("Russian MO unavailable");
        if(!Settings::Get().setGameLanguage("ru"))throw std::runtime_error("Russian locale unavailable");
    }else Settings::Get().setGameLanguage("");
    const std::set<int> fonts={ICN::FONT,ICN::SMALFONT,ICN::YELLOW_FONT,ICN::YELLOW_SMALLFONT,ICN::GRAY_FONT,ICN::GRAY_SMALL_FONT,ICN::WHITE_LARGE_FONT,
        ICN::GOLDEN_GRADIENT_FONT,ICN::GOLDEN_GRADIENT_LARGE_FONT,ICN::SILVER_GRADIENT_FONT,ICN::SILVER_GRADIENT_LARGE_FONT,
        ICN::BUTTON_GOOD_FONT_RELEASED,ICN::BUTTON_GOOD_FONT_PRESSED,ICN::BUTTON_EVIL_FONT_RELEASED,ICN::BUTTON_EVIL_FONT_PRESSED};
    const std::set<int> localized={ICN::BUTTON_WELL_MAX,ICN::BUTTONS_FILE_DIALOG_GOOD,ICN::BUTTONS_FILE_DIALOG_EVIL,ICN::BUTTONS_EDITOR_FILE_DIALOG_GOOD,ICN::BUTTONS_EDITOR_FILE_DIALOG_EVIL,ICN::BUTTONS_ADVENTURE_OPTIONS_DIALOG_GOOD,ICN::BUTTONS_ADVENTURE_OPTIONS_DIALOG_EVIL,ICN::BUTTON_QUIT_GOOD,ICN::BUTTON_QUIT_EVIL,ICN::BUTTON_SMALL_CANCEL_GOOD,ICN::BUTTON_SMALL_CANCEL_EVIL,ICN::BUTTON_SMALL_OKAY_GOOD,ICN::BUTTON_SMALL_OKAY_EVIL,ICN::BUTTON_SMALL_ACCEPT_GOOD,ICN::BUTTON_SMALL_ACCEPT_EVIL,ICN::BUTTON_SMALL_DECLINE_GOOD,ICN::BUTTON_SMALL_DECLINE_EVIL,ICN::BUTTON_SMALL_LEARN_GOOD,ICN::BUTTON_SMALL_LEARN_EVIL,ICN::BUTTON_SMALL_TRADE_GOOD,ICN::BUTTON_SMALL_TRADE_EVIL,ICN::BUTTON_SMALL_YES_GOOD,ICN::BUTTON_SMALL_YES_EVIL,ICN::BUTTON_SMALL_NO_GOOD,ICN::BUTTON_SMALL_NO_EVIL,ICN::BUTTON_SMALL_EXIT_GOOD,ICN::BUTTON_SMALL_EXIT_EVIL,ICN::BUTTON_EXIT_HEROES_MEETING,ICN::BUTTON_EXIT_TOWN,ICN::BUTTON_OKAY_TOWN,ICN::BUTTON_EXIT_PUZZLE_DIM_DOOR_GOOD,ICN::BUTTON_EXIT_PUZZLE_DIM_DOOR_EVIL,ICN::BUTTON_SMALL_DISMISS_GOOD,ICN::BUTTON_SMALL_DISMISS_EVIL,ICN::BUTTON_SMALL_UPGRADE_GOOD,ICN::BUTTON_SMALL_UPGRADE_EVIL,ICN::BUTTON_SMALL_RESTART_GOOD,ICN::BUTTON_SMALL_RESTART_EVIL,ICN::BUTTON_KINGDOM_EXIT,ICN::BUTTON_KINGDOM_HEROES,ICN::BUTTON_KINGDOM_TOWNS,ICN::BUTTON_MAPSIZE_SMALL,ICN::BUTTON_MAPSIZE_MEDIUM,ICN::BUTTON_MAPSIZE_LARGE,ICN::BUTTON_MAPSIZE_XLARGE,ICN::BUTTON_MAPSIZE_ALL,ICN::BUTTON_1_GOOD,ICN::BUTTON_2_GOOD,ICN::BUTTON_3_GOOD,ICN::BUTTON_4_GOOD,ICN::BUTTON_5_GOOD,ICN::BUTTON_1_EVIL,ICN::BUTTON_2_EVIL,ICN::BUTTON_3_EVIL,ICN::BUTTON_4_EVIL,ICN::BUTTON_5_EVIL,ICN::BUTTON_MAP_SELECT_GOOD,ICN::BUTTON_MAP_SELECT_EVIL,ICN::BUTTON_MAP_ABOUT_GOOD,ICN::BUTTON_MAP_ABOUT_EVIL,ICN::BUTTONS_NEW_GAME_MENU_GOOD,ICN::BUTTONS_NEW_GAME_MENU_EVIL,ICN::BUTTONS_EDITOR_MENU_GOOD,ICN::BUTTONS_EDITOR_MENU_EVIL,ICN::BUTTON_GIFT_GOOD,ICN::BUTTON_GIFT_EVIL,ICN::UNIFORM_EVIL_MAX_BUTTON,ICN::UNIFORM_EVIL_MIN_BUTTON,ICN::UNIFORM_GOOD_MAX_BUTTON,ICN::UNIFORM_GOOD_MIN_BUTTON,ICN::UNIFORM_GOOD_OKAY_BUTTON,ICN::UNIFORM_EVIL_OKAY_BUTTON,ICN::UNIFORM_GOOD_CANCEL_BUTTON,ICN::UNIFORM_EVIL_CANCEL_BUTTON,ICN::UNIFORM_GOOD_EXIT_BUTTON,ICN::UNIFORM_EVIL_EXIT_BUTTON,ICN::BUTTON_SMALL_MIN_GOOD,ICN::BUTTON_SMALL_MIN_EVIL,ICN::BUTTON_SMALL_MAX_GOOD,ICN::BUTTON_SMALL_MAX_EVIL,ICN::BUTTON_RESET_GOOD,ICN::BUTTON_RESET_EVIL,ICN::BUTTON_START_GOOD,ICN::BUTTON_START_EVIL,ICN::BUTTON_GUILDWELL_EXIT,ICN::GOOD_CAMPAIGN_BUTTONS,ICN::EVIL_CAMPAIGN_BUTTONS,ICN::POL_CAMPAIGN_BUTTONS,ICN::BUTTON_VIEWWORLD_EXIT_GOOD,ICN::BUTTON_VIEWWORLD_EXIT_EVIL,ICN::BUTTON_VERTICAL_DISMISS,ICN::BUTTON_VERTICAL_EXIT,ICN::BUTTON_VERTICAL_PATROL,ICN::BUTTON_HSCORES_VERTICAL_CAMPAIGN_GOOD,ICN::BUTTON_HSCORES_VERTICAL_CAMPAIGN_EVIL,ICN::BUTTON_HSCORES_VERTICAL_EXIT_GOOD,ICN::BUTTON_HSCORES_VERTICAL_EXIT_EVIL,ICN::BUTTON_HSCORES_VERTICAL_STANDARD_GOOD,ICN::BUTTON_HSCORES_VERTICAL_STANDARD_EVIL,ICN::DISMISS_HERO_DISABLED_BUTTON,ICN::BUTTON_SELECT_GOOD,ICN::BUTTON_SELECT_EVIL,ICN::BUTTON_TOGGLE_ALL_ON_GOOD,ICN::BUTTON_TOGGLE_ALL_OFF_GOOD,ICN::BUTTON_TOGGLE_ALL_ON_EVIL,ICN::BUTTON_TOGGLE_ALL_OFF_EVIL,ICN::ARMY_ESTIMATION_ICON,ICN::BTNSHNGL,ICN::TEXTBAR};
    const char *destination=std::getenv("HMM2_EXPORT_OUT");if(!destination)throw std::runtime_error("Locale export path missing");
    if(std::getenv("HMM2_EXPORT_CAMPAIGN_ONLY")){
        FILE *palette=std::fopen((std::string(destination)+".pal").c_str(),"wb");
        if(!palette||std::fwrite(fheroes2::getGamePalette(),1,768,palette)!=768)throw std::runtime_error("campaign palette export");
        std::fclose(palette);
    }
    FILE *out=std::fopen(destination,"wb");if(!out)throw std::runtime_error("Locale export open");
    auto write=[&](const void*p,size_t n){if(std::fwrite(p,1,n,out)!=n)throw std::runtime_error("Locale export write");};
    uint32_t header[]={0x58555248,1,ICN::LASTICN,0};write(header,sizeof(header));
    unsigned count=0,frames=0;
    for(int id=1;id<ICN::LASTICN;++id){
        const bool campaign=id==ICN::CAMPBKGE||id==ICN::CAMPBKGG||id==ICN::X_CMPBKG||id==ICN::X_CMPEXT;
        if(std::getenv("HMM2_EXPORT_CAMPAIGN_ONLY")&&!campaign)continue;
        if(std::getenv("HMM2_EXPORT_MENU_ONLY")&&id!=ICN::BTNSHNGL)continue;
        if(!fonts.count(id)&&!localized.count(id)&&!campaign)continue;
        const auto labels=russian&&id==ICN::X_CMPEXT?hmm_campaign_labels():std::vector<fheroes2::Sprite>{};
        const uint32_t originalCount=fheroes2::AGG::GetICNCount(id),n=originalCount+uint32_t(labels.size());
        uint32_t h[]={uint32_t(id),n};write(h,sizeof(h));++count;
        for(uint32_t i=0;i<n;++i){
            fheroes2::Sprite localized;
            const auto *image=i<originalCount?&fheroes2::AGG::GetICN(id,i):&labels[i-originalCount];
            if(russian&&id==ICN::X_CMPEXT&&i>=15&&i<=18){localized=hmm_campaign_title(*image,i);image=&localized;}
            if(russian&&id==ICN::BTNSHNGL){localized=hmm_russian_menu_sprite(*image,i);image=&localized;}
            if(russian&&id==ICN::TEXTBAR){localized=hmm_russian_battle_button(*image,i);image=&localized;}
            const auto &im=*image;
            uint32_t r[]={uint32_t(im.width()),uint32_t(im.height()),uint32_t(im.x()),uint32_t(im.y()),im.singleLayer()?1u:0u};write(r,sizeof(r));
            const size_t size=size_t(im.width())*im.height();if(size){write(im.image(),size);if(!im.singleLayer())write(im.transform(),size);}++frames;
        }
    }
    std::fseek(out,12,SEEK_SET);write(&count,4);std::fclose(out);
    std::printf("LOCALE EXPORT %s groups=%u frames=%u\n",mode,count,frames);return 1;
}
#endif
