#ifdef HMM2_PACKED
static bool locale_game_active,locale_game_english;
static uint64_t locale_game_started;
static int locale_game_previous;
static bool hmm_test_locale_game_events(){
    if(!locale_game_active)return false;
    const int step=int((hmm_millis()-locale_game_started)/16);
    if(step==locale_game_previous)return true;locale_game_previous=step;
    if(step>30)throw std::runtime_error("in-game language click sequence timed out");
    SDL_Event e{};
    auto move=[&](int x,int y){e.type=SDL_MOUSEMOTION;e.motion.x=x;e.motion.y=y;SDL_PushEvent(&e);};
    auto button=[&](int x,int y,bool down){e={};e.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;e.button.state=down?SDL_PRESSED:SDL_RELEASED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);};
    const int selectedY=locale_game_english?208:234;
    if(step==1)move(228,110);if(step==2)button(228,110,true);if(step==3)button(228,110,false);
    if(step==5)move(320,selectedY);if(step==6)button(320,selectedY,true);if(step==7)button(320,selectedY,false);
    if(step==9)move(240,305);if(step==10)button(240,305,true);if(step==11)button(240,305,false);
    if(step==13){phase(locale_game_english?"locale_game_en_settings":"locale_game_ru_settings");move(320,414);}
    if(step==14)button(320,414,true);if(step==15)button(320,414,false);
    return true;
}
static int hmm_test_locale_game(){
    auto &conf=Settings::Get();auto &display=fheroes2::Display::instance();auto &ui=Interface::AdventureMap::Get();
    if(!conf.getGameLanguage().empty())throw std::runtime_error("in-game locale test starts in English");
    ui.getStatusPanel().SetState(Interface::StatusType::STATUS_DAY);
    phase("locale_game_initial");ui.redraw(Interface::REDRAW_STATUS);display.render();
    for(bool english:{false,true}){
        phase(english?"locale_game_return_en":"locale_game_choose_ru");locale_game_active=true;locale_game_english=english;
        locale_game_started=hmm_millis();locale_game_previous=-1;
        fheroes2::showSystemOptionsDialog();locale_game_active=false;
        if(conf.getGameLanguage()!=(english?"":"ru"))throw std::runtime_error("in-game language selection");
        SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
        phase(english?"locale_game_en_map":"locale_game_ru_map");ui.redraw(Interface::REDRAW_ALL);display.render();
    }
    std::puts("IN-GAME LANGUAGE RU/EN CLICK/SAVE COMPLETE\nSCENARIO COMPLETE");return 1;
}
#endif
