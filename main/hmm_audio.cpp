/* Codex: штатный декодер OGG VDAC4 и потоковые 82M; правила запуска — audio_manager.cpp.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_audio.h"
#include "hmm_platform.h"
extern "C" {
#include "hmm_core.h"
#include "zm_source.h"
}
#ifndef HMM2_HOST
extern "C" unsigned char *hmm_recycled_data(size_t);
#endif
#include "audio.h"
#include "dir.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>
#ifdef HMM2_HOST
#include <SDL.h>
extern "C" int hmm_ogg_main(int,char**);
#endif
namespace {
constexpr unsigned rate=44100,block=256,ring_frames=4096,channels_max=32,music_bytes=128*1024,sample_bytes=16384;
// Клод: буфер зацикленного звука в PSRAM (см. loop_alloc): data — весь звук, filled — сколько уже скопировано с первого прохода,
// users — сколько каналов его играют: одну петлю (море, шахта) на карте играют сразу несколько источников.
// Клод: pooled — звук в постоянной области звуков (sound_get): последний канал его не освобождает, он остаётся для следующего запуска.
struct LoopBuffer {uint32_t offset{},size{},filled{};uint8_t *data{};unsigned users{};bool pooled{};uint32_t stamp{};};
// Клод: whole — общий буфер петли канала или нет.
struct Effect {uint32_t offset{},size{},sample_rate{},pos{},fraction{},until{},page{};float left{1},right{1};bool active{},loop{};LoopBuffer *whole{};};
// Codex: общий кэш делит данные одинаковых звуков с разной панорамой: всего те же 16 КиБ.
// До 8 каналов читаются окнами 2 КиБ; при большей полифонии каждому остаётся окно 512 байт.
struct SamplePage {uint32_t at{},n{},age{};};
SamplePage sample_pages[channels_max];uint8_t *sample_data,*music_data;uint32_t sample_age;
unsigned sample_width=2048,sample_count=8;bool sample_changed=true;
Effect effects[channels_max];unsigned channel_count=32;int sound_volume=100,music_volume=100,fade_ms=900;
std::atomic<bool> running{false},finished{true},muted{false};bool valid,servicing,playing,pcm_started;
float pcm[block*2],*ring;unsigned ring_read,ring_write,ring_count;void *ring_lock,*module;
const zm_source_t *decoder;void *song;zm_host_t host{};
uint32_t input_base,input_size,input_pos,input_head,music_frames,pending_resume,starves;unsigned music_channels=2;
/* Клод: input_pos/input_head — сквозные позиции кольца OGG; pass_start — где в них начался текущий проход файла. Повтор трека
 * (не PLAY_ONCE) подкачивает начало следующего прохода заранее, и конец трека открывает декодер на данных в памяти (restart_pass):
 * прежде повтор закрывал файл и читал его начало с SD в задаче звука — провал ~100 мс на каждом повторе (стенд, 3 × 32 мс). */
uint32_t pass_start;
struct Track {std::string path;uint32_t position{};};Track tracks[64];uint64_t current_uid;
uint32_t mixed_frames,music_until,fade_progress;std::atomic<uint32_t> written_frames{0};
Music::PlaybackMode current_mode=Music::PlaybackMode::PLAY_ONCE;
// Клод: дорожка ролика; пока она звучит, здесь сохранена громкость музыки, иначе -1.
constexpr uint32_t clip_track=63;int clip_music_volume=-1;
void clip_finish(){if(clip_music_volume>=0){music_volume=clip_music_volume;clip_music_volume=-1;}}
#ifdef HMM2_HOST
SDL_AudioDeviceID device;uint64_t test_epoch;FILE *capture;
float host_ring[ring_frames*2];uint8_t host_music[music_bytes],host_samples[sample_bytes];bool test_direct;
bool test_clock(){return std::getenv("HMM2_TEST_MAP")||std::getenv("HMM2_TEST_UI")||std::getenv("HMM2_AUDIO_TEST");}
#endif
void log_audio(const char *fmt,...){char text[180];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof(text),fmt,args);va_end(args);hmm_log(text);}
void *audio_raw_alloc(size_t n,uint32_t caps){
#ifdef HMM2_HOST
    if(!hmm_core||!hmm_core->alloc)return std::malloc(n);
#endif
    return hmm_core->alloc(n,caps);
}
void audio_raw_free(void *p){
#ifdef HMM2_HOST
    if(!hmm_core||!hmm_core->free){std::free(p);return;}
#endif
    hmm_core->free(p);
}
// Codex: OGG запрашивает 64 КиБ входа и 256 КиБ состояния при каждом открытии.
// После мира эти блоки сохраняются между песнями: в длинной трассе повторный
// запрос 256 КиБ не прошёл при 254384 свободных. На границе миров запас отдаётся.
struct AudioMemory {void *data{};bool busy{};};
AudioMemory audio_memory[2];bool audio_keep;
constexpr size_t audio_sizes[]={64*1024,256*1024};
void *audio_alloc(size_t n,uint32_t caps){
    for(unsigned i=0;i<2;++i)if(n==audio_sizes[i]&&!audio_memory[i].busy){
        auto &b=audio_memory[i];if(!b.data)b.data=audio_raw_alloc(n,caps);
        b.busy=b.data!=nullptr;return b.data;
    }
    return audio_raw_alloc(n,caps);
}
void audio_free(void *p){
    if(!p)return;
    for(auto &b:audio_memory)if(p==b.data){b.busy=false;if(audio_keep)return;b.data=nullptr;break;}
    audio_raw_free(p);
}
void audio_memory_plan(bool keep){
    audio_keep=keep;if(!keep)for(auto &b:audio_memory)if(!b.busy)audio_free(b.data);
}
// Codex: единственный владелец сжатого кольца — игровой поток. Декодер читает только PSRAM.
struct AudioServiceScope {bool previous{servicing};AudioServiceScope(){servicing=true;}~AudioServiceScope(){servicing=previous;}};
void music_hint(uint32_t offset,uint32_t size){
    if(hmm_core&&hmm_core->event){const uint32_t hint[]={offset,size};
        if(hmm_core->event(0xE7,hint,sizeof(hint),1000)!=ZM_OK)log_audio("AUDIO Evo prefetch hint busy");}
}
bool music_fill(){
#ifdef HMM2_HOST
    if(test_direct)return false;
#endif
    const uint32_t pass_end=pass_start+input_size,limit=current_mode!=Music::PlaybackMode::PLAY_ONCE?pass_end+input_size:pass_end;
    if(input_head>=limit||input_head-input_pos==music_bytes||hmm_audio_busy())return false;
    const bool next=input_head>=pass_end;const uint32_t file=input_head-(next?pass_end:pass_start);
    const unsigned at=input_head&(music_bytes-1);
    const unsigned count=std::min<unsigned>(16384,std::min<unsigned>(music_bytes-(input_head-input_pos),std::min<unsigned>(music_bytes-at,input_size-file)));
    if(next&&!file)music_hint(input_base,input_size);
    if(hmm_remote_read(input_base+file,music_data+at,count)!=count)hmm_fail("OGG short SD read");
    input_head+=count;return true;
}
size_t input_read(void *dst,size_t n){
#ifdef HMM2_HOST
    // Codex: независимый байтовый источник для проверки PCM при нескольких оборотах кольца.
    if(test_direct){n=std::min(n,std::min<size_t>(32768,input_size-input_pos));if(n&&hmm_remote_read(input_base+input_pos,dst,n)!=n)hmm_fail("OGG direct read");input_pos+=n;input_head=input_pos;return n;}
#endif
    const unsigned at=input_pos&(music_bytes-1);
    // Codex: штатный OGG сдвигает остаток после каждого пакета. Малая порция
    // не раздувает его входной буфер; кольцо HMM2 и готовый PCM остаются прежними.
    n=std::min(n,std::min<size_t>(256,std::min<unsigned>(std::min(input_head,pass_start+input_size)-input_pos,music_bytes-at)));
    if(n){std::memcpy(dst,music_data+at,n);input_pos+=n;}return n;
}
size_t input_available(){
#ifdef HMM2_HOST
    if(test_direct)return input_size-input_pos;
#endif
    return std::min(input_head,pass_start+input_size)-input_pos;}int input_ended(){return input_pos==pass_start+input_size;}
const Track *headers_track;uint32_t resume_target,resume_granule;
// Клод: повтор трека без закрытия файла: начало следующего прохода уже в кольце; не успело — дочитывается, как при открытии.
bool fill_now();
bool restart_pass(){
#ifdef HMM2_HOST
    if(test_direct)return false;
#endif
    if(input_pos!=pass_start+input_size)return false;
    decoder->close(song);song=nullptr;pass_start=input_pos;host.stream_offset=0;
    for(unsigned i=0;i<2&&input_head-pass_start<8192&&fill_now();++i){}
    song=decoder->open(input_size,nullptr);music_frames=resume_granule=0;resume_target=0;pending_resume=0;
    playing=song!=nullptr;return playing;
}
int decode(float *dst,unsigned frames,uint32_t *got){
    for(;;){const auto before=input_pos;int result=decoder->render(song,dst,frames,got);
        if(result!=ZM_RENDER_NEED_INPUT||*got)return result;
        // Codex: пополнение вне C-кадров OGG.ELF; ошибка SD остаётся исключением игрового потока.
        if(!input_available()&&music_fill())continue;
        if(input_pos==before)return result;
    }
}
int resource(const char *name,const uint8_t **data,size_t *size){return hmm_core&&hmm_core->resource?hmm_core->resource(name,data,size):ZM_NOT_FOUND;}
void cover(const uint8_t*,size_t){}
uint32_t audible_frames(){
#ifdef HMM2_HOST
    if(test_clock())return std::min<uint32_t>(written_frames,uint32_t((hmm_millis()-test_epoch)*rate/1000));
    const uint32_t queued=device?SDL_GetQueuedAudioSize(device)/(2*sizeof(float)):0;
#else
    const uint32_t queued=hmm_core->pcm_queued_ms?hmm_core->pcm_queued_ms()*rate/1000:0;
#endif
    uint32_t written=written_frames;return written>queued?written-queued:0;
}
bool effect_playing(const Effect &e){return e.active||(e.until&&int32_t(e.until-audible_frames())>0);}
void close_song(){if(song){decoder->close(song);song=nullptr;}playing=false;}
void clear_queue(){
#ifdef HMM2_HOST
    if(device)SDL_ClearQueuedAudio(device);
#else
    if(ring_lock){hmm_core->mutex_lock(ring_lock);ring_read=ring_write=ring_count=0;hmm_core->mutex_unlock(ring_lock);}
    if(pcm_started)hmm_core->pcm_flush();
#endif
}
// Клод: звук занимал 40 % команд потока игры (трасса QEMU, GateHell): OGG 25 %, остальное микшер. Теперь весь он в задаче на ядре 0:
// декодирование, микшер, чтение музыки и звуков. Команды потока игры короткие и идут под этим замком (ring_lock).
#ifdef HMM2_HOST
struct AudioLock {AudioLock(){}};
#else
// Клод: audio_lock_owner — поток, держащий замок (threadptr): освобождению памяти по нехватке (hmm_audio_drop_loops) надо знать,
// не держит ли его уже сам вызвавший — тогда буферы отдаются без повторного захвата.
uintptr_t thread_id(){uintptr_t id;__asm__ volatile("rur.threadptr %0":"=a"(id));return id;}
volatile uintptr_t audio_lock_owner;
struct AudioLock {AudioLock(){if(ring_lock){hmm_core->mutex_lock(ring_lock);audio_lock_owner=thread_id();}}~AudioLock(){if(ring_lock){audio_lock_owner=0;hmm_core->mutex_unlock(ring_lock);}}};
Track *open_pending;uint32_t open_resume;uintptr_t audio_task_id;
#endif
#ifdef HMM2_HOST
bool output_room(){
    if(test_clock())return int32_t(mixed_frames-audible_frames())<int32_t(rate*100/1000);
    return device&&SDL_GetQueuedAudioSize(device)<rate*2*sizeof(float)*220/1000;
}
#endif
void output_block(unsigned n){
    mixed_frames+=n;
#ifdef HMM2_HOST
    if(muted)std::memset(pcm,0,n*2*sizeof(float));
    if(capture)std::fwrite(pcm,2*sizeof(float),n,capture);
    if(!test_clock())SDL_QueueAudio(device,pcm,n*2*sizeof(float));written_frames+=n;
#else
    // Клод: блок сразу отдаётся ядру: задача одна, кольцо между ней и самой собой не нужно.
    if(muted)std::memset(pcm,0,n*2*sizeof(float));
    unsigned sent=0;while(sent<n&&running){int got=hmm_core->pcm_write(pcm+sent*2,n-sent);if(got<0){running=false;break;}if(!got)hmm_core->delay(1);else {sent+=unsigned(got);written_frames+=unsigned(got);}}
#endif
}
// Клод: страница Ogg, на которой звук дошёл до target: поиск половинным делением по номерам кадров в заголовках страниц.
// Читаются окна 8 КиБ в ещё пустое кольцо; страницы музыки ~4 КиБ (~0,2 с), поэтому хватает 7–8 чтений.
uint32_t music_resume_page(uint32_t target,uint32_t *granule){
    uint32_t low=0,high=input_size;*granule=0;
    while(high-low>16384){
        const uint32_t middle=low+(high-low)/2,count=std::min<uint32_t>(8192,input_size-middle);
        if(hmm_remote_read(input_base+middle,music_data,count)!=count)hmm_fail("OGG short SD read");
        uint32_t at=0;uint64_t page=~UINT64_C(0);
        for(;at+27<=count;++at){
            if(std::memcmp(music_data+at,"OggS",4)||music_data[at+4]||(music_data[at+5]&~7))continue;
            std::memcpy(&page,music_data+at+6,8);if(page!=~UINT64_C(0))break;
        }
        if(at+27>count||page==~UINT64_C(0)||page>target)high=middle;
        else {low=middle+at;*granule=uint32_t(page);}
    }
    return low;
}
// Клод: чтение игры в полёте — задача звука ждёт его конца: декодеру без первых данных открыть файл нечем.
bool fill_now(){for(unsigned t=0;t<2000&&hmm_audio_busy();++t)hmm_core->delay(1);return music_fill();}
// Клод: куда смотреть в DAT — на потоке игры: hmm_fopen берёт память из кучи игры.
bool open_file(Track &track){
    hmm_file *f=hmm_fopen(track.path.c_str(),"rb");if(!f)return false;
    hmm_fseek(f,0,2);const long size=hmm_ftell(f);input_base=hmm_file_offset(f);hmm_fclose(f);
    if(size<=0)return false;
    input_size=uint32_t(size);return true;
}
bool open_stream(Track &track,uint32_t resume){
    close_song();
    input_pos=input_head=pass_start=0;host.stream_offset=0;
    uint32_t start=0,granule=0;bool seek=resume!=0;
#ifdef HMM2_HOST
    if(test_direct)seek=false;
#endif
    // Клод: возврат музыки (замок, бой, меню) без декодирования пропущенного: на плате оно занимало поток игры на десятки секунд,
    // и курсор стоял. OGG.ELF продолжает с любой страницы (как перемотка плеера), но ему нужны заголовки этого файла;
    // он сохраняет их, когда файл открыт с начала, и держит один набор.
    if(seek&&headers_track!=&track){
        headers_track=nullptr;
        for(unsigned i=0;i<2&&fill_now();++i){}
        song=decoder->open(input_size,nullptr);
        for(unsigned i=0;song&&i<32;++i){uint32_t got=0;const int result=decode(pcm,1,&got);if(got){headers_track=&track;break;}if(result==ZM_RENDER_END||result==ZM_RENDER_ERROR)break;fill_now();}
        if(song){decoder->close(song);song=nullptr;}
        input_pos=input_head=0;seek=headers_track==&track;
    }
    if(seek)start=music_resume_page(resume,&granule);
    if(!start){headers_track=nullptr;granule=0;}
    input_pos=input_head=start;host.stream_offset=start;
    music_hint(input_base+start,input_size-start);
    // Codex: первая порция до старта декодера; дальнейшая предвыборка идёт между его вызовами.
    for(unsigned i=0;i<2&&fill_now();++i){}
    song=decoder->open(input_size,nullptr);music_frames=resume_granule=granule;resume_target=resume;pending_resume=0;
#ifdef HMM2_HOST
    if(test_direct)pending_resume=resume;
#endif
    music_channels=2;
    playing=song!=nullptr;if(playing)log_audio("AUDIO track=%u mode=%u resume=%u bytes=%u volume=%d",unsigned(current_uid),unsigned(current_mode),resume,input_size,music_volume);else log_audio("AUDIO decoder allocation failed");return playing;
}
bool open_track(Track &track,uint32_t resume){close_song();return open_file(track)&&open_stream(track,resume);}
void position(Effect &e,int16_t angle,uint8_t distance){int a=angle%360;if(a<0)a+=360;float attenuation=(255-distance)/255.0f;
    // Codex: панорама SDL_mixer: впереди оба канала, справа затухает левый, слева правый.
    int side=a<=180?std::min(a,180-a):-std::min(a-180,360-a);
    e.left=attenuation*(side>0?(90-side)/90.0f:1);e.right=attenuation*(side<0?(90+side)/90.0f:1);
}
void update_sample_cache(){
    if(!sample_changed)return;sample_changed=false;unsigned active=0;
    for(unsigned i=0;i<channel_count;++i)active+=effects[i].active;
    const unsigned width=active>8?512:2048;
    if(width!=sample_width){sample_width=width;sample_count=sample_bytes/width;for(auto &page:sample_pages)page.n=0;}
}
constexpr uint32_t loop_cache_max=128*1024;std::atomic<uint32_t> loop_cache_bytes{0};
LoopBuffer loop_buffers[channels_max];uint8_t *loop_region;unsigned loop_capacity,loop_generation;
// Codex: без активных петель область временно хранит фон боя. Новый звук отзывает
// её поколение до первой записи; выдача и запуск петель происходят в игровом потоке.
// Codex: поток звука только освобождает петли. Атомарный ноль разрешает заём
// без ожидания микшера при каждом чтении тени; указатель и поколение меняет игра.
extern "C" void *hmm_audio_idle_region(unsigned *bytes,unsigned *generation){
    *bytes=loop_capacity;*generation=loop_generation;
    return !loop_capacity||loop_cache_bytes.load(std::memory_order_acquire)?nullptr:loop_region;
}
// Codex: все петли живут в области мира. Под AudioLock можно уплотнить занятые
// интервалы и поправить data: каналы ссылаются на LoopBuffer, а не на его байты.
// Остановка канала освобождает интервал внутри области, без free PSRAM.
LoopBuffer *loop_alloc(uint32_t offset,uint32_t n){
    for(auto &b:loop_buffers)if(b.users&&b.offset==offset&&b.size==n){++b.users;return &b;}
    if(n>loop_cache_max||loop_cache_bytes+n>loop_capacity)return nullptr;
    LoopBuffer *slot=nullptr,*ordered[channels_max];unsigned count=0;
    for(auto &b:loop_buffers)if(b.users)ordered[count++]=&b;else if(!slot)slot=&b;
    if(!slot)return nullptr;
    ++loop_generation;
    // Codex: не больше 32 петель; вставки сохраняют порядок адресов без общего introsort.
    for(unsigned i=1;i<count;++i){auto *p=ordered[i];unsigned j=i;while(j&&ordered[j-1]->data>p->data){ordered[j]=ordered[j-1];--j;}ordered[j]=p;}
    uint8_t *end=loop_region;
    for(unsigned i=0;i<count;++i){auto &b=*ordered[i];if(b.data!=end){std::memmove(end,b.data,b.filled);b.data=end;}end+=b.size;}
    slot->offset=offset;slot->size=n;slot->filled=0;slot->data=end;slot->users=1;
    loop_cache_bytes+=n;hmm_loop_bytes=loop_cache_bytes;return slot;
}
/* Клод: звуки целиком в памяти, как у оригинала. Прежде эффект (шаг героя, подбор, петли карты) дочитывался с SD страницами по 2 КиБ
 * прямо в задаче звука во время смешивания: стенд в условиях платы — 110–290 КБ чтений звуков на 100 кадров хода, один и тот же шаг
 * перечитывался каждый шаг, а пока канал SD занят графикой, задача звука ждала его, и звук рвался (десятки обрывов на ход).
 * Теперь звук читается один раз целиком в потоке игры при запуске (Mixer::Play) в постоянную область мира (hmm_audio_pool),
 * и смешивается только из памяти. Занятые каналами звуки не вытесняются; вытесняются давно не звучавшие. Не влез — прежний путь. */
constexpr uint32_t sound_pool_bytes=192*1024;constexpr unsigned sound_slots_max=24;
uint8_t *sound_pool;LoopBuffer sound_slots[sound_slots_max];uint32_t sound_clock;
// Только под AudioLock: свободный участок n байт в области; при нехватке вытесняются давно не звучавшие свободные звуки.
LoopBuffer *sound_place(uint32_t n){
    if(!sound_pool||!n||n>sound_pool_bytes)return nullptr;
    for(;;){
        uint32_t at=0;bool found=false;
        while(!found){
            found=true;
            for(auto &x:sound_slots)if(x.data&&at<uint32_t(x.data-sound_pool)+((x.size+3)&~3u)&&uint32_t(x.data-sound_pool)<at+n){at=uint32_t(x.data-sound_pool)+((x.size+3)&~3u);found=false;}
            if(at+n>sound_pool_bytes)break;
        }
        if(found&&at+n<=sound_pool_bytes){for(auto &x:sound_slots)if(!x.data){x=LoopBuffer{};x.data=sound_pool+at;x.pooled=true;return &x;}}
        LoopBuffer *victim=nullptr;
        for(auto &x:sound_slots)if(x.data&&!x.users&&(!victim||x.stamp<victim->stamp))victim=&x;
        if(!victim)return nullptr;*victim=LoopBuffer{};
    }
}
// Поток игры: звук целиком из области или прочитанный сейчас; nullptr — область занята или нет её.
LoopBuffer *sound_get(uint32_t offset,uint32_t n){
    LoopBuffer *b=nullptr;
    {AudioLock lock;
        for(auto &x:sound_slots)if(x.data&&x.offset==offset&&x.size==n&&x.filled==n){++x.users;x.stamp=++sound_clock;return &x;}
        b=sound_place(n);if(!b)return nullptr;b->offset=offset;b->size=n;b->filled=0;b->users=1;
    }
    const bool ok=hmm_package_read(offset,b->data,n)==n;
    AudioLock lock;
    if(!ok){*b=LoopBuffer{};return nullptr;}
    hmm_sfx_bytes+=n;b->filled=n;b->stamp=++sound_clock;return b;
}
// Клод: только под AudioLock: смешивание читает whole в задаче звука. Буфер освобождается с последним каналом.
void loop_release(Effect &e){
    if(!e.whole)return;
    LoopBuffer &b=*e.whole;e.whole=nullptr;
    if(--b.users||b.pooled)return;
    loop_cache_bytes-=b.size;hmm_loop_bytes=loop_cache_bytes;b=LoopBuffer{};
}
float sample(Effect &e){
    if(e.pos>=e.size){if(!e.loop){e.active=false;sample_changed=true;return 0;}e.pos=0;}
    if(e.whole&&e.whole->filled==e.size){const uint8_t value=e.whole->data[e.pos];e.fraction+=e.sample_rate;e.pos+=e.fraction/rate;e.fraction%=rate;return (int(value)-128)/128.0f;}
    const uint32_t at=e.offset+e.pos;auto *page=&sample_pages[e.page];
    if(at-page->at>=page->n){
        unsigned pick=sample_width==512?unsigned(&e-effects):0;
        for(unsigned i=0;i<sample_count;++i){
            if(at-sample_pages[i].at<sample_pages[i].n){pick=i;break;}
            if(sample_width!=512&&(!sample_pages[i].n||(sample_pages[pick].n&&sample_pages[i].age<sample_pages[pick].age)))pick=i;
        }
        page=&sample_pages[pick];e.page=pick;
        if(at-page->at>=page->n){
            const unsigned start=e.pos&~(sample_width-1);page->at=e.offset+start;
            page->n=std::min<uint32_t>(sample_width,e.size-start);
            if(hmm_package_read(page->at,sample_data+pick*sample_width,page->n)!=page->n){page->n=0;e.active=false;return 0;}
            hmm_sfx_bytes+=page->n;
        }
        page->age=++sample_age;
        // Клод: первый проход петли идёт по страницам подряд — каждую следующую копируем в буфер петли.
        if(e.whole&&e.whole->filled<e.size&&page->at==e.offset+e.whole->filled){
            LoopBuffer &b=*e.whole;const uint32_t n=std::min<uint32_t>(page->n,e.size-b.filled);std::memcpy(b.data+b.filled,sample_data+e.page*sample_width,n);b.filled+=n;
        }
    }
    const uint8_t value=sample_data[e.page*sample_width+at-page->at];
    e.fraction+=e.sample_rate;e.pos+=e.fraction/rate;e.fraction%=rate;return (int(value)-128)/128.0f;
}
// Клод: канал за блок одним циклом. Прежде на каждый отсчёт каждого канала — вызов sample() с поиском окна и целочисленным
// делением: 5 зацикленных звуков карты — 220 тысяч вызовов в секунду на потоке игры. sample() остался только на границах окна.
// Значения те же: таблица хранит то же выражение, шаг позиции — то же деление, выраженное вычитанием.
float sample_table[256];int sample_table_volume=-1;
void mix_effect(Effect &e,unsigned frames){
    if(sample_table_volume!=sound_volume){sample_table_volume=sound_volume;for(int v=0;v<256;++v)sample_table[v]=(v-128)/128.0f*(sound_volume/100.0f);}
    const float left=e.left,right=e.right,*table=sample_table;const uint32_t step=e.sample_rate;
    for(unsigned i=0;i<frames&&e.active;){
        const auto &page=sample_pages[e.page];const uint32_t at=e.offset+e.pos;
        // Клод: петля целиком в буфере — источник весь звук, иначе текущая страница кэша.
        const bool whole=e.whole&&e.whole->filled==e.size;
        if(e.pos<e.size&&(whole||at-page.at<page.n)){
            const uint32_t end=whole?e.offset+e.size:std::min(page.at+page.n,e.offset+e.size);uint32_t pos=at,fraction=e.fraction;
            const uint8_t *data=whole?e.whole->data-e.offset:sample_data+e.page*sample_width-page.at;float *out=pcm+i*2;unsigned n=frames-i;
            while(n&&pos<end){const float v=table[data[pos]];out[0]+=v*left;out[1]+=v*right;out+=2;--n;fraction+=step;while(fraction>=rate){fraction-=rate;++pos;}}
            e.pos=pos-e.offset;e.fraction=fraction;i=frames-n;
            if(!n)break;
        }
        const float v=sample(e)*(sound_volume/100.0f);if(!e.active)e.until=mixed_frames+i;pcm[i*2]+=v*left;pcm[i*2+1]+=v*right;++i;
    }
    sample_pages[e.page].age=++sample_age;
}
void render_block(){
    // Клод: буфер петли отдаётся, как только звук остановлен (render_block идёт под AudioLock).
    for(auto &e:effects)if(!e.active&&e.whole)loop_release(e);
    update_sample_cache();
    std::memset(pcm,0,sizeof(pcm));unsigned frames=block;
    unsigned filled=0;
    for(unsigned attempt=0;playing&&!pending_resume&&filled<block&&attempt<4;++attempt){
        uint32_t got=0;int result=decode(pcm+filled*2,block-filled,&got);zm_info_t info{};decoder->info(song,&info);
        // Клод: файл, открытый с начала и уже давший звук: его заголовки теперь у OGG.ELF, возврат к нему не откроет его дважды.
        if(got&&!host.stream_offset)headers_track=&tracks[uint32_t(current_uid)];
        if(info.rate&&info.rate!=rate){log_audio("AUDIO unsupported original rate=%u",info.rate);close_song();break;}
        if(info.channels)music_channels=info.channels;
        if(music_channels==1)for(unsigned i=got;i-->0;){pcm[(filled+i)*2]=pcm[filled*2+i];pcm[(filled+i)*2+1]=pcm[filled*2+i];}
        const float volume=music_volume/100.0f;
        // Клод: деление на каждый отсчёт — только пока идёт нарастание; после него множитель один на блок.
        if(fade_ms<=0||fade_progress*uint64_t(1000)>=uint64_t(rate)*fade_ms){for(unsigned i=0;i<got*2;i++)pcm[filled*2+i]*=volume;}
        else for(unsigned i=0;i<got;i++){float fade=std::min(1.0f,(fade_progress+i)*1000.0f/(rate*float(fade_ms)));pcm[(filled+i)*2]*=volume*fade;pcm[(filled+i)*2+1]*=volume*fade;}
        music_frames+=got;filled+=got;if(fade_progress<rate*60u)fade_progress+=got;
        if(result==ZM_RENDER_END){if(current_mode==Music::PlaybackMode::PLAY_ONCE){music_until=mixed_frames+filled;close_song();}else if(!restart_pass())open_stream(tracks[uint32_t(current_uid)],0);}
        else if(result==ZM_RENDER_ERROR){log_audio("AUDIO OGG decode error");close_song();}
        // Клод: кольцо OGG опустело, пока игра читала карту: дождаться данных, а не отдавать блок тишины (щелчки и провалы музыки).
        else if(!got&&!fill_now())break;
    }
    // Codex: отсчёты эффектов исходные unsigned PCM 8 бит, 22050 Гц, как LoadWAV OpenHMM2.
    for(unsigned c=0;c<channel_count;c++){auto &e=effects[c];if(e.active)mix_effect(e,frames);}
    for(float &v:pcm)v=std::clamp(v,-1.0f,1.0f);output_block(frames);
}
#ifdef HMM2_HOST
void fill_queue(){
    if(!valid||!running)return;
    AudioServiceScope service;
    for(unsigned i=0;i<16&&output_room();++i)render_block();
}
#else
void output_task(void*){
    __asm__ volatile("rur.threadptr %0":"=a"(audio_task_id));
    const zm_pcm_format_t format={rate,2};
    pcm_started=hmm_core->pcm_open(&format)==ZM_OK;
    if(!pcm_started){log_audio("AUDIO PCM open failed");running=false;finished=true;return;}
    bool had_audio=false;
    // Клод: hmm_fail уходит исключением: здесь его некому поймать, кроме этой задачи. Сбой чтения закрывает только текущую песню:
    // прежде задача кончалась и до выхода из игры звука не было (SD timeout в журнале сборки 68). Следующий Music::Play откроет песню заново.
    while(running){
        try {
            const uint32_t queued=hmm_core->pcm_queued_ms?hmm_core->pcm_queued_ms():0;
            if(queued>220){hmm_core->delay(2);continue;}
            if(had_audio&&!queued&&hmm_core->pcm_queued_ms){++starves;log_audio("AUDIO underrun count=%u",starves);}
            AudioLock lock;
            // Клод: признак снимается только после открытия. Иначе isPlaying на это время говорил «не играет», игра запускала трек заново, и позиция шла назад:
            // на плате трек 28 открывался 6 раз подряд через 0,4 с, и музыка прерывалась.
            if(open_pending){open_stream(*open_pending,open_resume);open_pending=nullptr;}
            render_block();had_audio=true;
            // Codex: готовые PCM имеют приоритет; кольцо пополняется заранее отдельными порциями.
            if(playing&&input_available()<=music_bytes-16384)music_fill();
        }catch(...){AudioLock lock;open_pending=nullptr;close_song();hmm_core->delay(100);}
    }
    hmm_core->pcm_flush();hmm_core->pcm_close();pcm_started=false;finished=true;
}
#endif
}
extern "C" int hmm_audio_self(){
#ifdef HMM2_HOST
    return 0;
#else
    uintptr_t id;__asm__ volatile("rur.threadptr %0":"=a"(id));return id==audio_task_id;
#endif
}
extern "C" void hmm_audio_tick(){
    if(!valid||servicing||hmm_audio_busy())return;
    AudioServiceScope service;
    hmm_audio_manager_tick();
#ifdef HMM2_HOST
    // Клод: только проверка декодера напрямую (HMM2_AUDIO_DIRECT); в игре возврат идёт через music_resume_page.
    for(unsigned i=0;playing&&pending_resume&&i<64;++i){uint32_t got=0;int result=decode(pcm,std::min<uint32_t>(block,pending_resume),&got);pending_resume-=std::min(pending_resume,got);music_frames+=got;if(result==ZM_RENDER_END||result==ZM_RENDER_ERROR){pending_resume=0;close_song();}}

    fill_queue();
    // Codex: готовые PCM имеют приоритет; кольцо пополняется заранее отдельными порциями.
    if(playing&&input_available()<=music_bytes-16384)music_fill();
#endif
}
// Codex: частые безопасные точки не должны заново гонять весь микшер на каждом спрайте.
extern "C" void hmm_audio_poll(){
    if(!valid||servicing||hmm_audio_busy())return;
    static uint32_t previous;const uint32_t now=uint32_t(hmm_millis());
    if(now-previous>=8){previous=now;hmm_audio_tick();}
}
extern "C" void hmm_audio_shutdown(){Audio::Quit();}
/* Клод: нехватка памяти игры (hmm_cache_trim, отказ malloc): все буферы петель отдаются сразу, петли доиграют страницами кэша семплов.
 * Смешивание читает их под AudioLock: если замок держит вызвавший поток (поток игры внутри Music::Play, задача звука в render_block),
 * отдаём без захвата, иначе берём замок. Сборка 74 на плате: петли не отдавались, и вход в бой падал с bad_alloc при 88 КиБ свободных. */
extern "C" void hmm_audio_drop_loops(){
    if(!loop_cache_bytes)return;
    log_audio("AUDIO loops released bytes=%u",unsigned(loop_cache_bytes));
#ifndef HMM2_HOST
    if(audio_lock_owner!=thread_id()){AudioLock lock;for(auto &e:effects)loop_release(e);return;}
#endif
    for(auto &e:effects)loop_release(e);
}
extern "C" void hmm_audio_plan(void *region,unsigned bytes){
    AudioLock lock;for(auto &e:effects)loop_release(e);
    audio_memory_plan(region!=nullptr);
    ++loop_generation;
    loop_region=static_cast<uint8_t*>(region);loop_capacity=bytes;
    for(auto &e:effects)if(e.active&&e.loop)e.whole=loop_alloc(e.offset,e.size);
}
namespace Audio {
void Init(){if(valid)return;
#ifdef HMM2_HOST
    ring=host_ring;music_data=host_music;sample_data=host_samples;test_direct=std::getenv("HMM2_AUDIO_DIRECT")!=nullptr;
#else
    // Codex: упакованные инициализаторы уже восстановлены; их копия больше не используется.
    // Клод: 128 КиБ OGG + 16 КиБ SFX занимают прежнюю PSRAM ELF, без нового выделения. Кольца PCM нет: блоки сразу идут ядру. Его 32 КиБ отданы кэшу окон DAT (hmm_files.c).
    static_assert(music_bytes+sample_bytes==HMM_RECYCLED_AUDIO_BYTES);
    auto *memory=hmm_recycled_data(HMM_RECYCLED_AUDIO_BYTES);
    if(!memory){log_audio("AUDIO recycled PSRAM unavailable");return;}
    music_data=memory;sample_data=memory+music_bytes;
#endif
    ring_read=ring_write=ring_count=0;input_base=input_size=input_pos=input_head=pass_start=0;
    for(auto &page:sample_pages)page.n=0;
#ifdef HMM2_HOST
    if(std::getenv("HMM2_TEST_AUDIO_SILENT"))return;test_epoch=hmm_millis();if(const char *path=std::getenv("HMM2_AUDIO_CAPTURE"))capture=std::fopen(path,"wb");
#endif
    mixed_frames=0;written_frames=0;music_until=0;host={};host.size=sizeof(host);host.abi_major=ZM_SOURCE_ABI_MAJOR;host.abi_minor=ZM_SOURCE_ABI_MINOR;host.alloc=audio_alloc;host.free=audio_free;host.log=log_audio;host.input_read=input_read;host.input_available=input_available;host.input_ended=input_ended;host.resource=resource;host.cover=cover;host.file_type="OGG";
    int entry=0;
#ifdef HMM2_HOST
    char name[]="OGG.ELF";char *args[]={name,reinterpret_cast<char*>(&host),reinterpret_cast<char*>(&decoder)};entry=hmm_ogg_main(3,args);
    if(entry!=ZM_SOURCE_ABI_MAJOR)return;
    if(!test_clock()){SDL_InitSubSystem(SDL_INIT_AUDIO);SDL_AudioSpec wanted{};wanted.freq=rate;wanted.format=AUDIO_F32SYS;wanted.channels=2;wanted.samples=block;device=SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);if(!device){log_audio("AUDIO SDL output failed");return;}SDL_PauseAudioDevice(device,0);}running=true;
#else
    if(!hmm_core->module_open||hmm_core->module_open("OGG.ELF",&host,&decoder,&module,&entry)!=ZM_OK||entry!=ZM_SOURCE_ABI_MAJOR){log_audio("AUDIO OGG.ELF missing from ESP cache");return;}
    if(!decoder||decoder->abi_major!=ZM_SOURCE_ABI_MAJOR){hmm_core->module_close(module);module=nullptr;return;}
    ring_lock=hmm_core->mutex_create();if(!ring_lock)return;running=true;finished=false;
    // Клод: приоритет ниже прежних 5: декодер занимает ядро заметно дольше, чем прежняя подача PCM; очередь ядра держит 220 мс.
    if(hmm_core->task_start(output_task,nullptr,5632,4,0)!=ZM_OK){running=false;finished=true;hmm_core->module_close(module);module=nullptr;return;}
#endif
    valid=true;log_audio("AUDIO OGG=%s/%s PCM=%u SFX=%u",decoder->name,decoder->version,rate,channel_count);
}
void Quit(){if(!valid&&!module)return;valid=false;
#ifdef HMM2_HOST
    close_song();for(auto &e:effects){e.active=false;loop_release(e);}running=false;
    if(device){SDL_CloseAudioDevice(device);device=0;}if(capture){std::fclose(capture);capture=nullptr;}
#else
    running=false;while(!finished)hmm_core->delay(1);close_song();for(auto &e:effects){e.active=false;loop_release(e);}if(module)hmm_core->module_close(module);module=nullptr;
#endif
    audio_memory_plan(false);for(auto &track:tracks)track={};log_audio("AUDIO stopped starves=%u",starves);
}
void Mute(){muted=true;}void Unmute(){muted=false;}bool isValid(){return valid&&running;}
}
namespace Mixer {
void SetChannels(int n){sample_changed=true;channel_count=unsigned(std::clamp(n,0,int(channels_max)));for(unsigned i=channel_count;i<channels_max;i++)effects[i].active=false;}
int getChannelCount(){return int(channel_count);}
int Play(const uint8_t *p,uint32_t n,bool loop,std::optional<std::pair<int16_t,uint8_t>> pos){if(!Audio::isValid()||!p)return -1;
    // Клод: звук ролика — дескриптор с путём OGG (hmm_clip.cpp): идёт путём музыки с её кольцом и кэшем Evo.
    // Как у оригинала, громкость у него от звуков и нарастания нет; музыка получает свою громкость обратно в clip_finish.
    if(n==40){Music::Play(clip_track,std::string(reinterpret_cast<const char*>(p)),Music::PlaybackMode::PLAY_ONCE);if(clip_music_volume<0)clip_music_volume=music_volume;music_volume=sound_volume;fade_progress=rate*60;return 0;}
    unsigned pick=0;while(pick<channel_count&&effect_playing(effects[pick]))++pick;if(pick==channel_count)return -1;
    if(n!=16)return -1;uint32_t desc[4];std::memcpy(desc,p,16);if(desc[0]!=0x31584653u)return -1;
    if(desc[1]>hmm_package_size||desc[2]>hmm_package_size-desc[1]||!desc[2]||!desc[3])return -1;
    // Клод: звук целиком — до замка каналов: чтение с SD идёт в потоке игры, задача звука его не ждёт (sound_get).
    LoopBuffer *whole=sound_get(desc[1],desc[2]);
    AudioLock lock;Effect &e=effects[pick];loop_release(e);e={};e.page=pick;e.loop=loop;e.offset=desc[1];e.size=desc[2];e.sample_rate=desc[3];
    sample_changed=true;e.active=true;
    e.whole=whole?whole:loop?loop_alloc(e.offset,e.size):nullptr;position(e,pos?pos->first:0,pos?pos->second:0);return int(pick);
}
void setVolume(int v){sound_volume=std::clamp(v,0,100);}void setPosition(int c,int16_t a,uint8_t d){if(c>=0&&unsigned(c)<channel_count)position(effects[c],a,d);}
void Stop(int c){sample_changed=true;if(c<0){for(auto &e:effects){e.active=false;e.until=0;}if(clip_music_volume>=0)Music::Stop();}else if(unsigned(c)<channel_count){effects[c].active=false;effects[c].until=0;}}
bool isPlaying(int c){return c>=0&&unsigned(c)<channel_count&&effect_playing(effects[c]);}
}
namespace Music {
bool Play(uint64_t uid,PlaybackMode mode){const uint32_t id=uint32_t(uid);if(id>=64||tracks[id].path.empty())return false;
    AudioServiceScope service;
    Stop();AudioLock lock;if(id!=clip_track)clip_finish();current_uid=uid;current_mode=mode;music_until=0;fade_progress=0;
    const uint32_t resume=mode==PlaybackMode::RESUME_AND_PLAY_INFINITE?tracks[id].position:0;
#ifdef HMM2_HOST
    open_track(tracks[id],resume);
#else
    // Клод: открытие и поиск места — в задаче звука: игра не ждёт ни заголовков OGG, ни карты.
    if(open_file(tracks[id])){open_resume=resume;open_pending=&tracks[id];}
#endif
    return true;}
void Play(uint64_t uid,const std::string &path,PlaybackMode mode){const uint32_t id=uint32_t(uid);if(id>=64)return;tracks[id].path=path;Play(uid,mode);}
void Play(uint64_t,const std::vector<uint8_t>&,PlaybackMode){log_audio("AUDIO MIDI source unavailable; select external music");}
void setVolume(int v){music_volume=std::clamp(v,0,100);
#ifdef HMM2_HOST
    log_audio("AUDIO music volume=%d",music_volume);
#endif
}void SetFadeInMs(int ms){fade_ms=std::max(0,ms);}
void Stop(){AudioLock lock;
#ifndef HMM2_HOST
    open_pending=nullptr;
#endif
    if(valid)music_hint(0,0);music_until=0;if(uint32_t(current_uid)==clip_track)clip_finish();if(playing){tracks[uint32_t(current_uid)].position=current_mode==PlaybackMode::RESUME_AND_PLAY_INFINITE?(music_frames==resume_granule&&resume_target?resume_target:pending_resume?music_frames+pending_resume:(music_frames>mixed_frames-audible_frames()?music_frames-(mixed_frames-audible_frames()):0)):0;close_song();}}
bool isPlaying(){
#ifndef HMM2_HOST
    if(open_pending)return true;
#endif
    return playing||(music_until&&int32_t(music_until-audible_frames())>0);}void setMidiSoundFonts(const ListFiles&){}void setMidiTimidityCfg(const std::string&){}
}

#ifdef HMM2_HOST
extern "C" unsigned hmm_audio_test_frames(){return mixed_frames;}
extern "C" unsigned hmm_audio_test_resume(){return resume_target;}
extern "C" void hmm_audio_test_ring(unsigned *loaded,unsigned *consumed,unsigned *capacity){*loaded=input_head;*consumed=input_pos;*capacity=music_bytes;}
#endif
/* Клод: область звуков — по плану памяти мира (hmm_memory_plan), после загрузки мира, и отдаётся перед следующей загрузкой. При старте
 * она разбивала PSRAM: GateHell не получал сплошные 1,41 МБ под тайлы мира (стенд QEMU, bad_alloc при загрузке). Нет места — звуки
 * идут прежним путём страниц. Отдача останавливает звуки, играющие из области. */
extern "C" void hmm_audio_pool(int enable){
    if(enable){
        if(sound_pool)return;
#ifdef HMM2_HOST
        sound_pool=static_cast<uint8_t*>(std::malloc(sound_pool_bytes));
#else
        void *probe=hmm_core->alloc(sound_pool_bytes+256u*1024u,ZM_MEM_PSRAM);if(!probe)return;hmm_core->free(probe);
        sound_pool=static_cast<uint8_t*>(hmm_core->alloc(sound_pool_bytes,ZM_MEM_PSRAM));
#endif
        return;
    }
    if(!sound_pool)return;
    {AudioLock lock;for(auto &e:effects)if(e.whole&&e.whole->pooled){e.active=false;loop_release(e);}for(auto &x:sound_slots)x=LoopBuffer{};}
#ifdef HMM2_HOST
    std::free(sound_pool);
#else
    hmm_core->free(sound_pool);
#endif
    sound_pool=nullptr;
}
