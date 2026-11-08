/* Codex: оригинальный WAIT и движение мыши отдельно от слоёв экрана. */
#include "hmm_loading.h"
#include "hmm_audio.h"
#include "hmm_platform.h"
#include "agg_image.h"
#include "cursor.h"
#include "game_mainmenu_ui.h"
#include "icn.h"
#include "localevent.h"
#include "screen.h"
#include "settings.h"
#include "ui_text.h"
#include <cstdio>
extern "C" void hmm_loading_mouse_begin(int x,int y);
namespace {unsigned depth,ticks;uint32_t last_tick,started,max_gap;bool servicing;}
// Клод: часы задачи курсора (hmm_gpu.cpp): своя копия, потому что кэш ICN может быть отдан под игру.
const fheroes2::Image &hmm_wait_cursor_image(){
    static const fheroes2::Sprite *image=new fheroes2::Sprite(fheroes2::AGG::GetICN(ICN::ADVMCO,0xff&Cursor::WAIT));
    return *image;
}
// Клод: рамка окна сообщения оригинала (Dialog::FrameBox: верх и низ BUYBUILD, стык посередине) и строка в ней.
// Сам FrameBox не годится: его деструктор восстанавливает фон и показывает его, а окно нужно до конца паузы.
static void message_frame(fheroes2::Image &out,int cx,int cy,const char *message){
    const int frame=Settings::Get().isEvilInterfaceEnabled()?ICN::BUYBUILE:ICN::BUYBUILD;
    const auto &leftTop=fheroes2::AGG::GetICN(frame,4),&rightTop=fheroes2::AGG::GetICN(frame,0),&leftBottom=fheroes2::AGG::GetICN(frame,6),&rightBottom=fheroes2::AGG::GetICN(frame,2);
    fheroes2::Blit(leftTop,out,cx-leftTop.width(),cy-leftTop.height());fheroes2::Blit(rightTop,out,cx,cy-rightTop.height());
    fheroes2::Blit(leftBottom,out,cx-leftBottom.width(),cy);fheroes2::Blit(rightBottom,out,cx,cy);
    const fheroes2::Text text(message,fheroes2::FontType::normalWhite());
    text.draw(cx-text.width()/2,cy-text.height()/2,out);
}
void hmm_status_show(const char *message,bool once){
    // Клод: только список слоёв: буфер пикселей такому изображению не нужен, в PSRAM его нет.
    struct StatusImage:fheroes2::Image{StatusImage(int w,int h){_hmmSetDimensions(w,h,false);}};
    static StatusImage *image;delete image;
    const int w=384,h=320;image=new StatusImage(w,h);
    message_frame(*image,w/2,h/2,message);
    auto &screen=fheroes2::Display::instance();
    hmm_gpu_status(image,(screen.width()-w)/2,(screen.height()-h)/2,once);screen.render();
}
void hmm_status_clear(){hmm_gpu_status(nullptr,0,0,false);}
// Клод: «Map loading...» поверх первого кадра карты после загрузки мира — пока он не станет играбельным (hmm_gpu_status_hold).
void hmm_status_hold(const char *message){
    if(!hmm_world_take_fresh())return;
    struct StatusImage:fheroes2::Image{StatusImage(int w,int h){_hmmSetDimensions(w,h,false);}};
    static StatusImage *image;delete image;
    const int w=384,h=320;image=new StatusImage(w,h);
    message_frame(*image,w/2,h/2,message);
    auto &screen=fheroes2::Display::instance();hmm_gpu_status_hold(image,(screen.width()-w)/2,(screen.height()-h)/2);
}
HmmLoadingScope::HmmLoadingScope(const char *message){
    if(depth++)return;
    last_tick=uint32_t(hmm_millis());servicing=true;
    hmm_log("LOAD CURSOR begin");
    const auto &mouse=LocalEvent::Get().getMouseCursorPos();hmm_loading_mouse_begin(mouse.x,mouse.y);
    theme=Cursor::Get().Themes();visible=fheroes2::cursor().isVisible();
    Cursor::Get().SetThemes(Cursor::WAIT);fheroes2::cursor().show(true);
    // Codex: сообщение публикуется до чтения SD; движения мыши меняют только курсор.
    servicing=true;
    if(message){
        // Codex: после fade-out окно выбора восстанавливает только свой прямоугольник.
        // Перед публикацией WAIT заново рисуем весь исходный фон меню, без масштабирования ROI.
        fheroes2::drawMainMenuScreen();
        auto &screen=fheroes2::Display::instance();message_frame(screen,screen.width()/2,screen.height()/2,message);}
    // Codex: загрузить атлас часов до обслуживания незавершённых запросов SD.
    servicing=true;fheroes2::Display::instance().render();servicing=false;last_tick=uint32_t(hmm_millis());
}
HmmLoadingScope::~HmmLoadingScope(){
    if(--depth)return;
    hmm_log("LOAD CURSOR end");
    Cursor::Get().SetThemes(theme);fheroes2::cursor().show(visible);
}
extern "C" void hmm_loading_tick(){
    // Клод: задача звука читает карту тем же read_block; курсор, события и загрузка принадлежат потоку игры.
    if(hmm_audio_self())return;
#ifndef HMM2_HOST
    // Клод: курсор и звук в ELF живут своими задачами, здесь только положение мыши для игры. Часы на каждый вызов — 28 % команд загрузки новой карты (трасса QEMU).
    {static uint8_t calls;if(++calls&31)return;}
#endif
    // Codex: звук обслуживается и при чтении меню/атласов до появления Map loading.
    // В незавершённом запросе SD hmm_audio_busy запрещает вложенный обмен.
    hmm_audio_poll();
    if(!depth||servicing)return;
    const auto now=uint32_t(hmm_millis());if(uint32_t(now-last_tick)<16)return;
    last_tick=now;servicing=true;
    // Клод: на экране курсор двигает своя задача; здесь только положение для игры, без клавиш и щелчков.
    int x,y;hmm_loading_mouse(&x,&y);
    LocalEvent::Get().hmmLoadingMouse(x,y);
    last_tick=uint32_t(hmm_millis());servicing=false;
}
