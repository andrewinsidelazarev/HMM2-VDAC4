#include "translations.h"
#include "ui_constants.h"
static bool file_quit_active,file_quit_mouse;
static unsigned file_quit_events;
static int file_quit_answer;
static fheroes2::Rect file_quit_area;
static void phase(const char *);
static bool hmm_file_quit_events(){
    if(!file_quit_active)return false;
    if(++file_quit_events>200)throw std::runtime_error("file-menu exit input timeout");
    SDL_Event e{};
    auto key=[&](SDL_Keycode value){e={};e.type=SDL_KEYDOWN;e.key.keysym.sym=value;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);};
    const int x=file_quit_area.x+file_quit_area.width/2,y=file_quit_area.y+file_quit_area.height/2;
    if(file_quit_events==5){
        if(file_quit_answer<0)key(SDLK_ESCAPE);
        else if(!file_quit_mouse)key(SDLK_q);
        else {e.type=SDL_MOUSEMOTION;e.motion.x=x;e.motion.y=y;SDL_PushEvent(&e);e={};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_PRESSED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);}
    }
    if(file_quit_events==7&&file_quit_mouse&&file_quit_answer>=0){e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_RELEASED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);}
    if(file_quit_events==15)key(file_quit_answer?SDLK_RETURN:SDLK_ESCAPE);
    if(file_quit_events==30&&file_quit_answer==0)key(SDLK_ESCAPE);
    return true;
}
static int hmm_test_file_quit(){
    auto &conf=Settings::Get();auto &display=fheroes2::Display::instance();unsigned cases=0;
    for(const char *language:{"","ru"})for(bool evil:{false,true}){
        conf.setGameLanguage(language);conf.setInterfaceType(evil?InterfaceType::EVIL:InterfaceType::GOOD);
        if(*language&&std::strcmp(_("Click to return to the main menu."),"Click to return to the main menu.")==0)throw std::runtime_error("file-menu tooltip is not translated");
        for(bool mouse:{false,true})for(int answer:{1,0,-1}){
            phase("file_quit");fheroes2::drawMainMenuScreen();display.render();
            {fheroes2::ButtonGroup buttons(evil?ICN::BUTTONS_FILE_DIALOG_EVIL:ICN::BUTTONS_FILE_DIALOG_GOOD);
             fheroes2::StandardWindow probe(buttons,false,0,display);probe.renderSymmetricButtons(buttons,0,false);file_quit_area=buttons.button(5).area();}
            std::printf("FILEQUIT AREA evil=%d x=%d y=%d w=%d h=%d\n",evil,file_quit_area.x,file_quit_area.y,file_quit_area.width,file_quit_area.height);
            file_quit_events=0;file_quit_mouse=mouse;file_quit_answer=answer;file_quit_active=true;
            const auto mode=Dialog::FileOptions();file_quit_active=false;
            const auto expected=answer==1?fheroes2::GameMode::MAIN_MENU:fheroes2::GameMode::CANCEL;
            if(mode!=expected)throw std::runtime_error("file-menu Exit returned wrong game mode");
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);fheroes2::drawMainMenuScreen();display.render();
            std::printf("FILEQUIT PASS lang=%s evil=%d mouse=%d answer=%d mode=%d\n",*language?language:"en",evil,mouse,answer,int(mode));++cases;
        }
    }
    std::printf("FILEQUIT ALL %u PASS\nSCENARIO COMPLETE\n",cases);return 1;
}
static bool main_exit_active,main_exit_mouse;
static unsigned main_exit_events;
static uint64_t main_exit_started;
static fheroes2::Rect main_exit_cancel;
static bool hmm_main_exit_events(){
    if(!main_exit_active)return false;
    const auto elapsed=hmm_millis()-main_exit_started;
    if(elapsed>6000)throw std::runtime_error("main-menu exit Cancel input timeout");
    SDL_Event e{};
    auto key=[&](SDL_Keycode v){e={};e.type=SDL_KEYDOWN;e.key.keysym.sym=v;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);};
    if(elapsed>=1000&&main_exit_events==0){key(SDLK_q);main_exit_events=1;}
    const int x=main_exit_cancel.x+main_exit_cancel.width/2,y=main_exit_cancel.y+main_exit_cancel.height/2;
    if(elapsed>=1500&&main_exit_events==1){
        main_exit_events=2;
        if(!main_exit_mouse)key(SDLK_ESCAPE);
        else {e.type=SDL_MOUSEMOTION;e.motion.x=x;e.motion.y=y;SDL_PushEvent(&e);e={};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_PRESSED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);}
    }
    if(elapsed>=1550&&main_exit_events==2){main_exit_events=3;if(main_exit_mouse){e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_RELEASED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);}}
    if(elapsed>=2200&&main_exit_events==3){main_exit_events=4;key(SDLK_n);}
    return true;
}
static int hmm_test_main_exit(){
    auto &conf=Settings::Get();auto &display=fheroes2::Display::instance();unsigned count=0;
    for(const char *language:{"","ru"})for(bool mouse:{false,true}){
        conf.setGameLanguage(language);conf.setInterfaceType(InterfaceType::GOOD);
        if(*language&&(std::strcmp(_("Exit"),"Exit")==0||std::strcmp(_("Cancel"),"Cancel")==0))throw std::runtime_error("main-menu exit not translated");
        const std::string caption=std::string("ESC - ")+_("Cancel");
        {fheroes2::Sprite released,pressed;fheroes2::Point a,z;fheroes2::getCustomNormalButton(released,pressed,false,{190,30},a,z,ICN::STONEBAK);
         const fheroes2::Rect area((display.width()-fheroes2::boxAreaWidthPx)/2,(display.height()-100)/2,fheroes2::boxAreaWidthPx,100);
         const char *label=*language?_("Main menu|Quit"):"Exit";const fheroes2::Text title(label,fheroes2::FontType::normalYellow()),body(std::string("F12 - ")+label,fheroes2::FontType::normalWhite());
         fheroes2::ButtonSprite cancel(0,0,std::move(released),std::move(pressed));const int gap=(area.height-title.height()-body.height()-cancel.area().height)/4;
         cancel.setPosition(area.x+(area.width-cancel.area().width)/2,area.y+3*gap+title.height()+body.height());main_exit_cancel=cancel.area();
         const int bottom=area.y+area.height-main_exit_cancel.y-main_exit_cancel.height;if(gap<0||bottom<gap||bottom-gap>3||main_exit_cancel.y>=257)throw std::runtime_error("Exit spacing not balanced/raised");
         const fheroes2::Text text(caption,fheroes2::FontType::normalWhite());if(text.width()>main_exit_cancel.width-8)throw std::runtime_error("Exit Cancel caption clipped");}
        std::printf("MAINEXIT CANCEL lang=%s x=%d y=%d w=%d h=%d\n",*language?language:"en",main_exit_cancel.x,main_exit_cancel.y,main_exit_cancel.width,main_exit_cancel.height);
        main_exit_events=0;main_exit_mouse=mouse;main_exit_started=hmm_millis();main_exit_active=true;const auto mode=Game::MainMenu(false);main_exit_active=false;
        if(mode!=fheroes2::GameMode::NEW_GAME)throw std::runtime_error("main-menu Cancel terminated game or did not return to menu");
        SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);display.render();
        std::printf("MAINEXIT PASS lang=%s mouse=%d mode=%d\n",*language?language:"en",mouse,int(mode));++count;
    }
    std::printf("MAINEXIT ALL %u PASS\nSCENARIO COMPLETE\n",count);return 1;
}
