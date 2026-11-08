"""Codex: исходная политика звуков OpenHMM2, обслуживание очереди в потоке игры."""
import re

def patch(name,s):
    if name=='localevent.cpp':
        s='#include "hmm_audio.h"\n'+s
        marker='bool LocalEvent::HandleEvents( const bool sleepAfterEventProcessing /* = true */, const bool allowExit /* = false */ )\n{'
        assert marker in s
        s=s.replace(marker,marker+'\n    hmm_audio_tick();',1)
    if name!='audio_manager.cpp':return s
    s='#include "hmm_audio.h"\n#include "hmm_platform.h"\n#include <cstring>\n'+s
    a=s.index('    struct MusicFileType');b=s.index('    struct ChannelAudioLoopEffectInfo',a)
    s=s[:a]+'''    // Codex: каталог подготовлен по ID треков оригинала; имя не ищется на SD при каждом событии.
    std::string getExternalMusicFile(const int track){
        char path[40];std::snprintf(path,sizeof(path),"music/track%02d.ogg",track);
        return hmm_is_file(path)?std::string(path):std::string{};
    }
'''+s[b:]
    a=s.index('    void LoadWAV(');b=s.index('    std::map<int, std::vector<uint8_t>> wavDataCache;',a)
    s=s[:a]+'''    // Codex: каталог DAT указывает прямо в исходные байты 82M; копии PCM в PSRAM нет.
    void LoadWAV(int m82,std::vector<uint8_t> &v){
        std::string path="audio/sfx/";path+=M82::GetString(m82);
        hmm_file *f=hmm_fopen(path.c_str(),"rb");if(!f)return;
        hmm_fseek(f,0,2);const long size=hmm_ftell(f);
        const uint32_t desc[]={0x31584653u,hmm_file_offset(f),static_cast<uint32_t>(size),22050};
        hmm_fclose(f);v.resize(sizeof(desc));std::memcpy(v.data(),desc,sizeof(desc));
    }
    void LoadMID(int,std::vector<uint8_t>&){}

'''+s[b:]
    s=s.replace('std::map<int, std::vector<uint8_t>> wavDataCache;', 'std::vector<uint8_t> wavDataCache[M82::UNKNOWN];')
    s=s.replace('        wavDataCache.clear();','        for(auto &v:wavDataCache)v.clear();')
    s=s.replace('class AsyncSoundManager final : public MultiThreading::AsyncManager','class AsyncSoundManager final')
    s=s.replace('        bool prepareTask() override','        bool prepareTask()')
    s=s.replace('        void executeTask() override','        void executeTask()')
    s=s.replace('        std::recursive_mutex _resourceMutex;','')
    a=s.index('        std::recursive_mutex & resourceMutex()');b=s.index('    private:',a)
    s=s[:a]+'''        // Codex: очереди меняет только игровой поток; PCM выводится отдельной задачей.
        void createWorker(){} void notifyWorker(){} void stopWorker(){}
        void service(){while(prepareTask())executeTask();}

'''+s[b:]
    s=re.sub(r'        const std::scoped_lock<std::(?:recursive_mutex|mutex)> lock\( [^\n]+ \);\n','',s)
    a=s.index('        assert( !originalAGGFilePath.empty() );');b=s.index('\n    AudioInitializer::~AudioInitializer()',a)
    s=s[:a]+'''        // Codex: все 82M из обоих AGG уже адресованы каталогом; треки — исходные OGG.
        (void)originalAGGFilePath;(void)expansionAGGFilePath;
    }
'''+s[b:]
    a=s.index('        int xmi = XMI::UNKNOWN;');b=s.index('    std::pair<size_t, size_t> findPairOfClosestSoundEffects',a)
    s=s[:a]+'''        // Codex: полный набор оригинальных треков берётся из MUSIC; MIDI-банк не загружается.
    }

'''+s[b:]
    s=s.replace('thread_local ','static ')
    a=s.index('    fheroes2::AGGFile g_midiHeroes2AGG;');b=s.index('    AsyncSoundManager g_asyncSoundManager;',a)
    s=s[:a]+s[b:]
    a=s.index('    const std::vector<uint8_t> & GetMID(');b=s.index('    // Returns the ID of the channel',a)
    s=s[:a]+s[b:]
    s=s.replace('    std::map<int, std::vector<uint8_t>> MIDDataCache;','').replace('        MIDDataCache.clear();','')
    s=s.replace('std::deque<SoundTask> _soundTasks;','std::vector<SoundTask> _soundTasks;')
    s=s.replace('_soundTasks.pop_front();','_soundTasks.erase(_soundTasks.begin());')
    s=s.replace('std::atomic<TaskType> _taskToExecute{ TaskType::None };','TaskType _taskToExecute{ TaskType::None };')
    s+='\nextern "C" void hmm_audio_manager_tick(){g_asyncSoundManager.service();}\n'
    return s
