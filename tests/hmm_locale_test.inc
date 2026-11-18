#ifdef HMM2_PACKED
#include "hmm_locale.h"
#include "ui_language.h"
#include "translations.h"
#include "dialog_language_selection.h"
#include "maps_objects.h"
static unsigned locale_step,locale_events;
static bool locale_cancel;
static void hmm_locale_map_cases(){
    const char *path=std::getenv("HMM2_LOCALE_MAP_CASES");if(!path)return;
    FILE *f=std::fopen(path,"rb");if(!f)throw std::runtime_error("map locale cases");
    auto u32=[&](){uint32_t v;if(std::fread(&v,4,1,f)!=1)throw std::runtime_error("map case header");return v;};
    auto text=[&](){unsigned n=u32();if(n>2048)throw std::runtime_error("map case size");std::string s(n,'\0');if(std::fread(s.data(),1,n,f)!=n)throw std::runtime_error("map case text");return s;};
    if(u32()!=0x54504d48)throw std::runtime_error("map case magic");
    const unsigned count=u32();std::vector<std::pair<std::string,std::string>> cases;
    for(unsigned i=0;i<count;++i){auto source=text();auto value=text();cases.emplace_back(std::move(source),std::move(value));}
    if(std::fgetc(f)!=EOF)throw std::runtime_error("map cases trailing");std::fclose(f);
    if(std::getenv("HMM2_LOCALE_MAP_CORRUPT")){
        for(unsigned repeat=0;repeat<2;++repeat){bool rejected=false;
            try{hmm_ru_map_lookup(cases[0].first.c_str());}catch(const std::runtime_error &){rejected=true;}
            if(!rejected||hmm_locale_io_busy)throw std::runtime_error("corrupt map string accepted/cached/guard leaked");
        }
        std::puts("LOCALE CORRUPT MAP STRING REJECTED TWICE");return;
    }
    const char *stable=_("Russian");const std::string copied=stable;
    for(unsigned pass=0;pass<3;++pass)for(const auto &v:cases){
        auto key=pass==1?std::string(" \t\r\n")+v.first+"\n ":v.first;
        const char *translated=hmm_ru_map_lookup(key.c_str());
        if(!translated||v.second!=translated)throw std::runtime_error("map locale mismatch");
    }
    if(hmm_ru_map_lookup("__NO_SUCH_MAP_TEXT__")||std::strcmp(stable,copied.c_str()))throw std::runtime_error("map cache pointer isolation");
    Translation::reset();if(count&&hmm_ru_map_lookup(cases[0].first.c_str()))throw std::runtime_error("English map translated");
    Translation::setLanguage(std::string_view("ru"));
    std::printf("LOCALE MAP entries=%u cache evictions/whitespace/EN/lifetime EXACT\n",count);
}
static void hmm_locale_answer_cases(){
    const char *path=std::getenv("HMM2_LOCALE_ANSWER_CASES");if(!path)return;
    FILE *f=std::fopen(path,"rb");if(!f)throw std::runtime_error("answer locale cases");
    auto u32=[&](){uint32_t v;if(std::fread(&v,4,1,f)!=1)throw std::runtime_error("answer case header");return v;};
    auto text=[&](){unsigned n=u32();if(n>2048)throw std::runtime_error("answer case size");std::string s(n,'\0');if(std::fread(s.data(),1,n,f)!=n)throw std::runtime_error("answer case text");return s;};
    if(u32()!=0x41504d48)throw std::runtime_error("answer case magic");unsigned count=u32(),calls=0;
    for(unsigned i=0;i<count;++i){const auto source=text();auto value=text();
        MapSphinx sphinx;sphinx.answers.push_back(source);
        for(bool truncate:{false,true}){
            sphinx.isTruncatedAnswer=truncate;
            if(!sphinx.isCorrectAnswer(source)||!sphinx.isCorrectAnswer(value)||sphinx.isCorrectAnswer("__WRONG_ANSWER__"))throw std::runtime_error("localized Sphinx answer mismatch");
            for(char &c:value){auto v=uint8_t(c);if(v>=0xe0)c=char(v-32);else if(v==0xb8)c=char(0xa8);}
            if(!sphinx.isCorrectAnswer(value))throw std::runtime_error("Sphinx uppercase answer");
            for(char &c:value)if(uint8_t(c)==0xa8)c=char(0xc5);
            if(!sphinx.isCorrectAnswer(value))throw std::runtime_error("Sphinx optional yo");calls+=5;
        }
    }
    if(std::fgetc(f)!=EOF)throw std::runtime_error("answer cases trailing");std::fclose(f);
    std::printf("LOCALE SPHINX variants=%u actual-method calls=%u EXACT\n",count,calls);
}
static void hmm_locale_catalog_cases(){
    const char *path=std::getenv("HMM2_LOCALE_CASES");if(!path)return;
    FILE *f=std::fopen(path,"rb");if(!f)throw std::runtime_error("catalog cases file");
    auto u32=[&](){uint32_t v;if(std::fread(&v,4,1,f)!=1)throw std::runtime_error("catalog case header");return v;};
    auto text=[&](){unsigned n=u32();if(n>8192)throw std::runtime_error("catalog case size");std::string s(n,'\0');if(n&&std::fread(s.data(),1,n,f)!=n)throw std::runtime_error("catalog case text");return s;};
    if(u32()!=0x54555248)throw std::runtime_error("catalog case magic");unsigned count=u32(),calls=0;
    const size_t numbers[]={0,1,2,4,5,11,12,14,20,21,22,25,101,111,112,121,size_t(-1)};
    for(unsigned i=0;i<count;++i){const auto key=text();unsigned n=u32();std::vector<std::string> values;
        while(n--)values.push_back(text());
        if(values.size()==1){if(Translation::gettext(key)!=values[0])throw std::runtime_error("catalog gettext mismatch");++calls;}
        else for(size_t number:numbers){unsigned form=number%10==1&&number%100!=11?0:number%10>=2&&number%10<=4&&(number%100<10||number%100>=20)?1:2;
            if(Translation::ngettext(key.c_str(),"MISSING TRANSLATION",number)!=values[form])throw std::runtime_error("catalog plural mismatch");++calls;}
    }
    if(std::fgetc(f)!=EOF)throw std::runtime_error("catalog trailing cases");std::fclose(f);
    if(Translation::StringLower("ABC \xc0\xdf\xa8")!="abc \xe0\xff\xb8"||std::strcmp(Translation::getNonTranslated("context|English"),"English"))throw std::runtime_error("locale lowercase/context");
    std::printf("LOCALE CATALOG entries=%u calls=%u ALL EXACT\n",count,calls);
}
static bool hmm_test_locale_events(){
    if(!locale_step)return false;
    if(++locale_events>1000)throw std::runtime_error("language picker input timeout");
    SDL_Keycode key=SDLK_UNKNOWN;
    if(locale_events==5)key=locale_cancel?SDLK_UP:SDLK_DOWN;
    if(locale_events==15)key=locale_cancel?SDLK_ESCAPE:SDLK_RETURN;
    if(key!=SDLK_UNKNOWN){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}
    return true;
}
static int hmm_test_locale(){
    using fheroes2::SupportedLanguage;
    auto &conf=Settings::Get();auto &display=fheroes2::Display::instance();
    if(const char *expected=std::getenv("HMM2_EXPECT_LANGUAGE"))if(conf.getGameLanguage()!=expected)throw std::runtime_error("persisted locale startup");
    const auto languages=fheroes2::getSupportedLanguages();
    if(std::getenv("HMM2_LOCALE_UNAVAILABLE")){
        if(languages!=std::vector<SupportedLanguage>{SupportedLanguage::English}||hmm_ru_active()||!conf.getGameLanguage().empty()||hmm_locale_io_busy)throw std::runtime_error("unavailable locale fallback");
        if(fheroes2::selectLanguage(languages,SupportedLanguage::English,true)!=SupportedLanguage::English)throw std::runtime_error("single language fallback");
        phase("locale_unavailable");fheroes2::drawMainMenuScreen();display.render();std::puts("LOCALE UNAVAILABLE SAFE ENGLISH PASS\nSCENARIO COMPLETE");return 1;
    }
    if(languages!=std::vector<SupportedLanguage>{SupportedLanguage::English,SupportedLanguage::Russian})throw std::runtime_error("language list");
    conf.setGameLanguage("");phase("locale_english");fheroes2::drawMainMenuScreen();display.render();
    const auto &english=fheroes2::AGG::GetICN(ICN::FONT,'A'-32);uint32_t enkey=0;hmm_resource_id(english,enkey);
    locale_step=1;locale_events=0;locale_cancel=false;phase("locale_select_ru");
    auto selected=fheroes2::selectLanguage(languages,SupportedLanguage::English,true);locale_step=0;
    if(selected!=SupportedLanguage::Russian||conf.getGameLanguage()!="ru"||!hmm_ru_active())throw std::runtime_error("Russian selection");
    hmm_locale_catalog_cases();
    hmm_locale_map_cases();
    hmm_locale_answer_cases();
    if(std::getenv("HMM2_LOCALE_ALL_IMAGES")){
        unsigned images=0;
        for(unsigned group=1;group<ICN::LASTICN;++group)for(unsigned i=0;i<hmm_ru_count(group);++i){
            const uint32_t key=((group|HMM_RU_GROUP)<<16)|i;
            const uint32_t expected=hmm_ru_frame(group,i)?key:(group<<16)|i;
            if(hmm_resource_get(key)._hmmResourceKey()!=expected)throw std::runtime_error("locale resource key");++images;
        }
        std::printf("LOCALE ALL %u FT RESOURCE RECORDS VALID\n",images);
    }
    {uint8_t byte=0;bool rejected=false;try{hmm_ru_read(UINT32_MAX,&byte,1);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected||hmm_locale_io_busy)throw std::runtime_error("locale read guard unwind");}
    const char *saved=_("Russian");const std::string copied=saved;
    if(copied!="\xd0\xf3\xf1\xf1\xea\xe8\xe9")throw std::runtime_error("Russian translation encoding");
    const auto &russian=fheroes2::AGG::GetICN(ICN::FONT,'A'-32);uint32_t rukey=0;hmm_resource_id(russian,rukey);
    if(!enkey||!rukey||(hmm_ru_frame(ICN::FONT,'A'-32)?(enkey==rukey||&english==&russian):(enkey!=rukey||&english!=&russian)))throw std::runtime_error("locale sprite bank alias");
    phase("locale_russian");fheroes2::drawMainMenuScreen();display.render();
    const auto same=conf.getGameLanguage();if(!conf.setGameLanguage(conf.getGameLanguage())||conf.getGameLanguage()!=same)throw std::runtime_error("aliased language setting");
    if(conf.setGameLanguage("de")||conf.getGameLanguage()!="ru")throw std::runtime_error("unsupported locale changed state");
    locale_step=1;locale_events=0;locale_cancel=true;phase("locale_cancel_en");
    selected=fheroes2::selectLanguage(languages,SupportedLanguage::Russian,true);locale_step=0;
    if(selected!=SupportedLanguage::Russian||conf.getGameLanguage()!="ru")throw std::runtime_error("language Cancel");
    // No manual Save: the picker itself must have published the preference.
    {FILE *cfg=std::fopen(Settings::configFileName,"rb");if(!cfg)throw std::runtime_error("picker config missing");
     std::string savedConfig;char buf[256];size_t n;while((n=std::fread(buf,1,sizeof(buf),cfg)))savedConfig.append(buf,n);std::fclose(cfg);
     if(savedConfig.find("lang = ru")==std::string::npos)throw std::runtime_error("picker did not persist Russian");}
    if(!conf.setGameLanguage(""))throw std::runtime_error("English restoration");
    if(std::strcmp(saved,copied.c_str())||std::strcmp(_("Russian"),"Russian"))throw std::runtime_error("translation pointer lifetime");
    if(&fheroes2::AGG::GetICN(ICN::FONT,'A'-32)!=&english||&hmm_resource_get(rukey)!=&russian)throw std::runtime_error("bank lifetime");
    phase("locale_english_restored");fheroes2::drawMainMenuScreen();display.render();
    std::puts("LOCALE SWITCH/PREVIEW/CANCEL/CONFIG/LIFETIME PASS\nSCENARIO COMPLETE");return 1;
}
#endif
