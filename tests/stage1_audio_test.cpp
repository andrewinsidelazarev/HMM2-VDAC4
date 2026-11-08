/* Codex: интеграция всех предоставленных треков и оригинальных вызовов SFX. */
#include "audio.h"
#include "audio_manager.h"
#include "hmm_audio.h"
#include "hmm_platform.h"
#include "m82.h"
#include "mus.h"
#include "dir.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cstdlib>
extern void hmm_host_fail_next_read();
extern "C" unsigned hmm_audio_test_frames();
extern "C" unsigned hmm_audio_test_resume();
static void check(bool ok,const char *what){if(!ok)throw std::runtime_error(what);}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=10){hmm_audio_tick();hmm_delay(10);}hmm_audio_tick();}
// Codex: одинаковые каналы делят SD-окно; 32 разных звука проверяют вытеснение страниц.
extern unsigned hmm_host_read_requests();
extern unsigned hmm_host_read_bytes();
static int cache_test(){
    Audio::Init();check(Audio::isValid(),"audio cache init");Music::Stop();Music::setVolume(0);Mixer::SetChannels(32);Mixer::setVolume(60);
    for(unsigned phase=0;phase<3;++phase){
        Mixer::Stop();
        for(unsigned i=0;i<(phase==2?8:32);++i){
            std::string path="audio/sfx/";path+=M82::GetString(static_cast<M82::SoundType>(phase?i:0));
            auto *file=hmm_fopen(path.c_str(),"rb");check(file!=nullptr,"cache SFX file");hmm_fseek(file,0,2);
            const uint32_t desc[]={0x31584653u,hmm_file_offset(file),uint32_t(hmm_ftell(file)),22050};hmm_fclose(file);
            check(Mixer::Play(reinterpret_cast<const uint8_t*>(desc),sizeof(desc),true,{{int16_t(i*11),uint8_t(i*5)}})==int(i),"cache SFX channel");
        }
        const unsigned reads=hmm_host_read_requests(),bytes=hmm_host_read_bytes();
        const unsigned start=hmm_audio_test_frames();advance(3000);
        std::printf("AUDIO CACHE phase=%u begin=%u end=%u reads=%u bytes=%u\n",phase,start,hmm_audio_test_frames(),hmm_host_read_requests()-reads,hmm_host_read_bytes()-bytes);
    }
    Audio::Quit();std::puts("AUDIO CACHE complete");return 0;
}

extern "C" void hmm_audio_test_ring(unsigned*,unsigned*,unsigned*);
static int ring_test(){
    Audio::Init();check(Audio::isValid(),"audio ring init");Music::setVolume(100);Music::SetFadeInMs(0);Mixer::SetChannels(32);Mixer::setVolume(60);
    Music::Play(10,std::string("music/Track10.ogg"),Music::PlaybackMode::PLAY_ONCE);advance(60000);
    unsigned loaded,consumed,capacity;hmm_audio_test_ring(&loaded,&consumed,&capacity);
    check(consumed>capacity*2,"ring did not wrap twice");check(Music::isPlaying(),"long music ended early");
    std::printf("AUDIO RING loaded=%u consumed=%u capacity=%u frames=%u\n",loaded,consumed,capacity,hmm_audio_test_frames());
    Audio::Quit();std::puts("AUDIO RING complete");return 0;
}
int hmm_run_audio_test(){
    if(std::getenv("HMM2_AUDIO_RING_TEST"))return ring_test();
    if(std::getenv("HMM2_AUDIO_CACHE_TEST"))return cache_test();
    Audio::Init();check(Audio::isValid(),"audio init");Music::setVolume(100);Music::SetFadeInMs(0);Mixer::SetChannels(32);Mixer::setVolume(100);
    for(unsigned track=2;track<=43;track++){
        char path[64];std::snprintf(path,sizeof(path),"music/Track%02u.ogg",track);
        Music::Play(track,std::string(path),Music::PlaybackMode::PLAY_ONCE);std::printf("AUDIO TEST track_begin=%u frame=%u\n",track,hmm_audio_test_frames());advance(2500);Music::Stop();std::printf("AUDIO TEST track=%u decoded\n",track);
    }
    ListFiles fonts;AudioManager::AudioInitializer audio("data/heroes2.agg","data/heroes2x.agg",fonts,"");
    Music::setVolume(100);Music::SetFadeInMs(0);AudioManager::PlayMusic(MUS::MAINMENU,Music::PlaybackMode::RESUME_AND_PLAY_INFINITE);advance(1200);
    {AudioManager::MusicRestorer restore;AudioManager::PlayMusic(MUS::NEW_WEEK,Music::PlaybackMode::PLAY_ONCE);advance(300);}
    std::printf("AUDIO TEST resume_pcm_begin=%u\n",hmm_audio_test_frames());advance(500);check(Music::isPlaying(),"music restorer did not resume");
    Music::Stop();Music::Play(42,Music::PlaybackMode::RESUME_AND_PLAY_INFINITE);unsigned saved=hmm_audio_test_resume();
    Music::Stop();Music::Play(42,Music::PlaybackMode::RESUME_AND_PLAY_INFINITE);
    check(saved>0&&hmm_audio_test_resume()==saved,"interrupted music resume lost original position");advance(500);
    std::puts("AUDIO TEST interrupted_resume=ok");AudioManager::ResetAudio();Music::setVolume(0);
    int channel=AudioManager::PlaySound(M82::BOARATTK);check(channel>=0&&Mixer::isPlaying(channel),"SFX did not start");
    std::printf("AUDIO TEST sfx_pcm_begin=%u\n",hmm_audio_test_frames());
    advance(3000);check(!Mixer::isPlaying(channel),"SFX did not finish");
    std::map<M82::SoundType,std::vector<AudioManager::AudioLoopEffectInfo>> loops;
    loops[M82::WSND00].emplace_back(90,0);loops[M82::WSND00].emplace_back(270,100);AudioManager::playLoopSoundsAsync(loops);std::printf("AUDIO TEST ambient_pcm_begin=%u\n",hmm_audio_test_frames());advance(500);
    check(Mixer::isPlaying(0)||Mixer::isPlaying(1),"ambient loop did not start");loops.clear();AudioManager::playLoopSoundsAsync(loops);advance(50);
    for(int i=0;i<Mixer::getChannelCount();i++)check(!Mixer::isPlaying(i),"ambient loop did not stop");
    Audio::Mute();advance(50);Audio::Unmute();AudioManager::ResetAudio();
    // Codex: ошибка SD возвращается из C-декодера до повторного броска в C++.
    for(unsigned pass=0;pass<2;++pass){
        if(pass){Music::Play(2,std::string("music/Track02.ogg"),Music::PlaybackMode::PLAY_ONCE);advance(1500);}
        hmm_host_fail_next_read();bool cancelled=false;
        try {if(!pass)Music::Play(2,std::string("music/Track02.ogg"),Music::PlaybackMode::PLAY_ONCE);advance(10000);}catch(const std::runtime_error &e){std::printf("AUDIO TEST cancellation pass=%u reason=%s\n",pass,e.what());cancelled=std::strcmp(e.what(),"injected OGG SD cancellation")==0;}
        check(cancelled,"OGG SD cancellation did not return to C++");Music::Stop();
    }
    std::puts("AUDIO TEST cancel_headers=ok cancel_body=ok");Audio::Quit();std::puts("AUDIO TEST complete tracks=42 sfx=ok loops=ok");return 0;
}
