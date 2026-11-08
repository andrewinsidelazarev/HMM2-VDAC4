#include "hmm_clock.h"
extern "C" {
#include "hmm_core.h"
}
/* Codex: сценарий исполняет исходные карту, движение, город и бой; в ELF игры не входит. */
#include "game.h"
#include "game_interface.h"
#include "game_mainmenu_ui.h"
#include "game_delays.h"
#include "game_video.h"
#include "game_video_type.h"
#include "interface_gamearea.h"
#include "settings.h"
#include "maps_fileinfo.h"
#include "world.h"
#include "kingdom.h"
#include "castle.h"
#include "army.h"
#include "monster.h"
#include "battle_arena.h"
#include "battle_interface.h"
#include "battle_troop.h"
#include "battle_command.h"
#include "rand.h"
#include "screen.h"
#include "players.h"
#include "heroes.h"
#include "route.h"
#include "maps_tiles.h"
#include "maps.h"
#include "maps_tiles_helper.h"
#include "map_object_info.h"
#include "serialize.h"
#include "dir.h"
#include "game_io.h"
#include "world_regions.h"
#include "view_world.h"
#include "puzzle.h"
void hmm_host_capture_cached_cursor();
void hmm_host_prefetch_probe();
#include "ui_castle.h"
#include "dialog_hotkeys.h"
#include "dialog_system_options.h"
#include "agg_image.h"
#include "icn.h"
#include "image_palette.h"
#include "pal.h"
#include <chrono>
#include "cursor.h"
#include "localevent.h"
#ifdef HMM2_PACKED
#include "hmm_loading.h"
#include "dialog.h"
#include "ui_dialog.h"
#include "ui_window.h"
#include "spell_book.h"
#endif
#include <algorithm>
#include <cctype>
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
const char *hmm_test_phase="menu";
static bool closing,probe_hotkeys_updates;static unsigned events;
static bool choose_auto_mode;
void hmm_host_reject_unwind_alloc(bool);
void hmm_host_reject_restore_alloc(bool);
// Codex: десять виртуальных минут исходного ожидания в бою отделяют первый
// показ редких фаз анимации от повторной распаковки вытесненного кадра.
static uint64_t battle_idle_until;
extern "C" void hmm_cache_trim();
static bool forced_trim;
struct IdleDone {};
uint64_t hmm_millis();
static bool recruit_repeat,in_recruit;static unsigned recruit_opens,recruit_events,town_events;static SDL_Keycode recruit_key=SDLK_1;
void hmm_test_recruit_enter(){if(recruit_repeat){in_recruit=true;recruit_events=0;++recruit_opens;std::printf("RECRUIT OPEN %u\n",recruit_opens);std::fflush(stdout);}}
void hmm_test_recruit_leave(){if(recruit_repeat){in_recruit=false;std::printf("RECRUIT CLOSE %u\n",recruit_opens);std::fflush(stdout);}}
// Клод: HMM2_TEST_PLAY — настоящий цикл карты на сейве пользователя: конец хода клавишей, Enter закрывает окна; ходы врагов идут как в игре.
static bool scene_play;
unsigned hmm_host_frame_count();
static unsigned idle_dismissed;
struct RouteDone {};
static Heroes *route_hero;static int route_start,route_target;static bool route_started,route_triggered;
void hmm_scene_events(){
    if(choose_auto_mode){
        if(++events==5){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_a;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}
        if(events>1000)throw std::runtime_error("auto mode dialog input timeout");
        return;
    }
    if(route_hero){
        if(++events>20000)throw std::runtime_error("route probe timed out");
        if(hmm_millis()<1000)return;
        if(route_hero->GetIndex()==route_target||(route_started&&route_hero->GetIndex()!=route_start&&!route_hero->Modes(Heroes::ENABLEMOVE))){
            const bool complete=route_hero->GetIndex()==route_target;
            std::printf("ROUTE RESULT complete=%u start=%d end=%d target=%d green_left=%d injected=%u\n",unsigned(complete),route_start,route_hero->GetIndex(),route_target,route_hero->GetPath().GetAllowedSteps(),unsigned(route_triggered));
            const bool expect_stop=std::getenv("HMM2_ROUTE_EXPECT_STOP")!=nullptr;
            if(complete==expect_stop)throw std::runtime_error("hero stopped before end of green route or failed to cancel");
            route_hero->SetMove(false);throw RouteDone{};
        }
        SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_m;
        if(!route_started){route_started=true;SDL_PushEvent(&e);}
        else if(!route_triggered&&route_hero->GetIndex()!=route_start){
            route_triggered=true;e.key.repeat=1;
            if(std::getenv("HMM2_ROUTE_CANCEL")){e.key.repeat=0;e.key.keysym.sym=SDLK_SPACE;}
            if(std::getenv("HMM2_ROUTE_RELEASE")){e={};e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_RELEASED;}
            if(std::getenv("HMM2_ROUTE_MOUSE_STOP")){e={};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.state=SDL_PRESSED;}
            if(std::getenv("HMM2_ROUTE_REPRESS")){SDL_Event up=e;up.type=SDL_KEYUP;SDL_PushEvent(&up);e.key.repeat=0;}
            SDL_PushEvent(&e);
        }
        return;
    }
    if(battle_idle_until&&hmm_millis()>=battle_idle_until)throw IdleDone{};
    // Codex: аварийный сброс в середине прогретого боя проверяет возврат к обычной скорости.
    if(battle_idle_until&&!forced_trim&&std::getenv("HMM2_TEST_FORCE_TRIM")&&hmm_millis()+300000>=battle_idle_until){
        forced_trim=true;std::printf("FORCED TRIM time=%llu\n",static_cast<unsigned long long>(hmm_millis()));hmm_cache_trim();
    }
    if(scene_play){++events;const bool idle=std::getenv("HMM2_TEST_PLAY_IDLE")!=nullptr;
        const bool dismiss=idle&&idle_dismissed<3&&hmm_host_frame_count()>=(idle_dismissed+1)*50;if(dismiss)++idle_dismissed;
        SDL_Keycode key=!(events%3000)&&!idle?SDLK_e:(!idle?!(events%200):dismiss)?SDLK_RETURN:SDLK_UNKNOWN;
        if(key!=SDLK_UNKNOWN){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}return;}
    if(std::getenv("HMM2_TEST_CHOOSE")&&!(++events%1500)){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_1;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);return;}
    if(recruit_repeat){
        SDL_Keycode key=SDLK_UNKNOWN;
        if(in_recruit){if(++recruit_events==15)key=recruit_opens==1&&!std::getenv("HMM2_TEST_RECRUIT_CANCEL_ONLY")?SDLK_RETURN:SDLK_ESCAPE;}
        else {if(++town_events>1500)throw std::runtime_error("repeated recruit did not open dwelling");if(town_events%15==3)key=recruit_opens<12?recruit_key:SDLK_ESCAPE;}
        if(key!=SDLK_UNKNOWN){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}return;
    }
    if(probe_hotkeys_updates){
        static uint64_t start;static int previous;
        if(!events++){start=hmm_millis();previous=-1;}
        const int step=int((hmm_millis()-start)/16);if(step==previous)return;previous=step;SDL_Event e{};
        auto move=[&](int x,int y,bool held=false){e.type=SDL_MOUSEMOTION;e.motion.x=x;e.motion.y=y;e.motion.state=held?SDL_BUTTON_LMASK:0;SDL_PushEvent(&e);};
        auto button=[&](int x,int y,bool down){e={};e.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;e.button.state=down?SDL_PRESSED:SDL_RELEASED;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);};
        if(step==1)move(400,220);
        if(step==2||step==4){e.type=SDL_MOUSEWHEEL;e.wheel.y=step==2?-1:1;e.wheel.direction=SDL_MOUSEWHEEL_NORMAL;SDL_PushEvent(&e);}
        if(step==6){move(543,260);button(543,260,true);}if(step==7)button(543,260,false);
        if(step==9)move(543,240);if(step==10)button(543,240,true);
        if(step==11)move(543,270,true);if(step==12)move(543,300,true);if(step==13)button(543,300,false);
        if(step==15)move(320,414);if(step==16)button(320,414,true);if(step==18)button(320,414,false);
        if(step>25)throw std::runtime_error("Hot Keys did not close on the first OK click");
        return;
    }
    if(closing&&events==99&&std::getenv("HMM2_TEST_ADVENTURE_AUDIT"))hmm_host_capture_cached_cursor();
    if(closing&&++events>100&&events%2==0){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}}
static void phase(const char *s){hmm_test_phase=s;std::printf("SCENE %s\n",s);std::fflush(stdout);}
static void state(const char *s){RWStreamBuf stream;stream<<world;uint32_t hash=2166136261u;for(size_t i=0;i<stream.size();i++)hash=(hash^stream.data()[i])*16777619u;std::printf("STATE %s bytes=%zu hash=%08x\n",s,stream.size(),hash);std::fflush(stdout);
    if(const char *folder=std::getenv("HMM2_STATE_OUT")){std::string name=s;std::replace(name.begin(),name.end(),'/','_');
        auto save=[&](const char*extension,const RWStreamBuf&buf){FILE*f=std::fopen((std::string(folder)+"/"+name+extension).c_str(),"wb");if(!f||std::fwrite(buf.data(),1,buf.size(),f)!=buf.size())throw std::runtime_error("state write");std::fclose(f);};save(".bin",stream);
        RWStreamBuf regions;for(int i=0;i<world.w()*world.h();i++)regions<<uint32_t(world.getTile(i).GetRegion());
        regions<<uint32_t(world.getRegionCount());size_t capacity=0;
        for(size_t i=0;i<world.getRegionCount();i++){const auto&r=world.getRegion(i);regions<<r._id<<r._isWater<<uint32_t(r._neighbours.size());for(uint32_t neighbor:r._neighbours)regions<<neighbor;capacity+=r._nodes.capacity()*sizeof(MapRegionNode);}
        save(".regions",regions);std::printf("REGION_QUEUE %s bytes=%zu\n",s,capacity);
    }
}
void hmm_host_capture_cached_cursor();
void hmm_host_arm_torn_command();
bool hmm_host_torn_command_fired();
// Codex: координаты исходной карты для проверки маршрута стенда; состояние игры не меняется.
static void dump_map(){
    if(!std::getenv("HMM2_TEST_DUMP_MAP"))return;
    // Codex: число вступительных сообщений нужно сценарию настоящего ввода.
    if(!world.CountDay())world.NewDay();
    const auto dayEvents=world.GetEventsDate(PlayerColor::BLUE);
    std::printf("MAP DAY EVENTS count=%zu\n",dayEvents.size());
    for(const auto &event:dayEvents)std::printf("MAP DAY EVENT %.80s\n",event.message.c_str());
    std::printf("MAP COORDS width=%d height=%d day=%u\n",world.w(),world.h(),world.CountDay());
    for(auto color:PlayerColorsVector(Settings::Get().GetPlayers().GetColors()))
        for(const Heroes *hero:world.GetKingdom(color).GetHeroes())
            std::printf("MAP HERO %s index=%d x=%d y=%d\n",hero->GetName().c_str(),hero->GetIndex(),hero->GetIndex()%world.w(),hero->GetIndex()/world.w());
    for(int i=0;i<world.w()*world.h();++i){
        const auto &tile=world.getTile(i);
        if(tile.getMainObjectType()==MP2::OBJ_MONSTER)
            std::printf("MAP MONSTER index=%d x=%d y=%d id=%d count=%u\n",i,i%world.w(),i/world.w(),Maps::getMonsterFromTile(tile).GetID(),Maps::getMonsterCountFromTile(tile));
    }
    std::fflush(stdout);
}
extern "C" int hmm_scene_run(){
    const char *map=std::getenv("HMM2_TEST_MAP");if(!map)return 0;
    auto &conf=Settings::Get();auto &display=fheroes2::Display::instance();
    Rand::CurrentThreadRandomDevice()=Rand::PCG32(12345,6789);
    phase("menu");fheroes2::drawMainMenuScreen();display.render();
    // Codex: чтение настоящего CFG и штатная запись проверяют нормализацию скорости.
    if(const char *expected=std::getenv("HMM2_TEST_SCROLL_SETTING")){
        if(conf.ScrollSpeed()!=std::atoi(expected))throw std::runtime_error("scroll setting mismatch");
        if(!conf.Save("fheroes2.cfg"))throw std::runtime_error("scroll setting save failed");
        std::printf("SCROLL SETTING %d saved\nSCENARIO COMPLETE\n",conf.ScrollSpeed());return 1;
    }
    // Клод: HMM2_TEST_CHOOSE — настоящий экран выбора кампании: INTRO, затем петля CHOOSE; клавиша 1 подаётся из hmm_scene_events.
    if(std::getenv("HMM2_TEST_CHOOSE")){
        const auto mode=Game::NewSuccessionWarsCampaign();display.render();fheroes2::drawMainMenuScreen();display.render();
        std::printf("CHOOSE mode=%d\n",int(mode));std::puts("SCENARIO COMPLETE");std::fflush(stdout);return 1;
    }
    // Клод: HMM2_TEST_CLIP=A.SMK — ролик, A.SMK+B.SMK:x:y — фон с роликом поверх, как в кампании. Настоящий ShowVideo.
    if(const char *clip=std::getenv("HMM2_TEST_CLIP")){
        std::string text=clip;std::vector<Video::VideoInfo> infos;const auto plus=text.find('+');
        if(plus==std::string::npos)infos.push_back({text,Video::VideoControl::PLAY_CUTSCENE});
        else {const auto colon=text.find(':',plus);const auto second=text.find(':',colon+1);
            infos.push_back({text.substr(0,plus),Video::VideoControl::PLAY_VIDEO_LOOP});
            infos.push_back({text.substr(plus+1,colon-plus-1),Video::VideoControl::PLAY_CUTSCENE,{std::atoi(text.c_str()+colon+1),std::atoi(text.c_str()+second+1)}});}
        const bool shown=Video::ShowVideo(infos);display.render();
        fheroes2::drawMainMenuScreen();display.render();
        std::printf("CLIP shown=%d\n",int(shown));std::puts("SCENARIO COMPLETE");std::fflush(stdout);if(!shown)throw std::runtime_error("clip not shown");return 1;
    }
    if(const char *save=std::getenv("HMM2_TEST_LOAD_EXTERNAL")){
        conf.SetGameType(Game::TYPE_STANDARD|Game::TYPE_HOTSEAT);
        if(Game::Load(save)!=fheroes2::GameMode::START_GAME)throw std::runtime_error("target save load failed");
        RWStreamBuf raw;raw.setBigendian(true);raw<<world;
        const std::string path=std::string(std::getenv("HMM2_STATE_OUT"))+"/target_world.be";
        FILE*f=std::fopen(path.c_str(),"wb");if(!f||std::fwrite(raw.data(),1,raw.size(),f)!=raw.size())throw std::runtime_error("target world write");std::fclose(f);
        state("target_save");dump_map();std::printf("TARGET SAVE DAY=%u\n",world.CountDay());std::puts("TARGET SAVE LOAD COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_ALLMAPS")){
        ListFiles maps;maps.ReadDir("maps",".mp2");maps.ReadDir("maps",".mx2");
        for(auto &name:maps)std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});maps.sort();
        unsigned count=0;
        for(const auto &name:maps){
            Rand::CurrentThreadRandomDevice()=Rand::PCG32(12345,6789);Maps::FileInfo item;
            if(!item.readMP2Map(name,false))throw std::runtime_error("all maps info");
            conf.SetGameType(Game::TYPE_STANDARD);conf.setCurrentMapInfo(item);conf.GetPlayers().SetStartGame();
            if(!world.LoadMapMP2(item.filename,item.version==GameVersion::SUCCESSION_WARS))throw std::runtime_error("all maps load");
            state(name.c_str());++count;
        }
        std::printf("ALL MAPS count=%u\nSCENARIO COMPLETE\n",count);return 1;
    }
    if(std::getenv("HMM2_EVIL_UI"))conf.setInterfaceType(InterfaceType::EVIL);
    phase("load");Maps::FileInfo info;
    if(const char *saved=std::getenv("HMM2_TEST_RECRUIT_SAVE")){
        conf.SetGameType(Game::TYPE_STANDARD);
        if(!Game::LoadSAV2FileInfo(saved,info))throw std::runtime_error("user autosave header");
        std::printf("USER SAVE map=%s name=%s epoch=%u\n",info.filename.c_str(),info.name.c_str(),info.timestamp);
        if(Game::Load(saved)!=fheroes2::GameMode::START_GAME)throw std::runtime_error("user autosave load");
    }else {
        if(!info.readMP2Map(map,false))throw std::runtime_error("test map info");
        conf.SetGameType(Game::TYPE_STANDARD);conf.setCurrentMapInfo(info);conf.GetPlayers().SetStartGame();
        if(!world.LoadMapMP2(info.filename,info.version==GameVersion::SUCCESSION_WARS))throw std::runtime_error("test map load");
    }
    state("loaded");
    if(std::getenv("HMM2_TEST_DUMP_MAP")){dump_map();std::puts("SCENARIO COMPLETE");return 1;}
    if(std::getenv("HMM2_TEST_PLAY")){
        phase("play");scene_play=true;events=0;Interface::AdventureMap::Get().StartGame();scene_play=false;
        std::puts("PLAY SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    auto colors=PlayerColorsVector(conf.GetPlayers().GetColors());if(colors.empty())throw std::runtime_error("test map players");
    // Codex: выбор существующего героя для поиска безопасного маршрута стенда.
    const unsigned heroIndex=std::getenv("HMM2_TEST_HERO_INDEX")?unsigned(std::atoi(std::getenv("HMM2_TEST_HERO_INDEX"))):0;
    // Codex: плотная сцена Alteris начинается с настоящего героя, а не с первого цвета без героев.
    if(std::getenv("HMM2_TEST_HERO_FOCUS")){
        auto at=std::find_if(colors.begin(),colors.end(),[](auto color){return !world.GetKingdom(color).GetHeroes().empty();});
        if(at==colors.end())throw std::runtime_error("hero focus requires hero");
        std::rotate(colors.begin(),at,at+1);
        const auto &hero=*world.GetKingdom(colors.front()).GetHeroes().at(heroIndex);
        std::printf("FOCUS HERO name=%s tile=%d xy=%d,%d\n",hero.GetName().c_str(),hero.GetIndex(),hero.GetCenter().x,hero.GetCenter().y);
    }
    conf.SetCurrentColor(colors.front());
    if(std::getenv("HMM2_TEST_HERO_FOCUS")){
        // Codex: те же открытие тумана и начало дня, что AdventureMap::StartGame.
        world.ClearFog(colors.front());Interface::GameArea::updateMapFogDirections();world.NewDay();
    }
    phase("map");auto &ui=Interface::AdventureMap::Get();ui.reset();ui.ResetFocus(0,false);
    if(std::getenv("HMM2_TEST_HERO_FOCUS")){auto *hero=world.GetKingdom(colors.front()).GetHeroes().at(heroIndex);ui.SetFocus(hero,false);ui.getGameArea().SetCenter(hero->GetCenter());}
    ui.redraw(Interface::REDRAW_ALL);display.render();
    if(std::getenv("HMM2_MENU_HOTKEYS")){
        for(unsigned i=0;i<3;++i){phase("menu_background");fheroes2::drawMainMenuScreen();display.render();
            phase("hotkeys");closing=true;events=0;probe_hotkeys_updates=true;fheroes2::openHotkeysDialog();probe_hotkeys_updates=false;closing=false;
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);phase("hotkeys_closed");display.render();}
        std::puts("HOTKEYS MENU COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_HOTKEYS_WINDOW")){
        const int centers[][2]={{83,51},{88,52},{80,48},{87,49},{83,51},{88,52},{80,48},{87,49}};
        for(unsigned i=0;i<8;++i){
            if(i==4){for(int n=0;n<world.w()*world.h();++n)world.getTile(n).ClearFog(static_cast<PlayerColorsSet>(colors.front()));Interface::GameArea::updateMapFogDirections();}
            for(unsigned warm=0;warm<4;++warm){
                const auto &p=centers[(i+warm)%8];ui.getGameArea().SetCenter({p[0],p[1]});Game::updateAdventureMapAnimationIndex();ui.redraw(Interface::REDRAW_ALL);display.render();
            }
            phase("system_options");closing=true;events=0;fheroes2::showSystemOptionsDialog();closing=false;
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
            // No intervening map present: the real system-options handler
            // opens the hotkey dialog immediately after its menu is closed.
              phase("hotkeys");closing=true;events=0;probe_hotkeys_updates=std::getenv("HMM2_HOTKEYS_INTERACT")!=nullptr;
              fheroes2::openHotkeysDialog();probe_hotkeys_updates=false;closing=false;
              SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
              // The real dispatcher reopens settings immediately after Hot
              // Keys returns, without repainting the adventure map first.
              phase("system_return");closing=true;events=0;fheroes2::showSystemOptionsDialog();closing=false;
              SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);phase("hotkeys_closed");display.render();
        }
        std::puts("HOTKEYS WINDOWS COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_GUILD_TRANSITIONS")){
        for(bool oracle:{true,false}){
            phase(oracle?"guild_oracle":"guild_in_castle");closing=true;events=0;
            Dialog::ThievesGuild(oracle);closing=false;
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);phase("guild_closed");display.render();
        }
        std::puts("GUILD TRANSITIONS COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_SURFACE_ROLES")){
        for(unsigned variant=0;variant<2;++variant){
            phase("fullscreen_view");
            auto view=variant?std::make_unique<fheroes2::StandardWindow>(0,0,640,480,false,display):std::make_unique<fheroes2::StandardWindow>(640,480,false,display);
            fheroes2::Fill(display,0,0,640,480,42);display.render();
            phase("popup_in_fullscreen");closing=true;events=0;
            fheroes2::showStandardTextMessage("Popup","This popup retains its three-frame alpha transition inside a fullscreen view.",Dialog::OK);
            closing=false;phase("popup_closed");display.render();
            view.reset();phase("fullscreen_closed");display.render();
        }
        std::puts("SURFACE ROLES COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_CASTLE_TRANSITIONS")){
        auto &castles=world.GetKingdom(colors.front()).GetCastles();
        if(castles.empty())throw std::runtime_error("castle transition fixture needs castle");
        for(unsigned i=0;i<3;++i){
            phase("castle_enter");SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
            closing=true;events=0;Game::OpenCastleDialog(*castles.front(),true,true);closing=false;
            SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);phase("castle_exit");
            ui.redraw(0);
            if(Game::validateDisplayFadeIn())fheroes2::fadeInDisplay();else display.render();
            phase("castle_closed");display.render();
        }
        if(std::getenv("HMM2_TEST_LEGACY_VIEWPORT")){
            phase("legacy_viewport");
            fheroes2::Blit(fheroes2::AGG::GetICN(ICN::RESOURCE,6),display,600,440);
            hmm_gpu_fade(213);display.render();hmm_gpu_fade(0);display.render();
        }
        std::puts("CASTLE TRANSITIONS COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_SUBJECT_STEPS")){
        phase("subject_steps");auto *hero=Interface::GetFocusHeroes();if(!hero)throw std::runtime_error("subject probe needs hero");
        auto &area=ui.getGameArea();
        if(!std::getenv("HMM2_SUBJECT_KEEP_FOG")){for(int i=0;i<world.w()*world.h();++i)world.getTile(i).ClearFog(static_cast<PlayerColorsSet>(colors.front()));Interface::GameArea::updateMapFogDirections();}
        struct Pose {int direction,base,dx,dy;};
        const Pose poses[]={{Direction::TOP,0,0,-1},{Direction::TOP_RIGHT,9,1,-1},{Direction::RIGHT,18,1,0},{Direction::BOTTOM_RIGHT,27,1,1},
            {Direction::BOTTOM,36,0,1},{Direction::BOTTOM_LEFT,27,-1,1},{Direction::LEFT,18,-1,0},{Direction::TOP_LEFT,9,-1,-1}};
        for(const auto &p:poses){
            const int target=Maps::GetDirectionIndex(hero->GetIndex(),p.direction);if(target<0)throw std::runtime_error("subject pose outside map");
            hero->GetPath().setPath(std::list<Route::Step>{{target,hero->GetIndex(),p.direction,100}});hero->setDirection(p.direction);hero->SetMove(true);
            area.SetCenter(hero->GetCenter());const auto origin=area.getCurrentCenterInPixels();
            std::printf("SUBJECT direction=%d\n",p.direction);
            for(unsigned i=0;i<64;++i){if(!(i&15))Game::updateAdventureMapAnimationIndex();
                hero->SetSpriteIndex(p.base+int((i/4)%8));hero->SetOffset({p.dx*int(i&3),p.dy*int(i&3)});
                area.SetCenterInPixels({origin.x+p.dx*int(i%32),origin.y+p.dy*int(i%32)});
                {const HmmMapAnimation animation(i!=0);area.Redraw(display,Interface::LEVEL_ALL,false);}display.render();
            }
        }
        std::puts("SUBJECT STEPS COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_GREEN_ROUTE")){
        phase("green_route");route_hero=Interface::GetFocusHeroes();if(!route_hero)throw std::runtime_error("route probe needs hero");
        route_start=route_hero->GetIndex();route_target=-1;
        for(int target=0;target<world.w()*world.h();++target){
            const auto path=world.getPath(*route_hero,target);if(path.size()<5||path.size()>8)continue;
            bool safe=true;for(const auto &step:path)if(Maps::isTileUnderProtection(step.GetIndex())||MP2::isInGameActionObject(world.getTile(step.GetIndex()).getMainObjectType()))safe=false;
            if(!safe)continue;route_hero->GetPath().setPath(path);
            if(route_hero->GetPath().GetAllowedSteps()!=int(path.size()))continue;
            route_target=target;break;
        }
        if(route_target<0)throw std::runtime_error("no safe green route");
        std::printf("ROUTE START tile=%d target=%d green=%d points=%u\n",route_start,route_target,route_hero->GetPath().GetAllowedSteps(),route_hero->GetMovePoints());
        route_started=route_triggered=false;events=0;
        Game::setHotKeyForEvent(Game::HotKeyEvent::WORLD_START_HERO_MOVEMENT,fheroes2::Key::KEY_M);
        try{ui.hmmTestHumanTurn();}catch(const RouteDone&){}
        route_hero=nullptr;std::puts("GREEN ROUTE SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_CLIP_RESTORE")){
        phase("clip_restore_before");auto &area=ui.getGameArea();
        for(int i=0;i<world.w()*world.h();++i)world.getTile(i).ClearFog(static_cast<PlayerColorsSet>(colors.front()));
        Interface::GameArea::updateMapFogDirections();area.SetCenter({84,51});
        for(unsigned i=0;i<3;++i){area.Redraw(display,Interface::LEVEL_ALL,false);display.render();}
        std::printf("CLIP BEFORE frame=%u\n",hmm_host_frame_count());
        uint32_t words[1024];
        for(unsigned slot=0;slot<2;++slot)for(unsigned bank=0;bank<2;++bank){
            const auto address=hmm_gpu_clip_buffer(slot,bank,640,480);std::fill_n(words,1024,0x12345678u+slot+bank);
            for(unsigned at=0;at<640*480*2;at+=sizeof(words)){
                const unsigned n=std::min<unsigned>(sizeof(words),640*480*2-at)/4;
                if(hmm_core->ft_write(address+at,words,n)!=ZM_OK)throw std::runtime_error("clip fixture FT write");
            }
        }
        // A clip may abort after allocating its buffers, before the first frame.
        hmm_gpu_clip_end();phase("clip_restore_after");
        for(unsigned i=0;i<3;++i){ui.redraw(Interface::REDRAW_ALL);display.render();}
        std::printf("CLIP AFTER frame=%u\n",hmm_host_frame_count());std::puts("SCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_MONSTER_SCENE")){
        // Codex: первый вид Alteris со стартовым героем и исходным туманом — как на снимке Ево.
        phase("monsters");auto &area=ui.getGameArea();
        if(std::getenv("HMM2_MONSTER_REVEAL")){for(int i=0;i<world.w()*world.h();++i)world.getTile(i).ClearFog(static_cast<PlayerColorsSet>(colors.front()));Interface::GameArea::updateMapFogDirections();}
        if(const char *center=std::getenv("HMM2_MONSTER_CENTER")){int x,y;if(std::sscanf(center,"%d,%d",&x,&y)!=2)throw std::runtime_error("monster center");area.SetCenter({x,y});ui.redraw(Interface::REDRAW_ALL);display.render();}
        const auto origin=area.getCurrentCenterInPixels();
        const unsigned count=std::getenv("HMM2_MONSTER_FRAMES")?unsigned(std::atoi(std::getenv("HMM2_MONSTER_FRAMES"))):160;
        std::printf("MONSTER VIEW center=%d,%d frames=%u\n",origin.x,origin.y,count);
        const auto begun=std::chrono::steady_clock::now();
        const bool smooth=std::getenv("HMM2_MONSTER_SCROLL_SMOOTH")!=nullptr;
        for(unsigned i=0;i<count;++i){if(!smooth||!(i&15))Game::updateAdventureMapAnimationIndex();
            if(smooth){
                const int step=int(i%128);int dx=step<64?step*4:(127-step)*4;
                int dy=int((i/128)%3)*-32;
                if(std::getenv("HMM2_MONSTER_VERTICAL"))std::swap(dx,dy);
                if(std::getenv("HMM2_MONSTER_DIAGONAL"))dy=dx;
                area.SetCenterInPixels({origin.x+dx,origin.y+dy});
            }
            if(std::getenv("HMM2_MONSTER_SCROLL")){
                // Codex: граница тумана пересекает клетки и оборачивает полосы.
                static const int offsets[][2]={{0,0},{-1,0},{1,0},{0,-1},{0,1},{-3,-3},{3,3},{0,0}};
                const auto &offset=offsets[(i/16)%8];area.SetCenterInPixels({origin.x+offset[0]*32,origin.y+offset[1]*32});
            }
            if(std::getenv("HMM2_MONSTER_RECOVER")&&i>=count*3/4)area.SetCenter({0,0});
#ifdef HMM2_PACKED
            const HmmMapAnimation animation(i!=0);
#endif
            area.Redraw(display,Interface::LEVEL_ALL,false);display.render();
            if(std::getenv("HMM2_PREFETCH_STRESS")){
                if(!(i%17))hmm_host_prefetch_probe();
                if(i==64){closing=true;events=0;Dialog::GameInfo();closing=false;ui.redraw(Interface::REDRAW_ALL);display.render();}
            }
        }
        std::printf("MONSTER render_us=%lld\n",static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begun).count()));
        std::puts("MONSTER SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_WATER")){
        // Codex: неподвижный берег, исходный цикл воды; тот же DAT, что у сборки 99 на Ево.
        for(int i=0;i<world.w()*world.h();++i)world.getTile(i).ClearFog(static_cast<PlayerColorsSet>(colors.front()));
        Interface::GameArea::updateMapFogDirections();
        for(int y=4;y<world.h()-4;++y){bool found=false;for(int x=4;x<world.w()-4;++x)
            if(world.getTile(y*world.w()+x).isWater()){ui.getGameArea().SetCenter({x,y});found=true;break;}
            if(found)break;}
        phase("water");ui.redraw(Interface::REDRAW_ALL);display.render();
        uint32_t step=0;display.subscribe([&step](std::vector<uint8_t>&p){p=PAL::GetCyclingPalette(step++);return true;},{});
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<60;++i)display.render();
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        std::printf("WATER cycles=60 render_us=%lld\n",static_cast<long long>(us));
        phase("water_fill");fheroes2::Fill(display,20,20,32,24,214);display.render();
        for(unsigned i=0;i<4;++i)display.render();
        phase("water_redraw");ui.redraw(Interface::REDRAW_ALL);display.render();display.render();
        display.subscribe({},{});std::puts("WATER SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    // Клод: проверка длины списка FT812 (FINALDL при HMM2_MAPDL_STATS): обход карты — центр вида через HMM2_TEST_SWEEP клеток, каждый вид — кадр.
    if(const char *sweep=std::getenv("HMM2_TEST_SWEEP")){
        const int step=std::max(1,std::atoi(sweep));
        for(int y=step/2;y<world.h();y+=step)for(int x=step/2;x<world.w();x+=step){
            ui.getGameArea().SetCenter({x,y});ui.redraw(Interface::REDRAW_ALL);display.render();std::printf("SWEEP %d,%d\n",x,y);std::fflush(stdout);}
        std::puts("SCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_ROUTES")){
        // Codex: выбор щелчков использует исходный поиск пути и проверку охраны, без изменения мира.
        const auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
        if(heroes.empty())throw std::runtime_error("route probe requires hero");
        const auto &hero=*heroes.at(heroIndex);const auto &area=ui.getGameArea();
        for(int y=32;y<464;y+=32)for(int x=32;x<464;x+=32){
            const auto target=area.GetValidTileIdFromPoint({x,y});if(target<0)continue;
            const auto path=world.getPath(hero,target);if(path.empty())continue;
            bool guarded=false;for(const auto &step:path)if(Maps::isTileUnderProtection(step.GetIndex()))guarded=true;
            std::printf("ROUTE screen=%d,%d target=%d steps=%zu object=%u guarded=%u\n",x,y,target,path.size(),unsigned(world.getTile(target).getMainObjectType()),unsigned(guarded));
        }
        // Codex: дальние цели для радара; маршрут не проходит через охрану или действующие объекты.
        for(int dy=-12;dy<=12;dy+=4)for(int dx=-12;dx<=12;dx+=4){
            const int x=hero.GetCenter().x+dx,y=hero.GetCenter().y+dy;
            if(x<0||y<0||x>=world.w()||y>=world.h())continue;
            const int target=y*world.w()+x;const auto path=world.getPath(hero,target);
            if(path.size()<5||path.size()>16||MP2::isInGameActionObject(world.getTile(target).getMainObjectType()))continue;
            bool safe=true;for(const auto &step:path)if(Maps::isTileUnderProtection(step.GetIndex())||MP2::isInGameActionObject(world.getTile(step.GetIndex()).getMainObjectType()))safe=false;
            if(safe)std::printf("FAR ROUTE xy=%d,%d target=%d steps=%zu delta=%d,%d\n",x,y,target,path.size(),dx,dy);
        }
        std::puts("SCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_LOADING_CURSOR")){
        phase("loading_cursor");Cursor::Get().SetThemes(Cursor::WAIT);fheroes2::cursor().show(true);
        display.render();
        const fheroes2::Point positions[]={{0,0},{639,479},{511,256},{512,255},{1,478},{320,240},{639,0},{0,479},{100,100}};
        for(const auto &p:positions){
            SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.x=p.x;e.motion.y=p.y;SDL_PushEvent(&e);
            LocalEvent::Get().HandleEvents(false);
#ifdef HMM2_PACKED
            if(std::getenv("HMM2_TEST_CURSOR_CACHED")){
                hmm_gpu_move_cursor(p.x,p.y);
                hmm_host_capture_cached_cursor();
                continue;
            }
#endif
            display.render();
        }
        Cursor::Get().SetThemes(Cursor::POINTER);display.render();
        std::puts("LOADING CURSOR SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_ADVENTURE_AUDIT")){
        world.GetKingdom(colors.front()).PuzzleMaps()=std::string(48,'1').c_str();
        phase("adventure_info");closing=true;events=0;Dialog::GameInfo();closing=false;display.render();
        phase("adventure_puzzle");closing=true;events=0;world.GetKingdom(colors.front()).PuzzleMaps().ShowMapsDialog();closing=false;display.render();
        for(int zoom=0;zoom<3;++zoom){
            Settings::Get().SetViewWorldZoomLevel(static_cast<ZoomLevel>(zoom));
            const char *phases[]={"adventure_world0","adventure_world1","adventure_world2"};phase(phases[zoom]);
            closing=true;events=0;ViewWorld::ViewWorldWindow(colors.front(),ViewWorldMode::OnlyVisible,ui);closing=false;display.render();
        }
        const auto savedCenter=ui.getGameArea().getCurrentCenterInPixels();
        const char *allPhases[2][3]={{"adventure_all_se0","adventure_all_se1","adventure_all_se2"},{"adventure_all_nw0","adventure_all_nw1","adventure_all_nw2"}};
        for(int corner=0;corner<2;++corner){
            ui.getGameArea().SetCenter(corner?fheroes2::Point(0,0):fheroes2::Point(world.w()-1,world.h()-1));
            for(int zoom=0;zoom<3;++zoom){
                Settings::Get().SetViewWorldZoomLevel(static_cast<ZoomLevel>(zoom));phase(allPhases[corner][zoom]);
                closing=true;events=0;ViewWorld::ViewWorldWindow(colors.front(),ViewWorldMode::ViewAll,ui);closing=false;display.render();
            }
        }
        ui.getGameArea().SetCenterInPixels(savedCenter);
        phase("adventure_closed");display.render();display.render();
        std::puts("ADVENTURE AUDIT SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_OOM_UNWIND")){
        try{
            const fheroes2::ImageRestorer savedScreen(display);
            const fheroes2::StandardWindow window(340,220,true);display.render();
            hmm_host_reject_unwind_alloc(true);throw std::bad_alloc();
        }catch(const std::bad_alloc&){hmm_host_reject_unwind_alloc(false);}
        std::puts("OOM WINDOW UNWIND COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_OOM_CLOSE")){
        {const fheroes2::ImageRestorer savedScreen(display);
            const fheroes2::StandardWindow window(340,220,true);display.render();
            hmm_host_reject_restore_alloc(true);
        }
        hmm_host_reject_restore_alloc(false);
        if(hmm_running)throw std::runtime_error("failed window restore did not stop session");
        std::puts("OOM NORMAL CLOSE COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_CMD_TORN")){
        phase("cmd_torn_cursor");Cursor::Get().SetThemes(Cursor::POINTER);fheroes2::cursor().show(true);display.render();
        hmm_host_arm_torn_command();
        const bool accepted=hmm_gpu_move_cursor(200,200);
        if(accepted||!hmm_host_torn_command_fired())throw std::runtime_error("partial cursor packet not injected");
        phase("cmd_recovered");display.render();display.render();
        if(!hmm_gpu_move_cursor(300,230))throw std::runtime_error("cursor packet after recovery failed");
        hmm_host_capture_cached_cursor();display.render();display.render();
        std::puts("COMMAND RECOVERY SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    // Клод: длинное сообщение поверх карты (как сюжетные окна в начале Alteris) — проверка карты под непрозрачным окном (map_covers_refresh).
    if(std::getenv("HMM2_TEST_DENSE_WINDOWS")){
        phase("dense_windows");
        for(int i=0;i<world.w()*world.h();++i)world.getTile(i).ClearFog(static_cast<PlayerColorsSet>(colors.front()));
        Interface::GameArea::updateMapFogDirections();
        const int centers[][2]={{83,51},{80,48},{85,48},{88,52},{81,46},{84,54},{87,49},{79,50}};
        for(const auto &center:centers){
            ui.getGameArea().SetCenter({center[0],center[1]});ui.redraw(Interface::REDRAW_ALL);display.render();
            std::printf("DENSE WINDOW center=%d,%d\n",center[0],center[1]);
            closing=true;events=0;Dialog::GameInfo();closing=false;display.render();
            closing=true;events=0;fheroes2::showStandardTextMessage("Hardware fade","The window, its text and its shadow fade together over the dense monster map.",Dialog::OK);closing=false;display.render();
        }
        std::puts("DENSE WINDOWS SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_DIALOG_LAYERS")){
        phase("dialog_layers");
        {const Dialog::FrameBox outer(180,true);display.render();
            closing=true;events=0;fheroes2::showStandardTextMessage("Nested message","A message over another game window.",Dialog::OK);closing=false;
            display.render();
        }display.render();
        {const fheroes2::StandardWindow outer(320,180,true);display.render();
            closing=true;events=0;fheroes2::showStandardTextMessage("Nested standard window","Hardware sprites and shadow over an existing window.",Dialog::OK);closing=false;
            display.render();
        }display.render();
        closing=true;events=0;Dialog::GameInfo();closing=false;display.render();
        const Troop troop(Monster::SKELETON,25);
        closing=true;events=0;Dialog::ArmyInfo(troop,0);closing=false;display.render();
        auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
        if(!heroes.empty()){closing=true;events=0;Dialog::QuickInfo(*heroes.front());closing=false;display.render();}
        std::puts("DIALOG LAYERS SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_MESSAGE")){
        phase("message");std::string text;
        if(!std::atoi(std::getenv("HMM2_TEST_MESSAGE")))text="They destroyed the entire land.";
        for(int i=0;i<std::atoi(std::getenv("HMM2_TEST_MESSAGE"));++i)text+="They destroyed the entire land, killing everything living. The Necromancer King was bent on destroying everything. ";
        closing=true;events=0;fheroes2::showStandardTextMessage("Alteris",text,Dialog::OK);closing=false;display.render();
        // Клод: HMM2_TEST_MESSAGE_AFTER=N — после сообщения N кадров карты, как в игре.
        if(const char *after=std::getenv("HMM2_TEST_MESSAGE_AFTER"))for(int i=0;i<std::atoi(after);++i){Game::updateAdventureMapAnimationIndex();ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
        std::puts("MESSAGE SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    // Клод: книга заклинаний героя (щелчок по книге в инвентаре) — тень книги (makeShadow) слоями FT812 без второго большого буфера.
    if(std::getenv("HMM2_TEST_SPELLBOOK")){
        phase("spellbook");auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
        if(heroes.empty())throw std::runtime_error("spell book test requires hero");
        auto &hero=*heroes.front();std::printf("SPELLBOOK hero=%s had=%d%c",hero.GetName().c_str(),int(hero.HaveSpellBook()),10);if(!hero.HaveSpellBook())hero.SpellBookActivate();
        closing=true;events=0;hero.OpenSpellBook(SpellBook::Filter::ALL,false,false,{});closing=false;display.render();
        std::puts("SPELLBOOK SCENARIO COMPLETE");std::puts("SCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_HERO_DIALOG")){
        // Codex: исходное окно героя с градиентными тенями; тот же сценарий у CPU-эталона.
        phase("hero_dialog");auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
        if(heroes.empty())throw std::runtime_error("hero dialog test requires hero");
        closing=true;events=0;Game::OpenHeroesDialog(*heroes.front(),false,true);closing=false;
        display.render();
        // Клод: HMM2_TEST_HERO_DIALOG_AFTER=N — после окна игра дальше рисует карту (N кадров) — кадры анимации, как на стенде; окно — дважды.
        if(const char *again=std::getenv("HMM2_TEST_HERO_DIALOG_AFTER"))for(int round=0;round<2;++round){
            for(int i=0;i<std::atoi(again);++i){Game::updateAdventureMapAnimationIndex();ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
            if(!round){closing=true;events=0;Game::OpenHeroesDialog(*heroes.front(),false,true);closing=false;display.render();}
        }
        std::puts("HERO DIALOG SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_ANIMATION")){
        // Codex: 120 исходных фаз отдельно от движения героя; тот же сценарий у CPU-эталона и FT.
        phase("animation");auto &animationArea=ui.getGameArea();int animated=-1;
        auto hasAnimation=[](const Maps::ObjectPart &part){
            if(part.icnType==MP2::OBJ_ICN_TYPE_UNKNOWN||part.icnIndex==255)return false;
            const auto *object=Maps::getObjectPartByIcn(part.icnType,part.icnIndex);
            return object&&object->animationFrames>1;
        };
        for(int i=0;i<world.w()*world.h()&&animated<0;++i){
            const auto &tile=world.getTile(i);
            if(hasAnimation(tile.getMainObjectPart()))animated=i;
            for(const auto &part:tile.getGroundObjectParts())if(hasAnimation(part))animated=i;
            for(const auto &part:tile.getTopObjectParts())if(hasAnimation(part))animated=i;
        }
        if(animated<0)throw std::runtime_error("animation test requires animated object");
        if(!std::getenv("HMM2_TEST_HERO_FOCUS"))animationArea.SetCenter({animated%world.w(),animated/world.w()});
        std::printf("ANIMATION tile=%d\n",animated);
        const auto anchor=animationArea.getCurrentCenterInPixels();uint8_t testPalettes[2][768];
        for(unsigned i=0;i<120;++i){
            // Codex: отдельный стресс-тест одновременно меняет фазу, сдвиг на 5 px и палитру.
            // Палитра — fade оригинальных RGB, сохраняющий равенство исходно одинаковых цветов.
            // Эталон исполняет тот же публичный API исходного движка, без ускоренного рендера.
            if(std::getenv("HMM2_TEST_SCROLL_PALETTE")){
                animationArea.SetCenterInPixels({anchor.x+int(i%16)*5,anchor.y+int((i/16)%4)*5});
                auto *pal=testPalettes[i&1];for(unsigned c=0;c<768;++c)pal[c]=uint8_t(unsigned(fheroes2::getGamePalette()[c])*4*(80+i%40)/120);
                display.changePalette(pal);
            }
            Game::updateAdventureMapAnimationIndex();
            // Codex: тестовый вид без тумана использует штатный флаг; игровой режим не меняется.
#ifdef HMM2_PACKED
            const HmmMapAnimation hmm_animation(i!=0);
#endif
            animationArea.Redraw(display,Interface::LEVEL_HEROES|Interface::LEVEL_ROUTES,false);
            display.render();SDL_Delay(250);
        }
        display.changePalette();std::puts("ANIMATION SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_BUILDING")){
        // Codex: проверяем цветные флаги и тени при трёх альфа на неоднородном фоне.
        const auto &castles=world.GetKingdom(colors.front()).GetCastles();if(castles.empty())throw std::runtime_error("test castle missing");
        phase("building");
        for(int id:{ICN::TWNZCSTL,ICN::TWNKCSTL,ICN::TWNKDW_4,ICN::TWNKUP_4,ICN::TWNKLTUR,ICN::TWNKRTUR}){
            const auto count=fheroes2::AGG::GetICNCount(id);if(!count)throw std::runtime_error("test ICN missing");
            for(unsigned index:{0u,std::min(1u,count-1),count-1})for(uint8_t alpha:{64,128,255}){
                for(int y=0;y<480;y+=16)fheroes2::Fill(display,0,y,640,16,uint8_t(32+y/16));
                fheroes2::drawCastleDialogBuilding(id,index,*castles.front(),{0,0},{20,10,600,460},alpha);display.render();
            }
        }
        std::puts("BUILDINGS EXACT SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    if(std::getenv("HMM2_TEST_RECRUIT_REPEAT")){
        phase("recruit_repeat");auto &castles=world.GetKingdom(conf.CurrentColor()).GetCastles();if(castles.empty())throw std::runtime_error("test needs town");
        std::printf("RECRUIT TOWN name=%s race=%d color=%d\n",castles.front()->GetName().c_str(),castles.front()->GetRace(),conf.CurrentColor());
        const uint32_t dwellings[]={DWELLING_MONSTER1,DWELLING_MONSTER2,DWELLING_MONSTER3,DWELLING_MONSTER4,DWELLING_MONSTER5,DWELLING_MONSTER6};
        for(unsigned i=0;i<6;i++)std::printf("RECRUIT DWELLING level=%u built=%u stock=%u\n",i+1,unsigned(castles.front()->isBuild(dwellings[i])),castles.front()->getMonstersInDwelling(dwellings[i]));
        const unsigned level=std::getenv("HMM2_TEST_RECRUIT_LEVEL")?unsigned(std::atoi(std::getenv("HMM2_TEST_RECRUIT_LEVEL"))):1;
        if(level<1||level>6||!castles.front()->isBuild(dwellings[level-1]))throw std::runtime_error("chosen dwelling not built");recruit_key=SDLK_1+level-1;
        recruit_repeat=true;recruit_opens=recruit_events=town_events=0;castles.front()->OpenDialog(false,false,false,false);recruit_repeat=false;
        if(recruit_opens!=12)throw std::runtime_error("not all repeated recruit dialogs opened");
        state("recruit_complete");std::printf("RECRUIT REPEAT COMPLETE count=12 purchase_attempt=%u\nSCENARIO COMPLETE\n",unsigned(!std::getenv("HMM2_TEST_RECRUIT_CANCEL_ONLY")));return 1;
    }
    phase("scroll");auto &area=ui.getGameArea();const auto center=area.getCurrentCenterInPixels();
    for(int i=0;i<8;++i){area.SetCenterInPixels({center.x+i*4,center.y});ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
    phase("hero");auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
    if(!heroes.empty()){
        Heroes &hero=*heroes.front();ui.SetFocus(&hero,false);area.SetCenter(hero.GetCenter());const int start=hero.GetIndex();
        for(int target=0;target<world.w()*world.h();++target){
            if(target==start||world.getTile(target).getMainObjectType()!=MP2::OBJ_NONE)continue;auto path=world.getPath(hero,target);if(path.empty()||path.size()>3)continue;
            hero.GetPath().setPath(path);hero.SetMove(true);
            for(int f=0;f<120&&hero.isMoveEnabled();++f){hero.Move(false);ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
            std::printf("HERO start=%d end=%d\n",start,hero.GetIndex());break;
        }
    }
    state("moved");
    if(std::getenv("HMM2_TEST_SAVE")){
        const std::string name=Game::GetSaveDir()+"/King_of_Death_day_1.sav";
        if(!std::getenv("HMM2_TEST_LOAD_EXISTING")&&!Game::Save(name))throw std::runtime_error("save test failed");
        const uint64_t before=hmm_wall_seconds();if(!Game::AutoSave())throw std::runtime_error("autosave test failed");
        Maps::FileInfo saved;if(!Game::LoadSAV2FileInfo(Game::GetSaveDir()+"/AUTOSAVE"+Game::GetSaveFileExtension(),saved))throw std::runtime_error("autosave header read failed");
        if(saved.timestamp<before||saved.timestamp>hmm_wall_seconds())throw std::runtime_error("autosave RTC timestamp mismatch");
        std::printf("AUTOSAVE RTC epoch=%u before=%llu after=%llu\n",saved.timestamp,(unsigned long long)before,(unsigned long long)hmm_wall_seconds());
        world.generateBattleOnlyMap(0);if(Game::Load(name)!=fheroes2::GameMode::START_GAME)throw std::runtime_error("load test failed");
        state("reloaded");std::puts("SAVE LOAD COMPLETE\nSCENARIO COMPLETE");return 1;
    }
    phase("town");auto &castles=world.GetKingdom(colors.front()).GetCastles();
    if(!castles.empty()){closing=true;events=0;castles.front()->OpenDialog(false,false,false,false);closing=false;}
    phase("battle");world.generateBattleOnlyMap(0);conf.GetPlayers().Init(PlayerColor::BLUE|PlayerColor::RED);world.InitKingdoms();
    // Клод: HMM2_SCENE_HUMAN_BATTLE — синие под рукой человека: ход ждёт ввода, на кадрах — контур отряда (hmm_gpu_contour).
    Players::SetPlayerControl(PlayerColor::BLUE,(std::getenv("HMM2_SCENE_HUMAN_BATTLE")||std::getenv("HMM2_TEST_AUTOBATTLE"))?CONTROL_HUMAN:CONTROL_AI);Players::SetPlayerControl(PlayerColor::RED,CONTROL_AI);conf.SetCurrentColor(PlayerColor::BLUE);conf.SetGameType(Game::TYPE_STANDARD);
    Army attacker,defender;attacker.SetColor(PlayerColor::BLUE);defender.SetColor(PlayerColor::RED);
    attacker.GetTroop(0)->Set(Monster(Monster::ARCHER),12);defender.GetTroop(0)->Set(Monster(Monster::ORC),12);Rand::PCG32 random(12345,6789);
    if(std::getenv("HMM2_TEST_BATTLE_SUMMARY")){
        const int kinds[]={Monster::DWARF,Monster::GOBLIN,Monster::PEASANT,Monster::UNICORN,Monster::PHOENIX};
        const unsigned counts[]={25,15,15,15,5},enemy[]={5,5,5,4,4};
        for(unsigned i=0;i<5;++i){attacker.GetTroop(i)->Set(Monster(kinds[i]),counts[i]);defender.GetTroop(i)->Set(Monster(Monster::CENTAUR),enemy[i]);}
    }
    // Codex: состав тяжёлого боя Trodo из длинной сессии; исполняется исходная Battle::Arena.
    if(std::getenv("HMM2_TEST_LARGE_BATTLE")){
        const int kinds[]={Monster::GARGOYLE,Monster::GRIFFIN,Monster::MINOTAUR,Monster::HYDRA,Monster::BLACK_DRAGON};
        const unsigned counts[]={25,20,10,10,2};
        for(unsigned i=0;i<5;++i)attacker.GetTroop(i)->Set(Monster(kinds[i]),counts[i]);
        const unsigned zombies[]={8,8,7,6};for(unsigned i=0;i<4;++i)defender.GetTroop(i)->Set(Monster(Monster::ZOMBIE),zombies[i]);
    }
    const bool idle=std::getenv("HMM2_TEST_BATTLE_IDLE")!=nullptr;
    if(idle)Players::SetPlayerControl(PlayerColor::BLUE,CONTROL_HUMAN);
    {Battle::Arena arena(attacker,defender,1,true,random);
        if(std::getenv("HMM2_TEST_AUTOBATTLE")){
            const Battle::Unit *unit=nullptr;
            for(int cell=0;cell<99&&!unit;++cell){const auto *p=arena.GetTroopBoard(cell);if(p&&p->GetArmyColor()==PlayerColor::BLUE)unit=p;}
            if(!unit)throw std::runtime_error("auto mode fixture has no unit");
            // The fixture closes the town with repeated synthetic Escape
            // events. Directly invoking this dialog bypasses a real user's
            // opening action, so discard that fixture input before pressing A.
            SDL_FlushEvents(SDL_KEYDOWN,SDL_KEYUP);
            phase("auto_mode_select");Battle::Actions actions;choose_auto_mode=true;events=0;
            arena.GetInterface()->hmmTestOpenAutoMode(*unit,actions);choose_auto_mode=false;
            if(actions.size()!=1||actions.front().GetType()!=Battle::CommandType::TOGGLE_AUTO_COMBAT)throw std::runtime_error("Auto Combat selection failed");
            for(auto &action:actions)arena.ApplyAction(action);
            // AutoCombatInProgress() follows the current unit; the first unit
            // is selected by Turns(). Completing the battle without manual
            // input below verifies that the selected auto mode was applied.
            hmm_cache_trim();phase("auto_mode_running");std::puts("AUTO COMBAT ENABLED");
        }
        if(idle)battle_idle_until=hmm_millis()+600000;
        try{while(arena.BattleValid()&&arena.GetTurnNumber()<30)arena.Turns();}catch(const IdleDone&){std::puts("IDLE COMPLETE");}
        battle_idle_until=0;std::printf("BATTLE turns=%u\n",arena.GetTurnNumber());
        if(std::getenv("HMM2_TEST_AUTOBATTLE")){hmm_cache_trim();std::puts("AUTO COMBAT COMPLETE");}
        if(std::getenv("HMM2_TEST_BATTLE_SUMMARY")){
            phase("battle_summary");Players::SetPlayerControl(PlayerColor::BLUE,CONTROL_HUMAN);
            closing=true;events=0;arena.DialogBattleSummary(arena.GetResult(),{},false);closing=false;
            phase("battle_summary_closed");display.render();display.render();
            if(std::getenv("HMM2_TEST_WINDOW_AUDIT")){
                auto result=arena.GetResult();
                const unsigned losses[]={Battle::RESULT_LOSS};
                for(unsigned loss:losses){
                    phase("battle_loss_dialog");result.attacker=loss;result.defender=Battle::RESULT_WINS;
                    closing=true;events=0;arena.DialogBattleSummary(result,{},false);closing=false;display.render();
                }
                phase("necromancy_dialog");closing=true;events=0;arena.DialogBattleNecromancy(12);closing=false;display.render();
                phase("nested_battle_dialog");
                {const fheroes2::StandardWindow outer(340,220,true);display.render();
                    const Troop troop(Monster::CENTAUR,23);closing=true;events=0;Dialog::ArmyInfo(troop,0);closing=false;display.render();
                }display.render();phase("window_audit_complete");
            }
        }
    }
    std::puts("SCENARIO COMPLETE");return 1;
}
