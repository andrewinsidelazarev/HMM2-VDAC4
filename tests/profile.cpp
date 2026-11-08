// Codex: воспроизводимые замеры оригинального движка Win32; SDL и измеритель не входят в кучу ESP.
#include "profile.h"
#ifdef HMM2_STAGE0B
#include "profile0b.h"
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <SDL.h>
#include "game.h"
#include "game_auto_playtest.h"
#include "game_interface.h"
#include "game_mainmenu_ui.h"
#include "game_delays.h"
#include "game_mode.h"
#include "interface_gamearea.h"
#include "settings.h"
#include "maps_fileinfo.h"
#include "world.h"
#include "kingdom.h"
#include "castle.h"
#include "army.h"
#include "monster.h"
#include "battle_arena.h"
#include "rand.h"
#include "screen.h"
#include "players.h"
#include "heroes.h"
#include "route.h"
#include "maps_tiles.h"
#include "smk_decoder.h"
#ifdef HMM2_WORLD_COMPARE
#include "serialize.h"
static void profile_world_state(const char*);
#endif

namespace {
volatile LONG64 liveBytes=0, peakBytes=0;
thread_local bool observing=false;
const char *phase="startup";
unsigned eventCount=0, frames=0;
bool closeDialog=false;
bool visualExtra=false;
uint8_t *history[3]={};
uint8_t *dirty[3]={};
size_t historySize=0;
unsigned frameSerial=0;
uint64_t started=0;

int hook(int kind,void *data,size_t bytes,int block,long,const unsigned char *,int) {
    if(observing || block==_CRT_BLOCK) return TRUE;
    observing=true;
    LONG64 delta=0;
    if(kind==_HOOK_ALLOC) delta=bytes;
    else if(kind==_HOOK_FREE && data) delta=-static_cast<LONG64>(_msize_dbg(data,_NORMAL_BLOCK));
    else if(kind==_HOOK_REALLOC) delta=static_cast<LONG64>(bytes)-(data?static_cast<LONG64>(_msize_dbg(data,_NORMAL_BLOCK)):0);
    const auto n=InterlockedAdd64(&liveBytes,delta);
#ifdef HMM2_STAGE0B
    profile0b_hook(kind,data,bytes,n);
#endif
    LONG64 p=peakBytes;
    while(n>p) {auto old=InterlockedCompareExchange64(&peakBytes,n,p); if(old==p)break; p=old;}
    observing=false;
    return TRUE;
}
}

void profile_install() {
    _CrtSetReportMode(_CRT_WARN,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_WARN,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    // Codex: отключаем дорогую проверку всех блоков на каждой операции; учёт выделений остаётся.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);
    _CrtMemState state{}; _CrtMemCheckpoint(&state);
    liveBytes=state.lSizes[_NORMAL_BLOCK]+state.lSizes[_CLIENT_BLOCK];peakBytes=liveBytes;
    _CrtSetAllocHook(hook);started=GetTickCount64();
}
void profile_finish_phase() {
    observing=true;
#ifdef HMM2_STAGE0B
    profile0b_phase(phase,true);
#endif
    _CrtMemState state{};_CrtMemCheckpoint(&state);
    std::printf("PHASE\t%s\t%lld\t%lld\t%llu\t%llu\t%u\t%zu\n",phase,liveBytes,peakBytes,
                profile_cache_bytes(),GetTickCount64()-started,frames,state.lSizes[_NORMAL_BLOCK]+state.lSizes[_CLIENT_BLOCK]);
    std::fflush(stdout);observing=false;
}
void profile_phase(const char *name) {
    profile_finish_phase();phase=name;peakBytes=liveBytes;frames=0;eventCount=0;started=GetTickCount64();
#ifdef HMM2_STAGE0B
    profile0b_phase(phase,false);
#endif
}
void profile_read(const char *name,size_t size) {
    observing=true;std::printf("AGG\t%s\t%s\t%zu\n",phase,name,size);observing=false;
}
void profile_frame(const uint8_t *pixels,int width,int height) {
    if(!pixels || width<=0 || height<=0)return;
    observing=true;
    const size_t n=size_t(width)*height;
#ifdef HMM2_STAGE0B
    profile0b_frame(phase,pixels,width,height);
#endif
    if(n!=historySize){
        for(auto &h:history){if(h)VirtualFree(h,0,MEM_RELEASE);h=static_cast<uint8_t *>(VirtualAlloc(nullptr,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));}
        for(auto &d:dirty){if(d)VirtualFree(d,0,MEM_RELEASE);d=static_cast<uint8_t *>(VirtualAlloc(nullptr,(n+511)/512,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));}
        historySize=n;frameSerial=0;
    }
    auto *old=history[frameSerial%3];
    auto *previous=history[(frameSerial+2)%3];
    auto *d=dirty[frameSerial%3];
    std::memset(d,0,(n+511)/512);
    // Codex: как текущий DOOM video.c: объединяем изменения трёх последних кадров.
    // Сравнение прямо с буфером назначения служит только нижней контрольной границей.
    for(size_t at=0,b=0;at<n;at+=64,++b){
        if(!frameSerial || std::memcmp(previous+at,pixels+at,std::min(size_t(64),n-at)))d[b>>3]|=1u<<(b&7);
    }
    size_t changed=0,writes=0,run=0,minimum=0;
    for(size_t at=0,b=0;at<n;at+=64,++b){
        const size_t len=std::min(size_t(64),n-at);
        const bool required=frameSerial<3 || std::memcmp(old+at,pixels+at,len);
        const bool upload=frameSerial<3 || ((dirty[0][b>>3]|dirty[1][b>>3]|dirty[2][b>>3])>>(b&7)&1);
        if(required)minimum+=len;
        if(required && !upload)std::abort();
        if(upload){
            changed+=len;run+=len;
        }else if(run){writes+=(run+4095)/4096;run=0;}
    }
    if(run)writes+=(run+4095)/4096;
    std::memcpy(old,pixels,n);
    std::printf("FRAME\t%s\t%u\t%d\t%d\t%zu\t%zu\n",phase,frameSerial,width,height,changed,writes);
    std::printf("FRAME_MIN\t%s\t%u\t%zu\n",phase,frameSerial,minimum);
    // Codex: сохраняем начало сцены и кадр после перехода, не изменяя пиксели движка.
    if(frames==0 || frames==100 || (frames==20 && !std::strcmp(phase,"town"))){char path[128];std::snprintf(path,sizeof(path),"%s%s.idx",phase,frames?"_mid":"");FILE *f=std::fopen(path,"wb");if(f){std::fwrite(pixels,1,n,f);std::fclose(f);}}
    ++frameSerial;++frames;observing=false;
}
void profile_events() {
    if(closeDialog && ++eventCount>(visualExtra?1000:50) && eventCount%2==0){
        SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&e);
        e.type=SDL_KEYUP;SDL_PushEvent(&e);
    }
}

#ifdef HMM2_STAGE0B
void profile_trace_fill(fheroes2::Image &out,int x,int y,int w,int h,uint8_t color){
    bool saved=observing;observing=true;profile0b_fill(out,x,y,w,h,color);observing=saved;
}
void profile_trace_blit(const fheroes2::Image &in,int ix,int iy,fheroes2::Image &out,int ox,int oy,int w,int h,bool flip,bool copy){
    bool saved=observing;observing=true;profile0b_blit(in,ix,iy,out,ox,oy,w,h,flip,copy);observing=saved;
}
#endif
int profile_run(int argc,char **argv) {
    static_assert(sizeof(void*)==4,"Требуется 32-битная сборка");
    if(argc<3)return 2;
    auto &conf=Settings::Get();
    auto &display=fheroes2::Display::instance();
    if(const char *ext=std::strrchr(argv[2],'.');ext && !_stricmp(ext,".SMK")){
        profile_phase("smk_load");
        {
            SMKVideoSequence video(argv[2]);
            if(!video.frameCount())return 6;
            uint64_t audioBytes=0;for(const auto &a:video.getAudioChannels())audioBytes+=a.size();
            std::printf("SMK\t%lu\t%d\t%d\t%.3f\t%llu\n",video.frameCount(),video.width(),video.height(),video.microsecondsPerFrame(),audioBytes);
            profile_phase("smk_decode");
            fheroes2::Image frame;frame._disableTransformLayer();frame.resize(video.width(),video.height());frame.fill(0);
            std::vector<uint8_t> pal;
            uint32_t hash=2166136261u;
            // Codex: декодируем каждый кадр штатным SMKVideoSequence, не заменяя исходный режим загрузки.
            for(unsigned long i=0;i<video.frameCount();++i){
                int32_t w=0,h=0;video.getNextFrame(frame,0,0,w,h,pal);
                if(!w || !h)return 7;
                for(size_t at=0;at<size_t(frame.width())*frame.height();++at)hash=(hash^frame.image()[at])*16777619u;
                for(const auto c:pal)hash=(hash^c)*16777619u;
            }
            std::printf("SMK_HASH\t%08x\t%lu\n",hash,video.getCurrentFrameId());
            profile_finish_phase();
        }
        _CrtSetAllocHook(nullptr);std::printf("COMPLETE\t%s\n",argv[2]);return 0;
    }
    visualExtra=std::getenv("HMM2_VISUAL_EXTRA")!=nullptr;
#ifdef HMM2_WORLD_COMPARE
    // Codex: одинаковое начальное состояние ГПСЧ для проверки вариантов чтения карты.
    Rand::CurrentThreadRandomDevice()=Rand::PCG32(12345,6789);
#endif
    if(const char *r=std::getenv("HMM2_RESOLUTION")){if(std::atoi(r)==1024)display.setResolution({1024,768});}
    profile_phase("menu");fheroes2::drawMainMenuScreen();display.render();
    if(visualExtra)for(int i=0;i<30;++i)display.render();
    profile_phase("load");
    Maps::FileInfo info;
    if(!info.readMP2Map(argv[2],false))return 3;
    conf.SetGameType(Game::TYPE_STANDARD);conf.setCurrentMapInfo(info);conf.GetPlayers().SetStartGame();
    if(!world.LoadMapMP2(info.filename,info.version==GameVersion::SUCCESSION_WARS))return 4;
#ifdef HMM2_WORLD_COMPARE
    profile_world_state("load");profile_finish_phase();_CrtSetAllocHook(nullptr);
    std::printf("COMPLETE\t%s\n",argv[2]);return 0;
#endif
    const auto colors=PlayerColorsVector(conf.GetPlayers().GetColors());
    if(colors.empty())return 5;
    conf.SetCurrentColor(colors.front());
    profile_phase("map");
    auto &ui=Interface::AdventureMap::Get();ui.reset();ui.ResetFocus(0,false);ui.redraw(Interface::REDRAW_ALL);display.render();
    profile_phase("scroll");
    auto &area=ui.getGameArea();const auto center=area.getCurrentCenterInPixels();
    for(int i=0;i<32;++i){area.SetCenterInPixels({center.x+i*4,center.y});ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
    if(visualExtra){
        profile_phase("hero");
        auto &heroes=world.GetKingdom(colors.front()).GetHeroes();
        if(!heroes.empty()){
            Heroes &hero=*heroes.front();ui.SetFocus(&hero,false);area.SetCenter(hero.GetCenter());
            const int start=hero.GetIndex();
            // Codex: выбираем настоящий короткий путь оригинального поиска и исполняем обычную анимацию Move.
            for(int target=0;target<world.w()*world.h();++target){
                if(target==start || world.getTile(target).getMainObjectType()!=MP2::OBJ_NONE)continue;
                auto path=world.getPath(hero,target);
                if(path.empty() || path.size()>3)continue;
                hero.GetPath().setPath(path);hero.SetMove(true);
                for(int frame=0;frame<120 && hero.isMoveEnabled();++frame){hero.Move(false);ui.redraw(Interface::REDRAW_GAMEAREA);display.render();}
                std::printf("HERO\t%d\t%d\n",start,hero.GetIndex());break;
            }
        }
    }
    profile_phase("town");
    auto &castles=world.GetKingdom(colors.front()).GetCastles();
    if(!castles.empty()){closeDialog=true;castles.front()->OpenDialog(false,false,false,false);closeDialog=false;}
    profile_phase("week");
    for(const auto color:colors)Players::SetPlayerControl(color,CONTROL_AI);
    conf.SetAIMoveSpeed(0);Game::UpdateGameSpeed();
    auto &autoTest=fheroes2::AutoPlaytest::instance();autoTest.setMaxDaysInPlaythrough(7);autoTest.enableAnimation(false);autoTest.enableSounds(false);autoTest.reset(conf.GetPlayers().GetColors());
    conf.SetGameType(Game::TYPE_AUTO_PLAYTEST);Game::StartGame();
    observing=true;std::printf("DAYS\t%u\n",world.CountDay());observing=false;
    profile_phase("battle");
    world.generateBattleOnlyMap(0);conf.GetPlayers().Init(PlayerColor::BLUE|PlayerColor::RED);world.InitKingdoms();
    Players::SetPlayerControl(PlayerColor::BLUE,CONTROL_AI);Players::SetPlayerControl(PlayerColor::RED,CONTROL_AI);
    conf.SetCurrentColor(PlayerColor::BLUE);conf.SetGameType(Game::TYPE_STANDARD);
    Army attacker,defender;attacker.SetColor(PlayerColor::BLUE);defender.SetColor(PlayerColor::RED);
    attacker.GetTroop(0)->Set(Monster(Monster::ARCHER),12);defender.GetTroop(0)->Set(Monster(Monster::ORC),12);
    Rand::PCG32 random(12345,6789);
    {Battle::Arena arena(attacker,defender,1,true,random);while(arena.BattleValid() && arena.GetTurnNumber()<30)arena.Turns();}
    profile_finish_phase();_CrtSetAllocHook(nullptr);
    std::printf("COMPLETE\t%s\n",argv[2]);return 0;
}
#ifdef HMM2_WORLD_COMPARE
static void profile_world_state(const char*name){
    // Codex: сериализация наблюдателя исключена из замера и не меняет состояние мира.
    observing=true;
    {
        RWStreamBuf stream;stream<<world;uint32_t hash=2166136261u;
        for(size_t i=0;i<stream.size();++i)hash=(hash^stream.data()[i])*16777619u;
        FILE *f=std::fopen("world.state","wb");if(!f)std::abort();
        if(std::fwrite(stream.data(),1,stream.size(),f)!=stream.size())std::abort();std::fclose(f);
        std::printf("WORLD\t%s\t%d\t%d\t%zu\t%08x\n",name,world.w(),world.h(),stream.size(),hash);
    }
    observing=false;
}
#endif
