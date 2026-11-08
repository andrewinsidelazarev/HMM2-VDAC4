/* Codex: ESP собирает слои и DL; готовые атласы DAT загружает Z80 через SPI FT812.
 * CPU-участки создаются только при запросе пикселей неподдержанной операцией.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
extern "C" {extern size_t hmm_heap_mid_bytes,hmm_heap_mid_peak;}
#include "hmm_resources.h"
#include <atomic>
#include "image.h"
#include "screen.h"
#include "localevent.h"
#include "hmm_loading.h"
#include "hmm_audio.h"
#include "icn.h"
#include "agg_image.h"
extern "C" {
#include "hmm_core.h"
#include "ft_sprite.h"
}
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
extern const uint8_t *hmm_image_transform_table();
#include "hmm_render_worker.inc"
namespace {void message_fade_open(bool);void message_fade_close();void message_scene_capture();void message_fade_reset();}
#include "hmm_canvas.inc"
namespace {
const uint8_t *hmm_transform_table(){return hmm_image_transform_table();}
hft_cache textures;
#ifdef HMM2_HOST
constexpr unsigned DL_CAPACITY=8192;
const unsigned DL_LIMIT=std::getenv("HMM2_ORACLE_WIDE_DL")?8192:2048;
constexpr unsigned DL_HISTORY_CAPACITY=8192;
#else
constexpr unsigned DL_CAPACITY=4096;
constexpr unsigned DL_LIMIT=2048,DL_HISTORY_CAPACITY=2048;
#endif
uint32_t dl[DL_CAPACITY],lastdl[DL_HISTORY_CAPACITY],state[64],palette_rgb[256];
bool valid[64];
unsigned words,lastwords,serial,displayed,traffic,link_ms,frame_bytes,frame_link,prepare_ms;bool dl_overflow;
unsigned gpu_upload_bytes,palette_epoch,palette_mapping=~0u;uint32_t palette_location,palette_generation[HFT_ENTRIES];
uint16_t palette_entry_by_page[HFT_PAGES];
uint32_t command_position,dl_address;
bool copro_unsettled;
uint8_t last_palette[768];
// Codex: содержимое двух непоказанных по очереди палитровых банков для цикла воды.
uint32_t palette_bank_rgb[2][256];unsigned palette_bank_epoch[2];
struct Handle {uint32_t address=~0u;unsigned stride=0,height=0;uint32_t size=~0u,high=~0u;unsigned used=0;};
Handle handles[32];unsigned next_handle,handle_count;
// Codex: номера ручек прошлого плотного DL — подсказка для следующего кадра.
// Каждое поле проверяется заново; геометрия и порядок команд остаются исходными.
std::vector<uint8_t> rehandle_route;unsigned rehandle_words;
bool rehandle_force_full,rehandle_fast_used,body_handle_fast;
MapPart *map_compiling,*message_compiling;MapPart interface_dl;unsigned interface_revision;
MapPart body_plan;unsigned body_screen_revision,body_ring_revision,body_bank;
struct BodyPatch {uint16_t index,kind;};
struct BodyTerminal {uint32_t state[64],bitmap[32][5];uint8_t valid[64],mask[32],camera[64];uint16_t last[64];unsigned handle;};
std::unique_ptr<BodyTerminal> body_terminal;
std::vector<BodyPatch> motion_pending,body_patches;
bool motion_candidate,motion_active,body_motion;
unsigned body_contents_revision,motion_blocked_revision;
int body_tx,body_ty,body_x,body_y,body_cols,body_rows,motion_blocked_x,motion_blocked_y;
Rect body_window;
unsigned body_move_hits;
MapPart ring_sprite_plan;
struct HeroSlot {int16_t x,y;uint16_t run;};
struct BodyActorSpan {HeroSlot slot;unsigned begin,end;};
std::vector<BodyActorSpan> body_actor_spans;
std::vector<BodyActorSpan> body_spans;
std::vector<HeroSlot> body_actors;
constexpr unsigned BODY_ACTOR_OPS[]={5,4,0x10,0x15,0x17,0x1a,0x1b,0x1c,0x1f,0x2a,0x2b,0x2c};
struct BodyActorGlobals {uint32_t value[12]{},camera=0;uint16_t known=0;};
struct BodyActorBinding {uint32_t before[5],after[5];uint8_t handle,before_known,after_known;};
struct BodyActorState {BodyActorGlobals before,after;std::vector<uint8_t> handles;std::vector<BodyActorBinding> bindings;};
std::vector<BodyActorState> body_actor_states;
int body_actor_handle=-1;
uint32_t body_actor_touched;
bool body_dynamic,body_actor_collect;
unsigned body_base_revision,body_dynamic_hits;
struct MapCluster {int x=0,y=0;bool skeleton=false;std::array<uint64_t,64> keys{};MapPart part;std::vector<Draw> fills;std::vector<HeroSlot> heroes;};
std::array<std::unique_ptr<MapCluster>,16> map_clusters;
bool cluster_compiling,cluster_failed,cluster_used,cluster_retry,cluster_seeded;
bool skeleton_retry,skeleton_used,hero_slot_emitting;
unsigned skeleton_hits,skeleton_builds,skeleton_fallbacks,hero_slot_parts;
unsigned skeleton_blocked_static,skeleton_blocked_screen;
int skeleton_blocked_x,skeleton_blocked_y,skeleton_blocked_cols,skeleton_blocked_rows;
unsigned cluster_hits,cluster_builds,cluster_fallbacks,cluster_blocked_static,cluster_blocked_screen;
int cluster_blocked_x,cluster_blocked_y,cluster_blocked_cols,cluster_blocked_rows;
unsigned ring_display_bank,ring_build_bank,body_hits,body_builds;
bool ring_used,body_palette_fill;
uint16_t map_texture_refs[HFT_ENTRIES];
bool dense_mode;
// Клод: альфа чёрного прямоугольника затемнения поверх экрана (hmm_gpu_fade), 0 — нет.
uint8_t fade_black;
// Клод: последний кадр карты собран из клеток (cache_map_tiles) — RAM_G не держит вариантов анимации, см. hmm_gpu_animation_limited.
bool map_tiles_shown;
// Клод: счётчики клеток плотной карты за 100 кадров для строки журнала HMM2 GPU: клеток в кадрах и загруженных в RAM_G
// (растеризованных процессором).
unsigned stat_draws_peak;
unsigned stat_cells,stat_cell_uploads,stat_live_layers;
// Codex: фон клетки хранится в RAM_G отдельно от динамического хвоста; при отказе
// DL остаётся точный запасной список целых клеток. История движения больше не нужна.
unsigned live_budget=160;Sequence<Draw> tiles_baked;
// Codex: клетки обычного окна карты заменяют только его заместитель. Интерфейс и
// план статических проходов Redraw сохраняются между кадрами.
Sequence<Draw> tile_map_draws;uint64_t tile_map_signature;bool tile_map_active;
void map_tiles_clear(){Sequence<Draw>().swap(tile_map_draws);tile_map_signature=0;tile_map_active=false;}
// Codex: две независимые 32-битные цепочки вместо программного умножения uint64_t
// на каждом поле. Ключ сохраняет 60 бит и включает все влияющие на пиксели поля.
struct TileHash {
    uint32_t lo=2166136261u,hi=0x9e3779b9u;
    void add(uint32_t v){lo=(lo^v)*16777619u;hi=(hi^(v+0x9e3779b9u))*2246822519u;hi=(hi<<13)|(hi>>19);}
    uint64_t key()const{return (uint64_t(hi&0x0fffffffu)<<32)|lo;}
};
// Codex: фрагменты задают полное нужное им состояние. При сборке общего DL
// удаляются повторы, отдельно для каждой ручки FT812; порядок вершин не меняется.
uint32_t compact_state[64],compact_bitmap[32][5];
uint8_t compact_valid[64],compact_mask[32];unsigned compact_handle;
// Codex: параметры ручек живут между DL. Удаляем только неизменные внутри всего листа поля:
// это сохраняет корректность и первого исполнения, и всех повторов неизменного кадра.
uint32_t bitmap_prior[32][5],bitmap_first[32][5],bitmap_last[32][5];
uint8_t bitmap_known[32],bitmap_seen[32],bitmap_varied[32],bitmap_drop[32];bool bitmap_prior_valid;
int bitmap_field(uint32_t w){switch(w>>24){case 1:return 0;case 7:return 1;case 8:return 2;case 0x28:return 3;case 0x29:return 4;default:return -1;}}
void emit_map(const Draw &);
// Codex: активные ручки занимают префикс; новая ручка инициализируется при первом использовании.
void reset_emitter(){std::memset(valid,0,sizeof(valid));next_handle=handle_count=0;}
void map_cache_release(MapPart &p){
    for(auto i:p.textures)if(!--map_texture_refs[i])textures.entries[i].pins&=~4u;
    p.textures.clear();p.commands.clear();p.dirty=true;
}
HMM_SIZE_RENDER void cached_textures_use(const MapPart &part){
#ifdef HMM2_HOST
    static const bool touch=std::getenv("HMM2_NO_CACHED_TEXTURE_TOUCH")==nullptr;
#else
    constexpr bool touch=true;
#endif
    for(auto i:part.textures){auto &entry=textures.entries[i];entry.pins|=1u<<textures.build_slot;
        // Replaying cached commands is real texture use too. Otherwise a
        // just-displayed atlas becomes an old eviction victim when a UI opens.
        if(touch)entry.age=++textures.age;
    }
}
void body_plan_clear(){map_cache_release(body_plan);body_screen_revision=body_ring_revision=0;body_motion=body_dynamic=false;body_patches.clear();body_terminal.reset();body_actors.clear();body_actor_states.clear();body_spans.clear();}
void ring_clusters_clear(bool all){
    for(auto &p:map_clusters)if(p){map_cache_release(p->part);p.reset();}
    if(all){cluster_blocked_static=skeleton_blocked_static=0;cluster_seeded=false;}
}
void ring_sprite_plan_clear(){map_cache_release(ring_sprite_plan);}
void map_clips_clear(){}
/* Клод: карта закрыта целиком (map_covered): её слои, части DL, клетки и прошлый список клеток не нужны до следующего показа
 * (hmm_gpu_map_view запишет всё заново). Память отдаётся в первом же кадре, до выделений боя или города: на плате вход в бой
 * с Alteris забрал свободную PSRAM с 1,09 МБ до 244 КБ, и отказ malloc сбросил кэши. Заместителя карты на экране уже нет. */
bool map_released;
void map_release_covered(){
    ring_clear();hmm_ring_world_clear();
    std::vector<uint8_t>().swap(rehandle_route);rehandle_words=0;
    map_active=false;map_cache_clear();std::vector<MapPart>().swap(map_parts);Sequence<Draw>().swap(map_draws);
    map_spans.clear();std::vector<MapSpan>().swap(map_spans);map_span_position=0;map_plan_valid=map_static_allowed=false;
    tiles_forget();Sequence<Draw>().swap(tiles_baked);
    // Codex: закончилась сцена карты. Свободные кадры и её пиковый scratch
    // не складываются с памятью боя/города; живые снимки остаются по shared_ptr.
    Sequence<CullRect>().swap(cull_rects);release_frame_storage();Sequence<CullRect>::release_unused();
}
void map_cache_clear(){map_clips_clear();for(auto &p:map_parts)map_cache_release(p);map_parts.clear();}
void map_keep_texture(){
    if(!map_compiling)return;
    const auto i=uint32_t(textures.last_entry);auto &v=map_compiling->textures;
    if(std::find(v.begin(),v.end(),i)==v.end()){v.push_back(i);++map_texture_refs[i];textures.entries[i].pins|=4u;}
}
// Клод: курсор текущего вывода кадра (hmm_gpu_cursor из Display::render).
const fheroes2::Image *cursor_image;int cursor_x,cursor_y;
// Codex: координаты только последнего опубликованного курсора. Новый DL строится отдельно.
// Клод: clock=1 — слой часов. Он всегда в списке, ниже экрана на CURSOR_HIDE точек; показ и скрытие — только сдвиг.
struct CursorPatch{uint32_t base;uint16_t index;uint8_t clock;};
// Клод: clock=2 — слой подписи ожидания (status_image): лежит ниже экрана, задача курсора открывает его через STATUS_DELAY_MS.
constexpr unsigned CURSOR_PATCHES=192;CursorPatch cursor_pending[CURSOR_PATCHES],cursor_committed[CURSOR_PATCHES];
unsigned cursor_pending_count,cursor_count;bool cursor_record,cursor_overflow;uint8_t cursor_layer;
int cursor_anchor_x,cursor_anchor_y,cursor_origin_x,cursor_origin_y;unsigned cursor_swaps;
const fheroes2::Image *cursor_clock_image;
// Клод: 0,3 с давали мигание часов в бою: там паузы такой длины идут одна за другой. Часы — только при настоящей остановке.
constexpr int CURSOR_HIDE=1024;constexpr uint32_t CURSOR_CLOCK_MS=1000;
const fheroes2::Image *status_image;int status_x,status_y;bool status_once,status_fresh;
/* Клод: подписи ожидания («Entering castle...», «Battle loading...») — только если ожидание дольше 2 с, и тогда с начала третьей
 * секунды (пользователь, 03.11). Подпись лежит в DL ниже экрана; задача курсора, которая живёт и пока игра занята, открывает её
 * сдвигом, когда с её появления прошло STATUS_DELAY_MS. Сцена готова раньше — следующий кадр уже без подписи, её никто не видел.
 * status_hold — «Map loading...» после загрузки мира: держится, пока кадры карты не станут лёгкими (status_settle). */
// status_reveal_at — когда подпись видна (0 — скрыта); status_emitted_hidden — в каком положении она в опубликованном DL.
constexpr uint32_t STATUS_DELAY_MS=2000;uint32_t status_since;std::atomic<uint32_t> status_reveal_at{0};std::atomic<bool> status_emitted_hidden{false};
bool status_hold;uint32_t status_hold_since,status_last_frame;unsigned status_calm;
// Клод: признаки лёгкого кадра (status_frame_calm, status_frame_at) остались для журнала; «Map loading...» теперь снимается только
// при проявлении карты (hmm_status_hold_arm) или через 60 с — см. конец hmm_gpu_frame.
std::atomic<bool> status_hold_live{false},status_frame_calm{false};std::atomic<uint32_t> status_frame_at{0};
// Клод: на плате кадры карты сразу после загрузки тяжелее, чем на стенде, и «лёгких» кадров подпись ждала до окна вступления
// и дальше (пользователь, 03.11: «перекрывает начало игры»). Теперь она уходит в конце загрузки, когда карта проявлена
// (hmm_status_hold_arm); status_hold_armed и первый опрос ввода (hmm_status_input_seen) — запасной путь снятия.
bool status_hold_armed;
// Клод: игра не должна останавливать курсор: его двигает своя задача (cursor_task). Занятость — два счётчика, а не мьютекс:
// hmm_fail уходит исключением, и забытый мьютекс оставил бы задачу курсора висеть при выходе, а забытый счётчик — нет.
std::atomic<int> gpu_busy{0},cursor_active{0};
struct GpuBusy{GpuBusy(){hmm_gpu_busy_enter();}~GpuBusy(){hmm_gpu_busy_leave();}};
volatile bool cursor_run,cursor_finished=true;
// Клод: на стенде задача включается только сценой проверки курсора: прочие сцены сверяют кадры с виртуальными часами.
#ifdef HMM2_HOST
bool cursor_live(){static const bool live=std::getenv("HMM2_TEST_CURSOR_TASK")!=nullptr;return live;}
#else
constexpr bool cursor_live(){return true;}
#endif
// Codex: обычный DL сам задаёт все параметры использованных ручек. При редком
// переполнении восстанавливаем их конец из последнего такого списка, без прохода на каждом кадре.
void previous_bitmap_state(){
    std::memset(bitmap_known,0,sizeof(bitmap_known));unsigned handle=0;
    for(unsigned i=0;i<lastwords;++i){const auto w=lastdl[i];if((w>>24)==5){handle=w&31;continue;}
        const int f=bitmap_field(w);if(f>=0){bitmap_prior[handle][f]=w;bitmap_known[handle]|=1u<<f;}}
}
void compact_bitmap_constants(){
    std::memset(bitmap_seen,0,sizeof(bitmap_seen));std::memset(bitmap_varied,0,sizeof(bitmap_varied));unsigned scan_handle=0;
    for(unsigned i=0;i<words;++i){const auto w=dl[i];if((w>>24)==5){scan_handle=w&31;continue;}const int f=bitmap_field(w);if(f<0)continue;
        const auto bit=1u<<f;if(!(bitmap_seen[scan_handle]&bit)){bitmap_first[scan_handle][f]=w;bitmap_seen[scan_handle]|=bit;}
        else if(bitmap_first[scan_handle][f]!=w)bitmap_varied[scan_handle]|=bit;bitmap_last[scan_handle][f]=w;}
    for(unsigned h=0;h<32;++h){bitmap_drop[h]=0;if(!bitmap_seen[h])continue;for(unsigned f=0;f<5;++f){const auto bit=1u<<f;if(!(bitmap_seen[h]&bit))continue;
        if(!(bitmap_varied[h]&bit)&&(bitmap_known[h]&bit)&&bitmap_prior[h][f]==bitmap_first[h][f])bitmap_drop[h]|=bit;
        } }
    unsigned out=0,cursor_at=0,handle=0;uint32_t pending=0;bool select=false;
    for(unsigned i=0;i<words;++i){const auto w=dl[i];if((w>>24)==5){handle=w&31;pending=w;select=true;continue;}
        const int f=bitmap_field(w);if(f>=0){if(bitmap_drop[handle]&(1u<<f))continue;if(select){dl[out++]=pending;select=false;}}
        if(cursor_at<cursor_pending_count&&cursor_pending[cursor_at].index==i)cursor_pending[cursor_at++].index=uint16_t(out);
        dl[out++]=w;
    }
    words=out;
}
void put(uint32_t address,const uint32_t *p,unsigned count){
    uint32_t start=hmm_core->millis();
    while(count){unsigned n=std::min(count,1024u);if(hmm_core->ft_write(address,p,n)!=ZM_OK)hmm_fail("FT write failed");traffic+=n*4;address+=n*4;p+=n;count-=n;}
    link_ms+=hmm_core->millis()-start;
}
// Codex: Z80 Render_RuntimeObjectsCmd/Render_MapAnimCmd используют CMD_APPEND.
// RAM_G хранит опубликованные слова; по LINK передаются только изменившиеся полосы.
// Предыдущий DLSWAP уже завершён в hmm_video_frame, а RAM_G блока не читается scanout.
// Клод: кольцо RAM_CMD выделено из append_dl: те же команды шлют ролики (hmm_gpu_clip_decode).
// All callers own the GPU writer guard. Publish the local pointer only after
// REG_CMD_WRITE succeeds: an aborted cursor packet must not expose parameters
// from one command as the beginning of the next one.
#ifndef HMM2_HOST
__attribute__((optimize("Os")))
#endif
HMM_RENDER_NOINLINE bool copro_try_send(const uint32_t *commands,unsigned count){
    const unsigned first=std::min(count,unsigned((4096-command_position)/4));
    const uint32_t next=(command_position+count*4)&4095;
    copro_unsettled=true;
    if(hmm_core->ft_write(0x308000+command_position,commands,first)!=ZM_OK)return false;
    if(first<count&&hmm_core->ft_write(0x308000,commands+first,count-first)!=ZM_OK)return false;
    if(hmm_core->ft_write(0x3020fc,&next,1)!=ZM_OK)return false;
    command_position=next;copro_unsettled=false;return true;
}
void copro_wait();
void copro_send(const uint32_t *commands,unsigned count){
    if(copro_unsettled){
        // A failed register write may already have reached the FPGA. Recover
        // its committed pointer and drain that packet before reusing RAM_CMD.
        uint32_t written;
        if(hmm_core->ft_read(0x3020fc,&written)!=ZM_OK)hmm_fail("FT CMD WRITE LINK failed");
        if((written&3)||written>4092)hmm_fail("FT CMD WRITE invalid");
        command_position=written;copro_wait();copro_unsettled=false;
        hmm_core->log("HMM2: CMD cursor packet recovered at %lu",(unsigned long)written);
    }
    const uint32_t start=hmm_core->millis();
    if(!copro_try_send(commands,count))hmm_fail("FT CMD write failed");
    traffic+=(count+1)*4;link_ms+=hmm_core->millis()-start;
}
void copro_wait(){
    uint32_t read,start=hmm_core->millis();
    for(;;){
        if(hmm_core->ft_read(0x3020f8,&read)!=ZM_OK)hmm_fail("FT CMD READ LINK failed");
        if(read==4095){
            uint32_t written=~0u,length=~0u,mark=~0u;
            hmm_core->ft_read(0x3020fc,&written);hmm_core->ft_read(0x302100,&length);hmm_core->ft_read(0x308000,&mark);
            hmm_core->log("HMM2 CMD fault: wr=%lu local=%lu dl=%lu ring0=%08lx",(unsigned long)written,(unsigned long)command_position,(unsigned long)length,(unsigned long)mark);
            hmm_fail("FT CMD fault");
        }
        if(read==command_position)break;
        if(hmm_core->millis()-start>2000)hmm_fail("FT CMD timeout");
        hmm_core->delay(1);
    }
}
void append_dl(unsigned count){
    const uint32_t commands[]={0xffffff00u,0xffffff1eu,dl_address,count*4};
    copro_send(commands,4);copro_wait();
}
void publish_dl(){
    for(unsigned at=0;at<words;){
        if(at<lastwords&&dl[at]==lastdl[at]){++at;continue;}
        const unsigned begin=at++;unsigned end=at;
        // Codex: короткие неизменные промежутки дешевле послать одним пакетом LINK.
        while(at<words&&at-end<8){if(at>=lastwords||dl[at]!=lastdl[at])end=at+1;++at;}
        put(dl_address+begin*4,dl+begin,end-begin);at=end;
    }
    append_dl(words);
}
#ifdef HMM2_HOST
#define HMM_GPU_INLINE __forceinline
#else
// Shared hot writer keeps the cached-region renderer inside the ELF cache limit.
#define HMM_GPU_INLINE __attribute__((noinline,optimize("O2")))
#endif
// Codex: класс команды определяется одной таблицей, без цепочки сравнений для каждого слова кэшированного DL.
constexpr auto command_kind=[](){std::array<uint8_t,64> k{};
    k[1]=1;k[7]=2;k[8]=3;k[0x28]=4;k[0x29]=5;
    for(unsigned op:{5u,2u,4u,0xbu,0xeu,0x10u,0x16u,0x18u,0x19u,0x27u,0x15u,0x17u,0x1au,0x1bu,0x1cu,0x1fu,0x2au,0x2bu,0x2cu})k[op]=6;
    k[0x21]=7;return k;
}();
HMM_SIZE_RENDER bool bitmap_clipped(uint32_t w){
    // Cached geometry includes the entering/leaving border of the map. Keep
    // its state updates, but do not spend DL vertices on fully clipped quads.
    // Cursor/status vertices must remain: their task can move them later.
    if(!cursor_record&&compact_valid[0x1f]&&compact_state[0x1f]==0x1f000001&&
       compact_valid[0x1b]&&compact_valid[0x1c]&&compact_valid[0x2b]&&compact_valid[0x2c]){
        const unsigned h=(w>>7)&31;
        if((compact_mask[h]&20)==20){
            const auto *b=compact_bitmap[h];const uint32_t vx=compact_state[0x2b],vy=compact_state[0x2c];
            if(!((vx|vy)&15)){
                const int x=int((w>>21)&511)+(int32_t(vx<<15)>>19),y=int((w>>12)&511)+(int32_t(vy<<15)>>19);
                const int bw=int((b[2]>>9)&511)+int((b[4]>>2)&3)*512,bh=int(b[2]&511)+int(b[4]&3)*512;
                const int left=int((compact_state[0x1b]>>11)&2047),top=int(compact_state[0x1b]&2047);
                return x+bw<=left||y+bh<=top||x>=left+int((compact_state[0x1c]>>12)&4095)||y>=top+int(compact_state[0x1c]&4095);
            }
        }
    }
    return false;
}
HMM_GPU_INLINE void word(uint32_t w){
    if(map_compiling){map_compiling->commands.push_back(w);return;}
    if(!motion_active&&ring_optimized()&&(w>>30)==2&&bitmap_clipped(w))return;
    const unsigned op=w>>24;
    if(op<command_kind.size()){
        const unsigned kind=command_kind[op];
        if(kind&&kind<6){
            const unsigned field=kind-1;auto &bits=compact_mask[compact_handle];auto &prior=compact_bitmap[compact_handle][field];
            if((bits&(1u<<field))&&prior==w)return;bits|=1u<<field;prior=w;
        }else if(kind==6){
            if(cursor_record&&(op==0x2b||op==0x2c)){
                compact_valid[op]=0;
                if(cursor_pending_count<CURSOR_PATCHES)cursor_pending[cursor_pending_count++]={w,uint16_t(words),cursor_layer};
                else cursor_overflow=true;
            }
            if(op==5)compact_handle=w&31;
            if(compact_valid[op]&&compact_state[op]==w)return;compact_valid[op]=1;compact_state[op]=w;
        }else if(kind==7)compact_valid[0x1f]=0;
    }
    if(words>=DL_CAPACITY){dl_overflow=true;return;}dl[words++]=w;
}
inline void set(uint32_t w){unsigned op=w>>24;if(op<64){if(valid[op]&&state[op]==w&&!(cursor_record&&(op==0x2b||op==0x2c)))return;state[op]=w;valid[op]=true;}word(w);}
// Tags travel with commands through handle allocation. They are removed before
// publication; no private instruction or enlarged RAM_DL reaches the device.
void camera_word(uint32_t command){
    const unsigned op=command>>24;valid[op]=false;
    if(motion_active)command|=op==0x1a?0x600000u:0x800000u;
    word(command);
}
// Клод: преобразования 6–15 оригинала для FT812 — ARGB (6–9 осветление, 10–13 голубоватое, 14 зеркало, 15 «без цикла» — как есть).
const uint32_t lighten_argb[10]={0x8efff8fd,0x64fffffd,0x3efffffe,0x24fffffa,0x70d9deff,0x5cd2dcff,0x44cadaff,0x2cc4cbff,0x32ffffff,0};
uint32_t palette_address(unsigned mapping){
    if(mapping&0x800000u)return mapping&0x3fffffu;
    const unsigned base_mapping=mapping&~0x400000u;
    if(mapping==palette_mapping)return palette_location;
    // Codex: две версии каждой палитры; предыдущий кадр закрепляет противоположную чётность.
    // Неизменный адрес/цвет берётся один раз за кадр, без новых записей кэша на каждой фазе.
    uint64_t key=UINT64_C(0x2000000000000000)|(uint64_t(mapping)<<1)|(palette_epoch&1);
    uint32_t address;int upload;
    if(!hft_cache_get(&textures,key,1024,&address,&upload))hmm_fail("RAM_G palette capacity");
    palette_entry_by_page[address/HFT_PAGE_BYTES]=uint16_t(textures.last_entry+1);
    if(upload||palette_generation[textures.last_entry]!=palette_epoch){
        const unsigned previous=palette_generation[textures.last_entry];
        palette_generation[textures.last_entry]=palette_epoch;
        // Клод: буферы выгрузки статические — игра однопоточна, а кадр hmm_gpu_frame лежал на самой
        // глубокой цепочке стека (окно постройки в городе, свободно 3320 из 16384 байт).
        static uint32_t p[256];std::memset(p,0,sizeof(p));const auto *translate=hmm_resource_palette();
        for(unsigned i=0;i<256;++i)p[translate[i]]=palette_rgb[base_mapping?mappings[base_mapping-1][i]:i];
        // Клод: у «сплошной» таблицы (контур отряда, hmm_gpu_contour) тени и осветление прозрачны.
        const bool solid=base_mapping&&mapping_solid[base_mapping-1];
        p[0]=0;for(unsigned i=2;i<=5;++i)p[i]=solid?0:uint32_t(153-34*(i-1))<<24;
        /* Клод: точки атласа с преобразованиями 6–15 (tools/patch_ftg_lighten.py) — свободные индексы палитры атласа (не цели
         * перевода, кроме 0 и 2–5), по порядку; цвет и альфа подобраны по transformTable оригинала. FT812 рисует их сам. */
        {bool used[256]={};for(unsigned i=0;i<256;++i)used[translate[i]]=true;used[0]=used[2]=used[3]=used[4]=used[5]=true;
         for(unsigned i=0,t=0;i<256&&t<((mapping&0x400000)?14u:10u);++i)if(!used[i]){
             static const unsigned window_shadow[]={88,60,34,17};
             p[i]=solid?0:t<10?lighten_argb[t]:window_shadow[t-10]<<24;++t;}}
        // Codex: вода меняет цвета, а индексы тайлов остаются в RAM_G. Не посылаем
        // заново всю палитру; незнакомая/вытесненная запись получает полную таблицу.
        unsigned first=0,end=256;const unsigned bank=palette_epoch&1;
        if(!upload&&previous&&previous==palette_bank_epoch[bank]){
            first=256;end=0;
            for(unsigned i=0;i<256;++i){const unsigned color=base_mapping?mappings[base_mapping-1][i]:i;
                if(palette_rgb[color]!=palette_bank_rgb[bank][color]){
                    first=std::min(first,unsigned(translate[i]));end=std::max(end,unsigned(translate[i])+1);
                }
            }
        }
        if(end>first)put(address+first*4,p+first,end-first);
    }
    palette_mapping=mapping;palette_location=address;return address;
}
void palette(unsigned mapping){set(0x2a000000|(map_compiling?mapping:palette_address(mapping)));}
// Codex: только индексы и маска; источник и палитра неизменны до render_wait.
struct PixelJob {const uint8_t *pixels,*mask,*translate;uint32_t *output;unsigned size,valid;};
HMM_RENDER_NOINLINE void hmm_render_job_pixels(void *arg){
    const auto &j=*static_cast<PixelJob*>(arg);auto *dst=reinterpret_cast<uint8_t*>(j.output);
    for(unsigned i=0;i<j.size;++i){const unsigned mask=i<j.valid&&j.mask?j.mask[i]:0;dst[i]=i>=j.valid?0:!mask?j.translate[j.pixels[i]]:mask>=2&&mask<=5?mask:0;}
}
// Клод: missing — текстуре не нашлось места в RAM_G: слой пропускается в этом кадре, а собранные с пропуском части
// карты и интерфейса (texture_missed) пересобираются в следующем. Прежде здесь был hmm_fail («RAM_G atlas capacity»).
struct Texture {uint32_t address;unsigned stride,height,cell;bool missing=false;};
bool texture_missed;
Texture texture_missing(unsigned bytes,const char *what){
    static unsigned reports;
    if(reports<8){++reports;unsigned cached=0,shown=0,building=0,free=0;
        for(const auto &e:textures.entries)if(e.valid){if(e.pins&4)cached+=e.pages;if(e.pins&(1u<<displayed))shown+=e.pages;if(e.pins&(1u<<textures.build_slot))building+=e.pages;}
        for(auto p:textures.pages)free+=!p;
        char message[160];std::snprintf(message,sizeof(message),"HMM2 RAMG %s need=%u cached=%u shown=%u build=%u free=%u parts=%u: layer skipped",what,bytes,cached,shown,building,free,unsigned(map_parts.size()));hmm_log(message);}
    texture_missed=true;Texture t{};t.missing=true;return t;
}
// Codex: подготовка звука продолжается и вне LOAD; прерываем только безопасную
// фазу текстур, пока RAM_G DL последнего показанного кадра ещё не изменяется.
// Клод: в ELF звук идёт своей задачей, а опрос на каждую текстуру читал часы около 850 раз за кадр (6,4 % команд трассы GateHell).
// Очередь звуков игры теперь обслуживает hmm_gpu_frame раз за кадр; опрос текстур остался только у стенда с опросным звуком.
#ifdef HMM2_HOST
void render_audio_tick(){hmm_audio_poll();}
void texture_tick(){hmm_audio_poll();hmm_loading_tick();}
#else
void render_audio_tick(){}
void texture_tick(){}
#endif
Texture texture(const Draw &d){
    texture_tick();
    Texture t{};int upload;
    if(d.bitmap){
        const auto &b=*d.bitmap;const unsigned size=(b.w*b.h+3)&~3u;
        if(!hft_cache_get(&textures,(b.frame?UINT64_C(0x8000000000000000)|(b.id&UINT64_C(0x0fffffffffffffff)):UINT64_C(0x3000000000000000)|b.id),size,&t.address,&upload))return texture_missing(size,"CPU");
        t.stride=b.w;t.height=b.h;
        if(upload){
            if(b.frame)++stat_cell_uploads;
            bitmap_materialize(*d.bitmap);
            static uint32_t buffers[2][2048];const unsigned area=b.w*b.h;
            PixelJob job{b.pixels.data(),b.single?nullptr:b.pixels.data()+area,hmm_resource_palette(),buffers[0],std::min(8192u,size),std::min(8192u,area)};
            if(size>=8192)render_submit(hmm_render_job_pixels,&job);else hmm_render_job_pixels(&job);RenderGuard guard;render_wait();
            for(unsigned at=0,slot=0;at<size;slot^=1){
                const unsigned n=std::min(8192u,size-at),next=at+n;
                if(next<size){job={b.pixels.data()+next,b.single?nullptr:b.pixels.data()+area+next,job.translate,buffers[slot^1],std::min(8192u,size-next),std::min(8192u,area-next)};render_submit(hmm_render_job_pixels,&job);}
                put(t.address+at,buffers[slot],n/4);render_audio_tick();render_wait();at=next;
            }
            bitmap_release_pixels(*d.bitmap);
        }
    }else{
        const auto &r=hmm_resource_ft(d.resource,d.raw);
        if(!hft_cache_get(&textures,UINT64_C(0x1000000000000000)|r.offset,r.bytes,&t.address,&upload))return texture_missing(r.bytes,"atlas");
        if(upload){hmm_remote_graphics(r.offset,t.address,r.bytes);gpu_upload_bytes+=r.bytes;}
        t.stride=r.stride;t.height=r.height;t.cell=r.cell;
    }
    // Codex: общий банк CELL для кадров одной геометрии, включая монстров
    // 64x64. Адрес каждого пикселя прежний; копий для экземпляров нет.
    const uint32_t cell_bytes=t.stride*t.height;
    if(cell_bytes){const uint32_t source=t.address+t.cell*cell_bytes;t.cell=(source/cell_bytes)&127u;t.address=source-t.cell*cell_bytes;}
    map_keep_texture();return t;
}
/* Клод: большой ресурс (от 64 КиБ: фоны 640×480 окон, боя, города) — полосами по strip_rows строк, каждая своей текстурой кэша.
 * Целиком он требовал сплошные 300 КиБ RAM_G: за окном настроек карта держит закреплёнными ~2 МиБ текстур, свободное раздроблено,
 * фон окна пропускался, а пропуск пересобирал интерфейс и части карты в следующем кадре (стенд: 8 пропусков на окно настроек;
 * пользователь, 03.11: «после системных настроек игра стала постоянно что-то подгружать»). Полоса — непрерывный кусок того же
 * исходного кадра в HMM2.FTG, кратный 512 байтам, и грузится тем же E6 прямо с SD; рисуется та же область того же кадра. */
constexpr uint32_t STRIP_MIN_BYTES=64*1024;
unsigned strip_rows(unsigned stride){unsigned g=512,a=stride;while(a){const unsigned t=g%a;g=a;a=t;}unsigned rows=512/g;while(rows<32)rows*=2;return rows;}
Texture texture_strip(const HmmFtImage &r,unsigned k,unsigned rows){
    texture_tick();Texture t{};int upload;
    const uint32_t at=k*rows*r.stride,bytes=std::min<uint32_t>(rows*r.stride,r.bytes-at);
    if(!hft_cache_get(&textures,UINT64_C(0x6000000000000000)|(r.offset+at),bytes,&t.address,&upload))return texture_missing(bytes,"strip");
    if(upload){hmm_remote_graphics(r.offset+at,t.address,bytes);gpu_upload_bytes+=bytes;}
    t.stride=r.stride;t.height=std::min<unsigned>(rows,r.height-k*rows);t.cell=0;
    map_keep_texture();return t;
}
int physical(int x){return x;}
// Клод: показанные кадры роликов (RGB565 из CMD_VIDEOFRAME); номер берёт Draw::color у слоя fill+raw.
struct ClipFrame {uint32_t address;unsigned w,h;};ClipFrame clip_frames[HMM_CLIP_SLOTS];
void emit_clip(const Draw &d){
    const auto &f=clip_frames[d.color];
    // A decoded movie is a real texture lease too, including after its
    // player releases CLIP_PIN but a closing window still displays the frame.
    for(unsigned i=0;i<HFT_ENTRIES;++i){auto &e=textures.entries[i];
        if(e.valid&&e.page*HFT_PAGE_BYTES==f.address){
            e.pins|=1u<<textures.build_slot;e.age=++textures.age;textures.last_entry=i;map_keep_texture();break;
        }
    }
    // Клод: ручка помечена чужой (address=~0u): обычные PALETTED8 не возьмут её вместе с форматом RGB565.
    const unsigned handle=handle_count<32?handle_count++:31;handles[handle]={};
    set(0x05000000|handle);word(0x01000000|f.address);
    word(0x07000000|(7u<<19)|(((f.w*2)&1023)<<9)|(f.h&511));word(0x28000000|(((f.w*2)>>10)<<2)|(f.h>>9));
    word(0x08000000|((d.r.w&511)<<9)|(d.r.h&511));word(0x29000000|((d.r.w>>9)<<2)|(d.r.h>>9));
    set(0x1f000001);set(0x04ffffff);set(0x15000100);
    set(0x17000000|(uint32_t(d.sx*256)&0xffffff));set(0x1a000000|(uint32_t(d.sy*256)&0xffffff));
    set(0x2b000000|(uint32_t((d.r.x-(d.r.x&511))*16)&0x1ffff));set(0x2c000000|(uint32_t((d.r.y-(d.r.y&511))*16)&0x1ffff));
    word(0x80000000|(uint32_t(d.r.x&511)<<21)|(uint32_t(d.r.y&511)<<12)|(handle<<7));
}
void emit_textured(const Draw &d,const Texture &t,bool whole_allowed){
    /* Клод: кусок спрайта (сдвиг sx/sy без отражения) — адрес текстуры сдвигается на его первую точку (BITMAP_SOURCE, для PALETTED8
     * без выравнивания; RTL Tang берёт все 22 бита), ячейка CELL уходит в тот же адрес, а TRANSFORM_C/F остаются нулевыми. Прежде
     * каждый кусок монстра или тени менял TRANSFORM_C и TRANSFORM_F (на Alteris 427 слов из 2672 в списке кадра). */
    const bool shifted=d.step==1&&!d.repeat&&(d.sx||d.sy);
    const uint32_t address=shifted?t.address+(uint32_t(t.cell)*t.height+uint32_t(d.sy))*t.stride+uint32_t(d.sx):t.address;
    const unsigned cell=shifted?0u:t.cell;const int sx=shifted?0:d.sx,sy=shifted?0:d.sy;
    const unsigned preferred=body_actor_handle>=0?unsigned(body_actor_handle):(((t.stride>>5)*5)^((t.height>>5)*13)^((t.stride&31)*3)^(t.height&31))&31;
    unsigned handle=0;
    if(cluster_compiling||body_actor_handle>=0){handle=preferred;const auto &h=handles[handle];if(h.address!=address||h.stride!=t.stride||h.height!=t.height)handle=handle_count;}
    else for(;handle<handle_count;++handle)if(handles[handle].address==address&&handles[handle].stride==t.stride&&handles[handle].height==t.height)break;
    if(handle==handle_count){
        // Codex: атласы одинаковой геометрии меняют только SOURCE, состояние ручки сохраняется.
        unsigned same=0;
        if(cluster_compiling||body_actor_handle>=0){same=preferred;const auto &h=handles[same];if(h.address==~0u||h.stride!=t.stride||h.height!=t.height)same=handle_count;}
        else for(;same<handle_count;++same)if(handles[same].address!=~0u&&handles[same].stride==t.stride&&handles[same].height==t.height)break;
        if(same<handle_count){handle=same;handles[handle].address=address;set(0x05000000|handle);word(0x01000000|address);}
        else {
            handle=cluster_compiling||body_actor_handle>=0?preferred:handle_count<32?handle_count++:unsigned(std::min_element(handles,handles+32,[](const Handle&a,const Handle&b){return a.used<b.used;})-handles);
            handles[handle]={address,t.stride,t.height,~0u,~0u};
            set(0x05000000|handle);word(0x01000000|address);
            word(0x07000000|(16u<<19)|((t.stride&1023)<<9)|(t.height&511));
            word(0x28000000|((t.stride>>10)<<2)|(t.height>>9));
        }
    }
    handles[handle].used=++next_handle;
    if(body_actor_handle>=0)body_actor_touched|=uint32_t(1)<<handle;
    const int x=physical(d.r.x),y=physical(d.r.y);
    int w=physical(d.r.x+d.r.w)-x,h=physical(d.r.y+d.r.h)-y;
    if(whole_allowed&&!d.bitmap&&!d.repeat){
        const auto &im=hmm_resource_get(d.resource);
        // Codex: полный кадр CELL использует один размер ручки. Прозрачное дополнение
        // и BORDER закрывают остаток 5/8 без шва; обрезанные прямоугольники не расширяем.
        if(d.step==1&&d.sx==0&&d.sy==0&&d.r.w==im.width()&&d.r.h==im.height()){
            w=t.stride;h=t.height;
        }
    }
    auto &entry=handles[handle];uint32_t size=0x08000000|(d.repeat?0xc0000:0)|((w&511)<<9)|(h&511),high=0x29000000|((w>>9)<<2)|(h>>9);
    if(entry.size!=size||entry.high!=high)set(0x05000000|handle);
    if(entry.size!=size){word(size);entry.size=size;}if(entry.high!=high){word(high);entry.high=high;}
    set(0x1f000001);set(0x04ffffff);palette(d.mapping);
    set(0x15000000|(uint32_t(d.step*256)&0x1ffff));
    // Codex: растеризация 1:1. Штатный scanout Tang масштабирует готовую сцену с курсором.
    set(0x17000000|(uint32_t(sx*256+(d.step<0?255:0))&0xffffff));
    set(0x1a000000|(uint32_t(sy*256)&0xffffff));
    // Codex: VERTEX2II без знака; целые полосы по 512 px идут в знаковый VERTEX_TRANSLATE.
    const uint32_t vx=0x2b000000|(uint32_t((d.r.x-(d.r.x&511))*16)&0x1ffff);
    const uint32_t vy=0x2c000000|(uint32_t((d.r.y-(d.r.y&511))*16)&0x1ffff);
    if(hero_slot_emitting){camera_word(vx);camera_word(vy);}else{set(vx);set(vy);}
    // Клод: слой AlphaBlit (color у слоя без fill — альфа): COLOR_A только на эту вершину, затем снова 255.
    // Отслеживаемое set() состояние COLOR_A остаётся 255, поэтому кешированные части списка его не наследуют.
    if(d.color)word(0x10000000|d.color);
    word(0x80000000|(uint32_t(d.r.x&511)<<21)|(uint32_t(d.r.y&511)<<12)|(handle<<7)|cell);
    if(d.color)word(0x100000ff);
}
HMM_SIZE_RENDER void emit_strips(const Draw &d,const HmmFtImage &r){
    const int rows=int(strip_rows(r.stride));
    for(int k=d.sy/rows;k<=(d.sy+d.r.h-1)/rows;++k){
        const int top=std::max(d.sy,k*rows),bottom=std::min(d.sy+d.r.h,(k+1)*rows);if(bottom<=top)continue;
        Draw s=d;s.sy=top-k*rows;s.r.y=d.r.y+(top-d.sy);s.r.h=bottom-top;
        const auto t=texture_strip(r,unsigned(k),unsigned(rows));if(t.missing)continue;
        emit_textured(s,t,false);
    }
}
void emit(const Draw &d){
    // Клод: заместитель окна карты — слои режима карты по порядку, обрезанные по заместителю.
    if(d.layer){emit_map(d);return;}
    if(d.fill){
        body_palette_fill=true;
        if(d.raw){emit_clip(d);return;}
        if(d.mask==1)return;
        /* Клод: заливка-преобразование (hmm_gpu_fill с маской 2–9): таблицы 2–5 оригинала затемняют, 6–9 осветляют. FT812 — чёрный или белый
         * с альфой; альфа подобрана по самой таблице transformTable и палитре KB.PAL (регрессия яркости по 204 цветам). */
        static const uint8_t shade_alpha[6]={0,0,88,60,34,17};
        const unsigned shade=d.mask>=2&&d.mask<=9?d.mask<=5?shade_alpha[d.mask]:lighten_argb[d.mask-6]>>24:0;
        unsigned color=d.mapping?mappings[d.mapping-1][d.color]:d.color;
        set(0x2b000000);set(0x2c000000);
        // Codex: RECTS расширяет вершины на LINE_WIDTH; ножницы дают точные границы Fill.
        const int x=physical(d.r.x),y=physical(d.r.y),w=physical(d.r.x+d.r.w)-x,h=physical(d.r.y+d.r.h)-y;
        set(0x1b000000|(uint32_t(x)<<11)|uint32_t(y));set(0x1c000000|(uint32_t(w)<<12)|uint32_t(h));
        set(0x1f000009);set(0x04000000|(shade?(d.mask<=5?0u:0xffffffu):palette_rgb[color]&0xffffff));set(0x0e000010);
        // Клод: COLOR_A только на этот прямоугольник, затем снова 255 (как у слоя AlphaBlit): отслеживаемое состояние не меняется.
        if(shade)word(0x10000000|shade);
        word(0x40000000|(uint32_t(physical(d.r.x)*16)<<15)|uint32_t(physical(d.r.y)*16));
        word(0x40000000|(uint32_t(physical(d.r.x+d.r.w)*16)<<15)|uint32_t(physical(d.r.y+d.r.h)*16));
        if(shade)word(0x100000ff);
        const Rect scissor=map_compiling&&map_compiling!=&interface_dl&&map_compiling!=message_compiling?map_window:Rect{0,0,640,480};
        set(0x1b000000|(uint32_t(scissor.x)<<11)|uint32_t(scissor.y));
        set(0x1c000000|(uint32_t(scissor.w)<<12)|uint32_t(scissor.h));return;
    }
    // Клод: большой ресурс — полосами (texture_strip): сплошной кусок RAM_G под него не нужен.
    // Целиком, если он уже в RAM_G или есть сплошное место без вытеснения: полосы удлиняют DL (рамка карты 640×480 — +360 слов).
    if(!d.bitmap&&!d.repeat){const auto &r=hmm_resource_ft(d.resource,d.raw);
        if(r.bytes>=STRIP_MIN_BYTES&&r.stride!=32&&!hft_cache_ready(&textures,UINT64_C(0x1000000000000000)|r.offset,r.bytes)){emit_strips(d,r);return;}}
    const auto t=texture(d);
    if(t.missing)return;
    emit_textured(d,t,true);
}
template<class Iterator> void emit_sequence(Iterator source,size_t count){for(size_t i=0;i<count;++i)emit(source[i]);}
#ifndef HMM2_HOST
// Codex: старые части обслуживают редкие эффекты вне кольца; здесь важен размер ELF.
__attribute__((optimize("Os")))
#endif
void prepare_map(){
    Prof prof(P_PARTS);
    for(auto &part:map_parts){
        if(part.fill&&part.palette!=palette_epoch)part.dirty=true;
        // Клод: сдвинутая часть (прокрутка без пересборки) при окнах поверх карты собирается заново: под окно попали другие куски.
        if(!map_covers.empty()&&(part.tx||part.ty))part.dirty=true;
        if(part.dirty){
            map_cache_release(part);reset_emitter();map_compiling=&part;texture_missed=false;
            set(0x1b000000|(uint32_t(map_window.x)<<11)|uint32_t(map_window.y));
            set(0x1c000000|(uint32_t(map_window.w)<<12)|uint32_t(map_window.h));
            if(part.kind==2){
                Sequence<Draw> fog(map_draws.begin()+part.begin,map_draws.begin()+part.begin+part.count);
                // Клод: куски тумана под непрозрачным окном экрана (map_covers_refresh) не выводятся.
                if(!map_covers.empty()){size_t k=0;for(size_t j=0;j<fog.size();++j)if(!map_hidden(fog[j])){if(k!=j)fog[k]=std::move(fog[j]);++k;}fog.resize(k);}
                repeat_fog(fog);emit_sequence(fog.begin(),fog.size());
            }else{
                // Клод: склеенные куски спрайтов (map_merge_plan): якорный кусок выводится целым прямоугольником, остальные пропускаются;
                // кусок под непрозрачным окном экрана (map_hidden) не выводится.
                for(size_t i=part.begin;i<part.begin+part.count;++i){const int v=i<map_merge_at.size()?map_merge_at[i]:-1;
                    if(v==-2)continue;const Draw &d=v>=0?map_merged[size_t(v)]:map_draws[i];if(!map_covers.empty()&&map_hidden(d))continue;emit(d);}
            }
            map_compiling=nullptr;part.dirty=texture_missed;part.palette=palette_epoch;part.tx=part.ty=0;
        }
    }
}
#include "hmm_ring_layers.inc"
void emit_map(const Draw &layer){
    if(emit_ring(layer))return;
    if(tile_map_active){
        const auto r=intersect(map_window,layer.r);
        reset_emitter();set(0x1b000000|(uint32_t(r.x)<<11)|uint32_t(r.y));
        set(0x1c000000|(uint32_t(r.w)<<12)|uint32_t(r.h));
        emit_sequence(tile_map_draws.begin(),tile_map_draws.size());
        reset_emitter();set(0x1b000000);set(0x1c2801e0);return;
    }
    // Codex: обрезанный заместитель (например, окно поверх карты) идёт точным общим путём.
    const auto visible_bounds=intersect(map_bounds,layer.r);
    const bool clipped=visible_bounds.w!=map_bounds.w||visible_bounds.h!=map_bounds.h;
    if(clipped){
        Sequence<Draw> visible;visible.reserve(map_draws.size());
        for(const auto &d:map_draws){auto c=clip(d,layer.r);if(c.r.w&&c.r.h)visible.push_back(std::move(c));}
        occlude(visible);repeat_fog(visible);join_layers(visible);
        reset_emitter();set(0x1b000000);set(0x1c2801e0);
        emit_sequence(visible.begin(),visible.size());
        reset_emitter();set(0x1b000000);set(0x1c2801e0);return;
    }

    if(ring_enabled()){
        // Codex: редкий выход из кольца сохраняет обычные FT-примитивы;
        // переполнение по-прежнему обрабатывает прежний cache_map_tiles.
        reset_emitter();set(0x1b000000|(uint32_t(visible_bounds.x)<<11)|uint32_t(visible_bounds.y));
        set(0x1c000000|(uint32_t(visible_bounds.w)<<12)|uint32_t(visible_bounds.h));
        emit_sequence(map_draws.begin(),map_draws.size());
        reset_emitter();set(0x1b000000);set(0x1c2801e0);return;
    }
    prepare_map();
    // Codex: если готовые самодостаточные блоки целиком помещаются в RAM_DL,
    // повторное удаление одинаковых состояний не требуется. Запас курсора рассчитан
    // по числу его слоёв: bitmap требует до15слов, запас20; ещё2SCISSOR на окно и16служебных.
    size_t raw_words=0,holes=0;for(const auto &p:map_parts)raw_words+=p.commands.size();
    for(auto command:interface_dl.commands)if((command>>24)==0xff)++holes;
    const size_t cursor_layers=cursor_image&&!cursor_image->empty()?canvas(*cursor_image).draws.size()+(cursor_clock_image?canvas(*cursor_clock_image).draws.size():0):0;
    const bool direct=raw_words<=2048&&holes<=2048&&raw_words*holes+interface_dl.commands.size()+20*cursor_layers+2*holes+16<=2048;
    for(auto &part:map_parts){

        cached_textures_use(part);
        for(auto command:part.commands){
            // Codex: палитра меняется независимо от геометрии. В кэше хранится номер отображения,
            // а актуальный RAM_G-адрес подставляется при публикации, без пересборки статических слоёв.
            if((command>>24)==0x2a)command=0x2a000000|palette_address(command&0xffffff);
            else if((command>>24)==0x2b)command=0x2b000000|((command+part.tx*16)&0x1ffff);
            else if((command>>24)==0x2c)command=0x2c000000|((command+part.ty*16)&0x1ffff);
            if(direct)dl[words++]=command;else word(command);
        }
    }
    if(direct){std::memset(compact_valid,0,sizeof(compact_valid));std::memset(compact_mask,0,sizeof(compact_mask));compact_handle=0;}
    reset_emitter();set(0x1b000000);set(0x1c2801e0);
}
}
namespace {
#ifndef HMM2_HOST
// Codex: запасной путь оптимизируем по размеру, не меняя его результат.
__attribute__((optimize("Os")))
#endif
bool cache_map_tiles(bool whole=false,bool bake=false,bool full=false){
    Prof prof(P_CELLS);
    // Codex: запасной путь должен видеть исходные слои кольца до выбора map_only.
    ring_expand();
#ifdef HMM2_HOST
    // Codex: пиксельный тест принудительно проверяет редкий полный запасной путь; в ELF ветки нет.
    if(std::getenv("HMM2_TEST_WHOLE_TILES"))whole=true;
#endif
    /* Клод: целые клетки (whole) — сначала только в окне карты: по всему экрану они запекали и рамку карты, которую игра потом
     * не перерисовывает, и 68 её клеток оставались в списке экрана навсегда (~200 слов в каждом кадре: после окна героя на
     * Alteris кадры снова переполнялись, стенд, сборка 95). full — весь экран, если и так не влезло. */
    const bool screen_wide=whole&&(full||!map_window.w||!map_window.h);
    const auto roi=screen_wide?Rect{0,0,640,480}:intersect(map_window,{0,0,640,480});if(!roi.w||!roi.h)return false;
    const int gx=whole?0:roi.x-((map_origin_x%32+32)%32),gy=whole?0:roi.y-((map_origin_y%32+32)%32);
    const unsigned cols=(roi.x+roi.w-gx+31)/32,rows=(roi.y+roi.h-gy+31)/32,cells=cols*rows;
    /* Клод: клетки идут по сетке тайлов карты, и крайняя клетка окна растеризуется целиком: слои карты обрезаются по сетке клеток,
     * а не по окну. Прежде крайние клетки (около 30) обрезались окном, при ходе героя карта сдвигалась на 4 точки за кадр, и их
     * видимая часть, а с ней и ключ, менялись каждый кадр: на плате (сборка 77, Alteris) 40 клеток за кадр растеризовались заново
     * со спрайтами с SD, 180–235 мс на кадр, музыка прерывалась. Теперь крайняя клетка меняется при переходе на соседний тайл. */
    const Rect grid_rect{gx,gy,int(cols)*32,int(rows)*32};size_t map_first,map_count;
    auto &screen=canvas(fheroes2::Display::instance());
    const bool map_only=!whole&&!map_draws.empty()&&std::count_if(screen.draws.begin(),screen.draws.end(),[](const Draw &d){return d.layer;})==1;
    if(map_only){
        if(map_active)map_record_end();map_active=false;map_first=0;map_count=map_draws.size();
        for(auto &part:map_parts)if(!part.commands.empty()||!part.textures.empty())map_cache_release(part);
        tiles_forget();
    }else {
        if(whole){map_why="whole tiles";map_materialize();map_first=map_count=0;}
        else map_materialize(&grid_rect,&map_first,&map_count);
        Sequence<Draw>().swap(tile_map_draws);tile_map_signature=0;
    }
    const auto &source=map_only?map_draws:screen.draws;if(source.size()>=65536)return false;
    /* Клод: кадр простоя карты повторяет прошлый слой в слой (игра перерисовывает карту на каждый тик, а меняется не всё). Тогда клетки,
     * их ключи и список интерфейса берутся прошлые: на плате (сборка 79, Alteris) пересборка ~225 клеток и всего списка экрана
     * стоила около 25 мс на каждый кадр простоя. Подпись — все поля слоёв и сетка. */
    TileHash signature_hash;
    for(auto value:{uint32_t(whole)|(uint32_t(bake)<<1)|(uint32_t(screen_wide)<<2),uint32_t(gx),uint32_t(gy),uint32_t(cols),uint32_t(rows),uint32_t(map_first),uint32_t(map_count)})signature_hash.add(value);
    for(const auto &d:source){
        const uint32_t values[]={d.resource,uint32_t(d.r.x),uint32_t(d.r.y),uint32_t(d.r.w),uint32_t(d.r.h),uint32_t(d.sx),uint32_t(d.sy),uint32_t(d.step),d.mapping,uint32_t(d.raw)|(uint32_t(d.fill)<<1)|(uint32_t(d.repeat)<<2)|(uint32_t(d.opaque)<<3)|(uint32_t(d.color)<<8)|(uint32_t(d.mask)<<16),d.bitmap?uint32_t(d.bitmap->id):0u,d.bitmap?uint32_t(d.bitmap->id>>32):0u};
        for(auto value:values)signature_hash.add(value);
    }
    const uint64_t signature=signature_hash.key();
    if(map_only&&signature==tile_map_signature&&!tile_map_draws.empty()){tile_map_active=true;stat_cells+=cells;return true;}
    if(!map_only&&signature==tiles_signature&&!tiles_out.empty()){screen.draws=tiles_out;screen.revision=tiles_revision;screen.prepared=true;stat_cells+=cells;return true;}
    auto frame=take_frame();frame->roi=roi;
    if(map_only)frame->map_source=&map_draws;else frame->draws.swap(screen.draws);
    const auto &frame_draws=map_only?map_draws:frame->draws;
    // Клод: слой карты (из map_materialize) — его части вне окна не выводятся, а в клетку он входит по всей сетке.
    auto is_map=[&](size_t i){return i-map_first<map_count;};
    try {
        // Codex: рабочие счётчики переиспользуются при сохранённом алгоритме клеток 91.
        if(!tile_workspace)tile_workspace.reset(new TileWorkspace);
        auto &counts=tile_workspace->counts;auto &offsets=tile_workspace->offsets;
        std::fill_n(counts.begin(),cells,0);offsets[0]=0;unsigned outside=0;
        auto visit=[&](size_t index,auto fn){const auto &d=frame_draws[index];const auto v=intersect(d.r,is_map(index)?grid_rect:roi);if(!v.w||!v.h)return;
            for(int y=(v.y-gy)/32;y<=(v.y+v.h-1-gy)/32;++y)for(int x=(v.x-gx)/32;x<=(v.x+v.w-1-gx)/32;++x)fn(unsigned(y)*cols+unsigned(x));};
        auto outside_parts=[&](size_t index,auto fn){if(is_map(index))return;const auto &d=frame_draws[index];const auto v=intersect(d.r,roi);if(!v.w||!v.h){fn(d.r);return;}
            const Rect parts[]={{d.r.x,d.r.y,d.r.w,v.y-d.r.y},{d.r.x,v.y+v.h,d.r.w,d.r.y+d.r.h-v.y-v.h},{d.r.x,v.y,v.x-d.r.x,v.h},{v.x+v.w,v.y,d.r.x+d.r.w-v.x-v.w,v.h}};
            for(auto q:parts)if(q.w&&q.h)fn(q);};
        for(size_t i=0;i<frame_draws.size();++i){visit(i,[&](unsigned cell){++counts[cell];});outside_parts(i,[&](Rect){++outside;});}
        for(unsigned i=0;i<cells;++i){offsets[i+1]=offsets[i]+counts[i];counts[i]=offsets[i];}
        frame->layers.resize(offsets[cells]);
        for(size_t i=0;i<frame_draws.size();++i)visit(i,[&](unsigned cell){frame->layers[counts[cell]++]=uint16_t(i);});
        Sequence<Draw> out;out.reserve(outside+cells);
        stat_cells+=cells;
        for(size_t i=0;i<frame_draws.size();++i)outside_parts(i,[&](Rect r){out.push_back(clip(frame_draws[i],r));});
        tiles_baked.clear();if(!whole)tiles_baked.reserve(outside+cells);
        for(const auto &d:out)tiles_baked.push_back(d);
        unsigned live_layers=0;
        for(unsigned cell=0;cell<cells;++cell){const Rect full{gx+int(cell%cols)*32,gy+int(cell/cols)*32,32,32};const auto visible=intersect(full,roi);
            unsigned begin=offsets[cell],end=offsets[cell+1];
            // Codex: последний полный непрозрачный слой исключает невидимый префикс, как прежде.
            for(unsigned i=begin;i<end;++i){const auto &d=frame_draws[frame->layers[i]];const auto &need=is_map(frame->layers[i])?full:visible;const auto v=intersect(d.r,need);if(d.opaque&&v.w==need.w&&v.h==need.h)begin=i;}
            // Codex: запекаем только фон до первого динамического слоя. Весь хвост, включая
            // неподвижные деревья перед героем, остаётся в исходном порядке поверх этого фона.
            unsigned split=end;
            for(unsigned i=begin;i<end;++i){const auto &d=frame_draws[frame->layers[i]];if(!d.fill&&d.mask){split=i;break;}}
            const bool live=!whole&&!bake&&split<end&&live_layers+end-split<=live_budget;
            TileHash hash;uint64_t prefix=0;
            for(unsigned i=begin;i<end;++i){
                if(i==split)prefix=hash.key();
                const auto &d=frame_draws[frame->layers[i]];auto r=intersect(d.r,full);
                int sx=d.sx+(r.x-d.r.x)*d.step,sy=d.sy+r.y-d.r.y;r.x-=full.x;r.y-=full.y;
                if(d.repeat){sx&=127;sy&=127;}
                const uint32_t values[]={d.resource,uint32_t(r.x),uint32_t(r.y),uint32_t(r.w),uint32_t(r.h),uint32_t(sx),uint32_t(sy),uint32_t(d.step),d.mapping,uint32_t(d.raw)|(uint32_t(d.fill)<<1)|(uint32_t(d.repeat)<<2)|(uint32_t(d.color)<<8)|(uint32_t(d.fill?d.mask:0)<<16)};
                for(auto value:values)hash.add(value);
                if(d.bitmap){hash.add(uint32_t(d.bitmap->id));hash.add(uint32_t(d.bitmap->id>>32));}
            }
            auto tile=[&](unsigned stop,uint64_t key){
                auto bitmap=take_bitmap();bitmap->id=UINT64_C(0xc000000000000000)|key;bitmap->w=bitmap->h=32;bitmap->single=true;
                bitmap->frame=frame;bitmap->tile_rect=full;bitmap->paint_begin=begin;bitmap->paint_count=stop-begin;bitmaps.push_back(bitmap);
                Draw d;d.r=visible;d.sx=visible.x-full.x;d.sy=visible.y-full.y;d.bitmap=std::move(bitmap);d.opaque=d.raw=true;return d;
            };
            Draw baked=tile(end,hash.key());
            if(live){
                if(split!=begin)out.push_back(tile(split,prefix));
                for(unsigned i=split;i<end;++i){Draw d=clip(frame_draws[frame->layers[i]],visible);if(d.r.w&&d.r.h)out.push_back(std::move(d));}
                live_layers+=end-split;
            }else out.push_back(baked);
            if(!whole)tiles_baked.push_back(std::move(baked));
        }
        if(!live_layers)tiles_baked.clear();
        stat_live_layers+=live_layers;
        if(map_only){join_layers(out);tile_map_draws.swap(out);tile_map_signature=signature;tile_map_active=true;}
        else {screen.draws.swap(out);changed(screen);tiles_signature=signature;tiles_out=screen.draws;tiles_revision=screen.revision;}
        bitmaps.erase(std::remove_if(bitmaps.begin(),bitmaps.end(),[](const auto &v){return v.expired();}),bitmaps.end());
        if(!dense_mode)hmm_log("HMM2: dense map uses cached tiles");return true;
    } catch(...){if(!map_only)screen.draws.swap(frame->draws);throw;}
}
}
namespace {
// Клод: CMD_APPEND из задачи курсора: как append_dl, но без hmm_fail: исключение вне потока игры завершило бы всё приложение.
bool task_append(unsigned count){
    const uint32_t commands[]={0xffffff00u,0xffffff1eu,dl_address,count*4};
    if(!copro_try_send(commands,4))return false;
    for(const uint32_t start=hmm_core->millis();;){
        uint32_t read;if(hmm_core->ft_read(0x3020f8,&read)!=ZM_OK||read==4095){copro_unsettled=true;return false;}
        if(read==command_position)return true;
        if(hmm_core->millis()-start>2000){copro_unsettled=true;return false;}
        hmm_core->delay(1);
    }
}
// Codex: экран SPG продолжается до первого кадра игры; логотип, рамка и заполнение уже внутри JPEG.
// Codex: метка «VDLP» и её единицы прежние; координаты затемняющей маски общие с SPG.
constexpr uint32_t LOADER_MARK=0x07fff0,LOADER_CHIPID=0x0c0000,LOADER_PICTURE=0x080000;
#include "hmm_splash.inc"
bool loader_active;unsigned loader_shown;uint32_t loader_ticked;
constexpr uint32_t loader_vertex(int x,int y){return 0x40000000u|(uint32_t(x*16)<<15)|uint32_t(y*16);}
bool loader_begin(){
    if(!cursor_live())return false;
    uint32_t mark[4];for(unsigned i=0;i<4;++i)if(hmm_core->ft_read(LOADER_MARK+i*4,&mark[i])!=ZM_OK)return false;
    if(mark[0]!=0x504c4456u)return false;
    const uint32_t zero[4]={};if(hmm_core->ft_write(LOADER_CHIPID,&mark[2],1)!=ZM_OK||hmm_core->ft_write(LOADER_MARK,zero,4)!=ZM_OK)return false;
    loader_shown=std::min<unsigned>(mark[1]&0xffff,LOADER_FILL);loader_ticked=hmm_core->millis();loader_active=true;return true;
}
// Codex: одна вершина открывает по точке готовую полосу; SCISSOR защищает рамку и торцы.
// Клод: список экрана загрузки при заполнении shown точек — общий для задачи курсора и loader_finish.
constexpr unsigned LOADER_WORDS=26;
void loader_list(unsigned shown,uint32_t (&list)[LOADER_WORDS]){
    const uint32_t words[LOADER_WORDS]={0x02000000,0x26000007,0x27000004,0x04ffffff,0x100000ff,0x05000000,0x01000000|LOADER_PICTURE,0x07380100,0x28000009,0x08000100,0x29000009,0x1f000001,loader_vertex(0,0),
        0x21000000,0x22000000,0x1b000000|(LOADER_X<<11)|LOADER_TOP,0x1c000000|(LOADER_FILL<<12)|(LOADER_BOTTOM-LOADER_TOP),
        0x1f000009,0x0e000010,0x04000000,0x10000000|LOADER_MASK_ALPHA,loader_vertex(LOADER_X+shown+1,LOADER_TOP-2),
        loader_vertex(LOADER_X+LOADER_FILL+2,LOADER_BOTTOM+2),0x21000000,0x23000000,0};
    std::memcpy(list,words,sizeof(words));
}
bool loader_tick(){
    if(hmm_core->millis()-loader_ticked<100)return true;
    uint32_t swap;if(hmm_core->ft_read(0x302054,&swap)!=ZM_OK)return false;
    if(swap)return true;
    loader_ticked=hmm_core->millis();loader_shown+=(LOADER_FILL-loader_shown+63)/64;
    uint32_t list[LOADER_WORDS];loader_list(loader_shown,list);
    if(hmm_core->ft_write(dl_address,list,LOADER_WORDS)!=ZM_OK||!task_append(LOADER_WORDS))return false;
    swap=2;return hmm_core->ft_write(0x302054,&swap,1)==ZM_OK;
}
void video_mode_game(){uint32_t v=0;put(0x302070,&v,1);v=640;put(0x302034,&v,1);v=480;put(0x302048,&v,1);v=1;put(0x302070,&v,1);}
// Codex: до первого кадра RAM_G занят только блоком DL 0..8191, меткой и восстановлением CHIPID.
// Codex: после чёрного списка и смены режима текстуры могут занять место заставки; новых буферов у неё нет.
void loader_finish(){
    GpuBusy busy;loader_active=false;
    uint32_t v;do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Loader DLSWAP read");}while(v);
    // Клод: полоса шла к концу всё медленнее (шаг — остаток/64) и обрывалась первым кадром игры, не дойдя до конца
    // (пользователь, 02.11). Теперь она за несколько кадров дотягивается до конца и держится полной 150 мс.
    for(bool last=false;!last;){
        loader_shown=std::min(LOADER_FILL,loader_shown+96);last=loader_shown==LOADER_FILL;
        uint32_t bar[LOADER_WORDS];loader_list(loader_shown,bar);put(dl_address,bar,LOADER_WORDS);append_dl(LOADER_WORDS);v=2;put(0x302054,&v,1);
        do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Loader DLSWAP read");}while(v);
    }
    hmm_core->delay(150);
    const uint32_t list[]={0x26000007,0};put(dl_address,list,2);append_dl(2);v=2;put(0x302054,&v,1);
    do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Loader DLSWAP read");}while(v);
    lastwords=0;video_mode_game();
}
// Клод: один проход задачи курсора по последнему опубликованному списку; false — FT не отвечает, задача выходит.
// Здесь нельзя hmm_fail: исключение вне потока игры завершило бы всё приложение, об ошибке скажет сама игра.
bool cursor_apply(int x,int y,bool clock){
    if(!lastwords||!cursor_count)return true;
    bool differs=false;
    const auto patched=[&](const CursorPatch &p){
        const bool vertical=(p.base>>24)==0x2c;
        if(p.clock==2){const uint32_t at=status_reveal_at.load();const bool hidden=!at||int32_t(hmm_core->millis()-at)<0;
            const int shift=vertical?((hidden?CURSOR_HIDE:0)-(status_emitted_hidden.load()?CURSOR_HIDE:0))*16:0;
            return (p.base&0xff000000)|((p.base+shift)&0x1ffff);}
        int delta=vertical?y-cursor_anchor_y:x-cursor_anchor_x;
        if(vertical&&clock)delta+=p.clock?-CURSOR_HIDE:CURSOR_HIDE;
        return (p.base&0xff000000)|((p.base+delta*16)&0x1ffff);
    };
    for(unsigned i=0;i<cursor_count&&!differs;++i)differs=lastdl[cursor_committed[i].index]!=patched(cursor_committed[i]);
    if(!differs)return true;
    uint32_t swap;if(hmm_core->ft_read(0x302054,&swap)!=ZM_OK)return false;
    if(swap)return true;
    for(unsigned i=0;i<cursor_count;++i){const auto &p=cursor_committed[i];const uint32_t value=patched(p);
        if(lastdl[p.index]!=value){lastdl[p.index]=value;if(hmm_core->ft_write(dl_address+p.index*4,&value,1)!=ZM_OK)return false;}
    }
    if(!task_append(lastwords))return false;
    swap=2;if(hmm_core->ft_write(0x302054,&swap,1)!=ZM_OK)return false;
    ++cursor_swaps;return true;
}
void cursor_task(void*){
    while(cursor_run&&hmm_running){
        hmm_core->delay(8);
        int x,y;hmm_loading_mouse(&x,&y);
        const bool clock=hmm_core->millis()-hmm_input_polled()>CURSOR_CLOCK_MS;
        // Клод: «Map loading...» задача курсора больше не снимает (только hmm_status_hold_arm при проявлении карты).
        // Клод: обе стороны сначала ставят свой признак, потом читают чужой (seq_cst): вдвоём в FT не войдут.
        cursor_active.store(1);
        const bool alive=gpu_busy.load()||copro_unsettled||(loader_active?loader_tick():cursor_apply(x,y,clock));
        // Keep the cursor task alive while the game repairs a torn packet.
        // Snapshot under the writer guard; the game may clear the flag once
        // cursor_active is released below.
        const bool recover=copro_unsettled;
        cursor_active.store(0);
        if(!alive&&!recover)break;
    }
    cursor_finished=true;
}
void cursor_task_stop(){
    cursor_run=false;const uint32_t start=hmm_core->millis();
    while(!cursor_finished&&hmm_core->millis()-start<3000)hmm_core->delay(1);
}
}
// Клод: пишет только поток игры, задача курсора читает. fetch_add на PSRAM — тот же неатомарный S32C1I, что сломал канал чтения (hmm_io.c).
extern "C" void hmm_gpu_busy_enter(){
    gpu_busy.store(gpu_busy.load()+1);
    while(cursor_active.load())hmm_core->delay(1);
}
extern "C" void hmm_gpu_busy_leave(){gpu_busy.store(gpu_busy.load()-1);}
extern "C" HMM_SIZE_RENDER void hmm_gpu_init(){
    message_fade_reset();
    
    region_owner=nullptr;region_target=nullptr;region_bump();
    body_plan_clear();ring_clusters_clear(true);hft_cache_init(&textures);std::memset(palette_entry_by_page,0,sizeof(palette_entry_by_page));canvases.clear();tiles_forget();ready=true;bitmap_prior_valid=false;dense_mode=map_tiles_shown=false;std::memset(bitmap_known,0,sizeof(bitmap_known));
    render_worker_start();
#ifdef HMM2_HOST
#endif
    int upload;
    if(!hft_cache_get(&textures,UINT64_C(0x4000000000000000),DL_LIMIT*4,&dl_address,&upload))hmm_fail("RAM_G DL capacity");
    textures.entries[textures.last_entry].pins|=16;
    if(hmm_core->ft_read(0x3020fc,&command_position)!=ZM_OK||(command_position&3)||command_position>4092)hmm_fail("FT CMD position");
    copro_unsettled=false;
    // Codex: ядро 1.34 поддерживает 640×480 и 1024×768 в одном окне HDMI.
    // Меняем только стандартные регистры FT, сохраняя частоту кадров развёртки.
    // Клод: при экране загрузки — в первом кадре игры: картинка 1024×768.
    if(!loader_begin())video_mode_game();
    // Клод: курсор — отдельной задачей на ядре 0, выше рабочей задачи кадра и ниже PCM.
    hmm_input_init();gpu_busy=0;cursor_active=0;
    if(cursor_live()){
        cursor_run=true;cursor_finished=false;
        if(hmm_core->task_start(cursor_task,nullptr,3072,4,0)!=ZM_OK){cursor_run=false;cursor_finished=true;hmm_log("HMM2: cursor task unavailable");}
    }
}
#ifdef HMM2_HOST
// Клод: сцены стенда сверяют кадр курсора за известной точкой, без задачи и без реального времени.
extern "C" int hmm_gpu_move_cursor(int x,int y){return cursor_apply(x,y,false);}
#endif
// Клод: ролики. Первый кадр освобождает RAM_G: показывается пустой список, снимаются закрепления всех
// текстур, кроме блока DL, поэтому буферы получают непрерывное место; нужное движок загрузит заново.
// Кольцо FIFO не оборачивается: кадр меньше него (проверяет build_stage1_clips.py и код ниже).
namespace {
constexpr uint32_t CLIP_FIFO=256*1024;constexpr uint8_t CLIP_PIN=8;
uint32_t clip_fifo;
uint32_t clip_get(uint64_t key,uint32_t bytes){
    uint32_t address;int upload;
    if(!hft_cache_get(&textures,UINT64_C(0x5000000000000000)|key,bytes,&address,&upload))hmm_fail("RAM_G clip capacity");
    textures.entries[textures.last_entry].pins|=CLIP_PIN;return address;
}
HMM_SIZE_RENDER void clip_begin(){
    if(loader_active)loader_finish();
    
    GpuBusy busy;
    uint32_t v;do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Clip DLSWAP read");}while(v);
    dl[0]=0x26000007;dl[1]=0;words=2;publish_dl();v=2;put(0x302054,&v,1);displayed^=1;
    lastdl[0]=dl[0];lastdl[1]=dl[1];lastwords=2;bitmap_prior_valid=false;cursor_count=0;
    do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Clip DLSWAP read");}while(v);
    body_plan_clear();ring_clusters_clear(true);ring_sprite_plan_clear();for(auto &part:map_parts)map_cache_release(part);
    map_cache_release(interface_dl);interface_revision=0;
    for(auto &e:textures.entries)e.pins&=16;
    hft_cache_begin(&textures,displayed);clip_fifo=clip_get(0,CLIP_FIFO+4);
}
}
extern "C" uint32_t hmm_gpu_clip_buffer(unsigned slot,unsigned buffer,unsigned w,unsigned h){
    if(!clip_fifo)clip_begin();
    return clip_get(1+slot*2+buffer,w*h*2);
}
extern "C" void hmm_gpu_clip_decode(uint32_t offset,uint32_t size,uint32_t target){
    if(size>=CLIP_FIFO)hmm_fail("Clip frame exceeds FIFO");
    // Клод: весь кадр без задачи курсора: её CMD_APPEND и DLSWAP не должны вклиниться в декодирование.
    GpuBusy busy;
    uint32_t v;do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("Clip DLSWAP read");}while(v);
    const uint32_t fifo[]={0xffffff39u,clip_fifo,CLIP_FIFO};copro_send(fifo,3);copro_wait();
    // Клод: кадр загружается целиком, пока сопроцессор простаивает. Очередь записи Evo в RAM_G держит 8 байт
    // (~2,3 мкс при SPI 28 МГц): под чтением FIFO и записью кадра BL616 она теряла байт (FT_STAT 25, плата 01.11).
    for(uint32_t at=0;at<size;){
        const uint32_t n=size-at>8192?8192:size-at;hmm_remote_graphics(offset+at,clip_fifo+at,n);at+=n;hmm_audio_poll();
    }
    put(0x309018,&size,1);
    // Клод: REG_CMD_READ уходит за параметры VIDEOFRAME раньше конца декодирования (и у FT812, и у BL616);
    // DLSTART за кадром — барьер, поэтому ожидание кончается на готовом кадре. DL на этой команде не показывается.
    const uint32_t frame[]={0xffffff40u,0xffffff41u,target,clip_fifo+CLIP_FIFO,0xffffff00u};copro_send(frame,5);
    const uint32_t start=hmm_core->millis();
    for(;;){
        if(hmm_core->ft_read(0x3020f8,&v)!=ZM_OK||v==4095)hmm_fail("FT CMD fault");
        if(v==command_position)break;
        if(hmm_core->millis()-start>2000)hmm_fail("FT CMD timeout");
        hmm_audio_poll();hmm_core->delay(1);
    }
}
void hmm_gpu_clip_show(fheroes2::Image &im,int x,int y,unsigned slot,uint32_t address,unsigned w,unsigned h){
    clip_frames[slot]={address,w,h};hmm_gpu_clip_draw(im,x,y,int(w),int(h),slot);
}
extern "C" void hmm_gpu_clip_end(){
    if(!clip_fifo)return;
    clip_fifo=0;for(auto &e:textures.entries)e.pins&=uint8_t(~CLIP_PIN);
}
extern "C" void hmm_gpu_shutdown(){cursor_task_stop();render_worker_stop();}
bool hmm_gpu_restore_allowed(){return hmm_running&&!hmm_exception_pending();}
extern "C" uint32_t hmm_gpu_pending_ms(){return 0;}
/* Клод: плотная карта идёт клетками — анимация только рядом с героем (замком) в фокусе и вода (patch_fast_frame.py, просьба пользователя:
 * «анимировать ближайшие к герою анимации и воду — при нехватке RAM_G»). Правка хода героя (Codex) это снимала: на плате (сборка 86)
 * карта перепекала ~47 клеток за кадр, сборка кадра 250–335 мс. */
bool hmm_gpu_animation_limited(){return map_tiles_shown;}
void hmm_gpu_status(const fheroes2::Image *image,int x,int y,bool once){if(image&&!status_image)status_since=hmm_core->millis();if(!image){status_hold=false;status_hold_live.store(false);}status_image=image;status_x=x;status_y=y;status_once=once;status_fresh=true;}
// Клод: «Map loading...» сразу, без задержки, и до лёгких кадров карты (см. status_hold).
// Клод: конец загрузки — карта проявлена, игра готова к ходу игрока: подпись снимается сразу (задача курсора прячет её без кадра).
// Ожидание первого опроса ввода давало ей кадр-другой поверх окна вступления, которое игра рисует до опроса (стенд, кадр 136).
bool hmm_status_holding(){return status_hold&&status_hold_live.load();}
extern "C" void hmm_status_hold_arm(){if(!status_hold)return;status_hold_armed=false;status_hold_live.store(false);status_reveal_at.store(0);}
extern "C" void hmm_status_input_seen(){if(!status_hold_armed)return;status_hold_armed=false;status_hold_live.store(false);status_reveal_at.store(0);}
void hmm_gpu_status_hold(const fheroes2::Image *image,int x,int y){hmm_gpu_status(image,x,y,false);status_since=hmm_core->millis()-STATUS_DELAY_MS;status_hold=true;status_hold_since=hmm_core->millis();status_calm=0;status_last_frame=0;status_frame_calm.store(false);status_hold_live.store(true);status_hold_armed=false;}
void hmm_gpu_cursor(const fheroes2::Image *image,int x,int y){
    cursor_image=image;cursor_x=x;cursor_y=y;
    if(image){const auto &mouse=LocalEvent::Get().getMouseCursorPos();cursor_origin_x=mouse.x;cursor_origin_y=mouse.y;
        // Клод: часы читаются один раз, здесь: кадр ещё не строится и запроса к SD нет.
        if(!cursor_clock_image)cursor_clock_image=&hmm_wait_cursor_image();}
}
extern "C" void hmm_gpu_stats(uint32_t *bytes,uint32_t *prepare,uint32_t *transfer){*bytes=frame_bytes;*prepare=prepare_ms;*transfer=frame_link;}
void hmm_gpu_fade(uint8_t black){fade_black=black;}
/* Клод: ручки FT812 — по всему кадру. Части карты и интерфейс собираются каждая со своими ручками с нуля (их слова кэшируются
 * по отдельности), и одна текстура в соседних частях привязывается заново: на стартовом виде Alteris у героя 379 повторных
 * BITMAP_SOURCE из 568, занято 10 ручек из 32 — 2515 слов при пределе 2048, и карта уходила в клетки процессора (а с ними —
 * память и SD; сборка 94 упала на плате в окне героя). Проход по готовому списку: команды ручек (BITMAP_HANDLE, SOURCE, LAYOUT,
 * SIZE и их _H) убираются, каждой вершине BITMAPS даётся ручка, где уже лежит её состояние; иначе — ручка, чьё состояние
 * понадобится позже всех (список известен целиком — вытеснение Белади), при равенстве — с меньшим числом дописываемых полей.
 * Остальные команды и порядок вершин те же, картинка та же. Модель на списке Alteris: 2515 → 1879 слов.
 * Работает в том же буфере dl, на длинных списках; если короче не вышло — список остаётся как был. Рабочая память (до ~25 КБ) —
 * только на время прохода. */
constexpr unsigned REHANDLE_FROM=1700;
// Клод: не встраивается в hmm_gpu_frame — тот лежит на самой глубокой цепочке стека (окно постройки в городе); массивы прохода — в куче.
HMM_RENDER_NOINLINE bool rehandle_dl_full(){
    constexpr unsigned STATES=512,HASH=1024;constexpr uint16_t NONE=0xffff;
    const unsigned n=words;unsigned vertices=0;
    for(unsigned i=0;i<n;++i)if((dl[i]>>30)==2)++vertices;
    if(!vertices||vertices>=NONE)return false;
    struct Work{uint32_t fields[STATES][5],in[32][5],out[32][5];uint16_t hash[HASH],first[STATES],input_state[32],held[32];
        uint8_t holder[STATES],known[32],out_known[32];};
    std::unique_ptr<Work> work(new(std::nothrow) Work);std::unique_ptr<uint16_t[]> order(new(std::nothrow) uint16_t[size_t(vertices)*2]);
    if(!work||!order)return false;
    uint16_t *const state_of=order.get(),*const next=state_of+vertices;
    // Состояние ручки каждой вершины BITMAPS (5 полей) — номер в таблице различных состояний.
    auto &in=work->in;auto &known=work->known;std::memset(known,0,sizeof(known));
    unsigned cur=0,prim=0,count=0,states=0;std::fill_n(work->hash,HASH,NONE);std::fill_n(work->input_state,32,NONE);
    for(unsigned i=0;i<n;++i){const uint32_t w=dl[i];const unsigned op=w>>24;
        if((w>>30)==2){if(prim!=1)continue;const unsigned h=(w>>7)&31;if(known[h]!=31)return false;
            // Several vertices can use an unchanged input binding. Its exact
            // interned state is valid until one of the five fields changes.
            if(work->input_state[h]!=NONE){state_of[count++]=work->input_state[h];continue;}
            uint32_t key=2166136261u;for(auto f:in[h])key=(key^f)*16777619u;
            for(unsigned slot=key&(HASH-1);;slot=(slot+1)&(HASH-1)){unsigned s=work->hash[slot];
                if(s==NONE){if(states==STATES)return false;s=states++;std::memcpy(work->fields[s],in[h],20);work->hash[slot]=uint16_t(s);}
                else {const auto &a=work->fields[s];const auto &b=in[h];if(a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2]||a[3]!=b[3]||a[4]!=b[4])continue;}
                state_of[count++]=work->input_state[h]=uint16_t(s);break;}
            continue;}
        if((w>>30)==1){if(prim==1)return false;continue;}
        if(op==5){cur=w&31;continue;}
        const int f=bitmap_field(w);if(f>=0){if(!(known[cur]&(1u<<f))||in[cur][f]!=w)work->input_state[cur]=NONE;in[cur][f]=w;known[cur]|=1u<<f;continue;}
        if(op==0x1f)prim=w&15;else if(op==0x1d||op==0x1e||op==0x22||op==0x23||op==0x24||op==0x25)return false;
    }
    // Следующее использование того же состояния после каждой вершины; first — первое использование.
    std::fill_n(work->first,states,NONE);
    for(unsigned v=count;v--;){const unsigned s=state_of[v];next[v]=work->first[s];work->first[s]=uint16_t(v);}
    auto &out=work->out;auto &out_known=work->out_known;unsigned out_cur=0;
    /* Вход переносится в конец буфера dl, результат пишется с его начала: местами результат длиннее входа (ручку пришлось
     * вытеснить там, где вход её держал), и на месте он обгонял непрочитанные слова (обход Alteris, вид 81,51).
     * Клод: проход один (прежде — пробный и рабочий): если результат не короче входа или догнал непрочитанное, вход возвращается
     * на место. Вытесняемая ручка — по far[c] (следующее использование её состояния, обновляется на каждой вершине), а число
     * дописываемых полей считается только среди ручек с одинаковым far: стенд, ход по Alteris — проход занимал ~80 % сборки кадра. */
    const unsigned gap=DL_CAPACITY-n;
#ifdef HMM2_HOST
    const std::vector<uint32_t> before(dl,dl+n);
#endif
    std::memmove(dl+gap,dl,n*4);const uint32_t *const source=dl+gap;
    std::fill_n(work->holder,states,uint8_t(0xff));
    // В начале списка текущая ручка — 0 (контекст FT812 сбрасывается; так же считает word() по defaults), поля ручек неизвестны.
    std::memset(out_known,0,sizeof(out_known));auto &held=work->held;std::fill_n(held,32,NONE);
    uint32_t far[32];std::fill_n(far,32,0x10000u);
    unsigned o=0,v=0,p=0;bool ok=true;
    for(unsigned i=0;i<n&&ok;++i){const uint32_t w=source[i];const unsigned op=w>>24;
        if((w>>30)==2&&p==1){
            const unsigned s=state_of[v];const uint16_t after=next[v];++v;
            uint32_t put[8];unsigned k=0,h=work->holder[s];
            if(h==0xff){
                unsigned best_far=0,ties=0;
                for(unsigned c=0;c<32;++c){if(far[c]>best_far){best_far=far[c];ties=1;}else if(far[c]==best_far)++ties;}
                unsigned best=0,best_cost=~0u;
                for(unsigned c=0;c<32;++c){if(far[c]!=best_far)continue;
                    if(ties==1){best=c;break;}
                    unsigned cost=c!=out_cur;
                    if(!out_known[c])cost+=5;
                    else if(out_known[c]==31){const auto &a=out[c];const auto &b=work->fields[s];cost+=(a[0]!=b[0])+(a[1]!=b[1])+(a[2]!=b[2])+(a[3]!=b[3])+(a[4]!=b[4]);}
                    else for(unsigned f=0;f<5;++f)cost+=!(out_known[c]&(1u<<f))||out[c][f]!=work->fields[s][f];
                    if(cost<best_cost){best=c;best_cost=cost;}}
                h=best;if(held[h]!=NONE)work->holder[held[h]]=0xff;held[h]=uint16_t(s);work->holder[s]=uint8_t(h);
                if(h!=out_cur){put[k++]=0x05000000|h;out_cur=h;}
                for(unsigned f=0;f<5;++f){const uint32_t need=work->fields[s][f];
                    if(!(out_known[h]&(1u<<f))||out[h][f]!=need){put[k++]=need;out[h][f]=need;out_known[h]|=1u<<f;}}
            }
            far[h]=after==NONE?0x10000u:after;put[k++]=(w&~(31u<<7))|(h<<7);
            // Результат не должен догнать ещё не прочитанные слова входа (вход лежит с dl+gap).
            if(o+k>gap+i+1){ok=false;break;}
            std::memcpy(dl+o,put,k*4);o+=k;continue;
        }
        if((w>>30)==0&&(op==5||bitmap_field(w)>=0))continue;
        if((w>>30)==0&&op==0x1f)p=w&15;
        dl[o++]=w;
    }
    if(!ok||o>=n){std::memmove(dl,dl+gap,n*4);return false;}
    words=o;
#ifdef HMM2_HOST
    // Клод: проверка на ПК — каждая вершина BITMAPS рисует с теми же полями ручки, прочие команды те же и в том же порядке.
    auto resolve=[](const uint32_t *p,unsigned size){std::vector<uint32_t> r;uint32_t st[32][5]{};unsigned h=0,pr=0;
        for(unsigned i=0;i<size;++i){const uint32_t w=p[i];const unsigned op=w>>24;
            if((w>>30)==2&&pr==1){const unsigned v=(w>>7)&31;r.insert(r.end(),st[v],st[v]+5);r.push_back(w&~(31u<<7));continue;}
            if((w>>30)==0&&op==5){h=w&31;continue;}
            const int f=(w>>30)==0?bitmap_field(w):-1;if(f>=0){st[h][f]=w;continue;}
            if((w>>30)==0&&op==0x1f)pr=w&15;r.push_back(w);}
        return r;};
    if(resolve(before.data(),n)!=resolve(dl,words))hmm_fail("rehandle changed the display list");
#endif
    // Отслеживание повторов word() — по новому состоянию ручек: дальше идут подпись и курсор.
    std::memset(compact_mask,0,sizeof(compact_mask));
    for(unsigned h=0;h<32;++h){compact_mask[h]=out_known[h];std::memcpy(compact_bitmap[h],out[h],20);}
    compact_handle=out_cur;compact_valid[5]=1;compact_state[5]=0x05000000|out_cur;
    return true;
}
#include "hmm_rehandle_replay.inc"
#include "hmm_body_plan.inc"
extern "C" HMM_SIZE_RENDER void hmm_gpu_scene_frame(const uint8_t*,const uint8_t *rgb,int w,int h){
    Prof prof_frame(P_FRAME);
    if(w!=640||h!=480)hmm_fail("FT resolution mismatch");
    if(loader_active)loader_finish();
    hmm_audio_tick();
    ++canvas_frame;canvases_release_idle();
    {static unsigned shrink_frames;if(!(++shrink_frames&63))canvases_shrink();}
    // Codex: скрытые слои удаляются и из живого кадра. ImageRestorer хранит
    // собственное происхождение; ему не нужна копия уже закрытых слоёв дисплея.
    auto start=hmm_core->millis();auto &screen=frame_canvas();auto &draws=screen.draws;
    ring_build_bank=ring_display_bank^1u;
    cluster_retry=skeleton_retry=false;
    bool tiled_retry=false,ui_retry=false,baked_retry=false,whole_retry=false,full_retry=false,merge_retry=false,cover_retry=false,motion_retry=false,actor_retry=false,handle_retry=false;tile_map_active=false;
    // Клод: карта закрыта целиком (см. map_covered) — плотный режим снят; вернётся сам, когда DL карты снова переполнится.
    if(map_covered){dense_mode=false;map_merge_wanted=map_cover_wanted=false;if(!map_released){map_released=true;map_release_covered();}}
    // Клод: пик слоёв кадра до клеток за 100 кадров (peak= в HMM2 GPU): по нему видно, какой экран упирается в 800 слоёв и DL.
    stat_draws_peak=std::max(stat_draws_peak,unsigned(draws.size()+map_draws.size()));
rebuild:
    cluster_failed=cluster_used=skeleton_used=false;
    rehandle_force_full=handle_retry;rehandle_fast_used=false;
    body_actor_spans.clear();body_actor_collect=false;
    // Every frame first tries the current hardware list. A previous overflow
    // must not postpone recovery after scrolling away or closing a window.
    const bool interface_changed=interface_revision!=screen.revision;
    if(interface_changed&&!screen.prepared){occlude(draws);repeat_fog(draws);join_layers(draws);screen.prepared=true;}
    // Клод: непрозрачные окна экрана над картой — куски карты под ними не выводятся (только после переполнения, map_cover_wanted).
    map_covers_refresh(draws);
#ifdef HMM2_HOST
    // Codex: точечная диагностика границы карты, только в явном режиме ПК.
    if(std::getenv("HMM2_RING_TRACE")&&serial+1==unsigned(std::atoi(std::getenv("HMM2_RING_TRACE")))){
        ring_expand();std::printf("RING TRACE origin=%d,%d window=%d,%d,%d,%d ring=%d\n",map_origin_x,map_origin_y,map_window.x,map_window.y,map_window.w,map_window.h,int(ring_frame));
        for(const auto &d:map_draws)if(d.r.x<=463&&d.r.x+d.r.w>463&&d.r.y<=191&&d.r.y+d.r.h>191){
            const auto t=d.resource?hmm_resource_ft(d.resource,d.raw):HmmFtImage{};
            std::printf("RING DRAW res=%u r=%d,%d,%d,%d src=%d,%d step=%d raw=%d mask=%d ft=%u,%u,%u,%u\n",d.resource,d.r.x,d.r.y,d.r.w,d.r.h,d.sx,d.sy,d.step,int(d.raw),int(d.mask),t.offset,t.stride,t.height,t.cell);
        }
    }
    if(const char *path=std::getenv("HMM2_DRAW_LOG")){
        FILE *f=std::fopen(path,"a");
        if(f){for(const auto &d:draws){auto t=d.resource?hmm_resource_ft(d.resource,d.raw):HmmFtImage{};
            std::fprintf(f,"%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%u,%u,%u,%u\n",serial+1,d.resource,d.r.x,d.r.y,d.r.w,d.r.h,d.sx,d.sy,d.step,d.mapping,d.fill,d.opaque,t.offset,t.stride,t.height,t.cell);}
            std::fclose(f);}
    }
#endif
    // Codex: закрытая или обрезанная карта идёт общим путём; её полный DL не удерживает RAM_G.
    const bool full_map=std::any_of(draws.begin(),draws.end(),[](const Draw &d){const auto r=intersect(map_bounds,d.r);return d.layer&&r.w==map_bounds.w&&r.h==map_bounds.h;});
    if(full_map||!std::any_of(draws.begin(),draws.end(),[](const Draw &d){return d.layer;}))map_clips_clear();
    if(!full_map)for(auto &part:map_parts)if(!part.textures.empty())map_cache_release(part);
    bool reuse_body=ring_frame&&full_map&&!tiled_retry&&interface_revision==screen.revision&&
        body_screen_revision==screen.revision&&(body_ring_revision==ring_revision||body_motion_reusable())&&!body_plan.commands.empty();
#ifdef HMM2_HOST
    if(std::getenv("HMM2_NO_BODY_PLAN"))reuse_body=false;
#endif
    if(!reuse_body)body_plan_clear();
    motion_pending.clear();motion_active=false;
    motion_candidate=ring_frame&&full_map&&!tiled_retry&&!motion_retry&&map_covers.empty()&&
        !(motion_blocked_revision==ring_contents_revision&&motion_blocked_x==ring_x&&motion_blocked_y==ring_y);
#ifdef HMM2_HOST
    if(std::getenv("HMM2_NO_MOTION_PLAN"))motion_candidate=false;
#endif
    body_actor_collect=motion_candidate&&ring_hero_changed&&!actor_retry&&!reuse_body;
#ifdef HMM2_HOST
    if(std::getenv("HMM2_NO_ACTOR_BODY"))body_actor_collect=false;
#endif
    ring_used=body_palette_fill=false;texture_missed=false;
    hft_cache_begin(&textures,displayed);words=0;dl_overflow=false;reset_emitter();
    std::memset(compact_valid,0,sizeof(compact_valid));std::memset(compact_mask,0,sizeof(compact_mask));compact_handle=0;
    // Codex: начальный контекст FT812 подтверждён cx_default/CTX_DEF в RTL, без повторных установок.
    const uint32_t defaults[]={0x02000000,0x04ffffff,0x05000000,0x0b000014,0x0e000010,0x100000ff,0x15000100,0x16000000,0x17000000,0x18000000,0x1a000000,0x1b000000,0x27000004,0x2b000000,0x2c000000};
    for(auto v:defaults){compact_state[v>>24]=v;compact_valid[v>>24]=1;}
    palette_mapping=~0u;
    if(!palette_epoch||std::memcmp(rgb,last_palette,768)){
        map_clips_clear();++palette_epoch;std::memcpy(last_palette,rgb,768);
        for(unsigned i=0;i<256;++i)palette_rgb[i]=0xff000000|(uint32_t(rgb[i*3])<<16)|(uint32_t(rgb[i*3+1])<<8)|rgb[i*3+2];
    }
    // Codex: FT812 SRC_ALPHA=2, ONE_MINUS_SRC_ALPHA=4; значения проверяются настоящим RTL.
    const uint32_t initial[]={0x02000000,0x26000007,0x100000ff,0x0b000014,0x16000000,0x18000000,0x19000100,0x27000004,0x1c2801e0};
    if(!reuse_body)for(auto command:initial)set(command);
    // Codex: общий масштаб scanout исключает дробную фазу и швы отдельных битмапов.
    // Codex: интерфейс, карта и курсор имеют отдельные сроки жизни, как блоки RF_Frame.
    // Заместители вызывают актуальную карту: её анимации не входят в кэш интерфейса.
    unsigned map_words=0,before_rehandle=0,after_rehandle=0;
    if(reuse_body){Prof prof_emit(P_EMIT);body_plan_emit();before_rehandle=after_rehandle=words;}
    else{
    {Prof prof_emit(P_EMIT);
    if(reuse_body)body_plan_emit();
    else{
    if(interface_changed||(interface_dl.fill&&interface_dl.palette!=palette_epoch)){
        map_cache_release(interface_dl);map_compiling=&interface_dl;reset_emitter();interface_dl.fill=false;texture_missed=false;
        for(size_t i=0;i<draws.size();){
            if(draws[i].layer){word(0xff000000|uint32_t(i++));reset_emitter();}
            else {const auto begin=i;while(i<draws.size()&&!draws[i].layer){if(draws[i].fill)interface_dl.fill=true;++i;}emit_sequence(draws.begin()+begin,i-begin);}
        }
        // Клод: интерфейс, собранный с пропущенным слоем, пересобирается в следующем кадре.
        map_compiling=nullptr;interface_revision=texture_missed?0:screen.revision;interface_dl.palette=palette_epoch;
    }
    cached_textures_use(interface_dl);
    for(auto command:interface_dl.commands){
        if((command>>24)==0xff){const unsigned at=words;motion_active=motion_candidate;emit_map(draws[command&0xffffff]);motion_active=false;map_words+=words-at;}
        else {if((command>>24)==0x2a)command=0x2a000000|palette_address(command&0xffffff);word(command);}
    }
    // Клод: длинный список — ручки FT812 раздаются заново по всему кадру (rehandle_dl); подпись и курсор пишутся уже после.
    }
    }
    if(cluster_failed&&skeleton_used&&!skeleton_retry){
        skeleton_retry=true;++skeleton_fallbacks;ring_clusters_clear(false);body_plan_clear();ring_sprite_plan_clear();ring_live_revision=0;goto rebuild;
    }
    if(cluster_failed&&!cluster_retry){
        cluster_retry=true;++cluster_fallbacks;ring_clusters_clear(false);ring_sprite_plan_clear();ring_live_revision=0;goto rebuild;
    }
    before_rehandle=words;
    if(words>REHANDLE_FROM&&!dl_overflow)rehandle_dl();
    after_rehandle=words;
    if(reuse_body&&body_dynamic&&(dl_overflow||words>2000||texture_missed)){
        actor_retry=true;body_plan_clear();goto rebuild;
    }
    if(motion_candidate&&!dl_overflow){
        const bool captured=body_motion_capture();
        if(!captured||words>2000||body_palette_fill){
            motion_blocked_revision=ring_contents_revision;motion_blocked_x=ring_x;motion_blocked_y=ring_y;
            motion_retry=true;goto rebuild;
        }
    }
    if(!body_actor_spans.empty())body_actor_capture();
    if((!reuse_body||(body_dynamic&&!ring_hero_changed))&&ring_used&&ring_frame&&full_map&&!tiled_retry&&!dl_overflow&&words<=2000&&
        interface_revision==screen.revision&&!interface_dl.fill&&!body_palette_fill&&!texture_missed)
        body_plan_store(screen.revision);
    }
    if(reuse_body&&body_dynamic){
        if(dl_overflow||words>2000||texture_missed){actor_retry=true;body_plan_clear();goto rebuild;}
        if(!ring_hero_changed){const bool saved=motion_candidate;motion_candidate=false;body_plan_store(screen.revision);motion_candidate=saved;}
    }
    reset_emitter();
    // Клод: шаг затемнения экрана (fadeDisplay) — чёрный прямоугольник с альфой поверх экрана, под подписью и курсором.
    if(fade_black){set(0x2b000000);set(0x2c000000);set(0x1b000000);set(0x1c2801e0);set(0x1f000009);set(0x04000000);set(0x0e000010);
        word(0x10000000|fade_black);word(0x40000000);word(0x40000000|(uint32_t(640*16)<<15)|uint32_t(480*16));word(0x100000ff);reset_emitter();}
    message_scene_capture();
    // Клод: программный курсор — последним слоем поверх кадра, в Display он не пишется.
    cursor_pending_count=0;cursor_overflow=false;
    set(0x1b000000);set(0x1c2801e0);
    // Клод: подпись паузы поверх кадра, под курсором; разовая живёт один кадр.
    if(status_image&&status_once&&!status_fresh)status_image=nullptr;
    status_fresh=false;
    // Клод: «Map loading...», уже снятая задачей курсора, уходит и из списка.
    if(status_hold&&!status_hold_live.load()){status_image=nullptr;status_hold=false;}
    // Клод: подпись со сплошной заливкой сдвигом не двигать — такая показывается сразу и без сдвига, как прежде.
    const bool status_patchable=status_image&&std::none_of(canvas(*status_image).draws.begin(),canvas(*status_image).draws.end(),[](const Draw &d){return d.fill||d.layer;});
    const bool status_hidden=status_patchable&&hmm_core->millis()-status_since<STATUS_DELAY_MS;
    if(status_image){cursor_record=status_patchable;cursor_layer=2;
        for(auto d:canvas(*status_image).draws){d.r.x+=status_x;d.r.y+=status_y+(status_hidden?CURSOR_HIDE:0);if(d.r.w&&d.r.h)emit(d);}
        cursor_record=false;}
    // Клод: положение — самое новое, а не то, что игра успела разобрать: кадр занятой игры не отбрасывает курсор назад.
    int cursor_mouse_x,cursor_mouse_y;
    if(cursor_live())hmm_loading_mouse(&cursor_mouse_x,&cursor_mouse_y);
    else {const auto &mouse=LocalEvent::Get().getMouseCursorPos();cursor_mouse_x=mouse.x;cursor_mouse_y=mouse.y;}
    const int cursor_now_x=cursor_x+cursor_mouse_x-cursor_origin_x,cursor_now_y=cursor_y+cursor_mouse_y-cursor_origin_y;
    cursor_record=true;cursor_layer=0;
    if(cursor_image&&!cursor_image->empty()){
        for(auto d:canvas(*cursor_image).draws){
            d.r.x+=cursor_now_x;d.r.y+=cursor_now_y;
            // Codex: SCISSOR обрезает у края, геометрия сохраняется для движения обратно.
            if(d.fill||d.layer)cursor_overflow=true;
            if(d.r.w&&d.r.h)emit(d);
        }
        // Клод: часы лежат в том же списке за нижним краем; задача курсора показывает их сдвигом, без нового кадра и без SD.
        if(cursor_clock_image&&!cursor_clock_image->empty()){
            cursor_layer=1;
            for(auto d:canvas(*cursor_clock_image).draws){
                d.r.x+=cursor_mouse_x-cursor_clock_image->width()/2;d.r.y+=cursor_mouse_y-cursor_clock_image->height()/2+CURSOR_HIDE;
                if(d.fill||d.layer)cursor_overflow=true;
                if(d.r.w&&d.r.h)emit(d);
            }
        }
    }
    cursor_record=false;
    word(0x21000000);word(0);
    const unsigned raw_words=words;
    const bool compacted=!dl_overflow&&words>DL_LIMIT;
#ifdef HMM2_HOST
    static std::vector<uint32_t> raw_dl;raw_dl.assign(dl,dl+words);
#endif
    if(compacted){if(!bitmap_prior_valid)previous_bitmap_state();compact_bitmap_constants();}
    if((dl_overflow||words>DL_LIMIT)&&rehandle_fast_used&&!handle_retry){
        handle_retry=true;body_plan_clear();goto rebuild;
    }
    // Codex: геометрия малого обновления может идти без общей обработки. Если DL вырос,
    // сначала применяем исходные сокращения; переполненный рабочий буфер не публикуется.
    if((dl_overflow||words>DL_LIMIT)&&skeleton_used&&!skeleton_retry){
        skeleton_blocked_static=ring_static_revision;skeleton_blocked_screen=screen.revision;
        skeleton_blocked_x=ring_x;skeleton_blocked_y=ring_y;skeleton_blocked_cols=ring_cols;skeleton_blocked_rows=ring_rows;
        skeleton_retry=true;++skeleton_fallbacks;ring_clusters_clear(false);body_plan_clear();ring_sprite_plan_clear();ring_live_revision=0;goto rebuild;
    }
    if((dl_overflow||words>DL_LIMIT)&&cluster_used&&!cluster_retry){
        cluster_blocked_static=ring_static_revision;cluster_blocked_screen=screen.revision;
        cluster_blocked_x=ring_x;cluster_blocked_y=ring_y;cluster_blocked_cols=ring_cols;cluster_blocked_rows=ring_rows;
        cluster_retry=true;++cluster_fallbacks;ring_clusters_clear(false);body_plan_clear();ring_sprite_plan_clear();ring_live_revision=0;goto rebuild;
    }
    if((dl_overflow||words>DL_LIMIT)&&!ui_retry){occlude(draws);repeat_fog(draws);join_layers(draws);screen.prepared=true;interface_revision=0;ui_retry=true;goto rebuild;}
    // Клод: карта не влезла — сначала куски карты под непрозрачным окном экрана (map_covers_refresh: дёшево, без памяти),
    // затем склейка кусков её спрайтов (map_merge_plan), и только потом клетки процессора.
    if((dl_overflow||words>DL_LIMIT)&&!cover_retry&&!map_cover_wanted&&(!map_parts.empty()||ring_frame)&&!tiled_retry){cover_retry=map_cover_wanted=true;interface_revision=0;goto rebuild;}
    if((dl_overflow||words>DL_LIMIT)&&!merge_retry&&!map_merge_wanted&&!map_parts.empty()&&!tiled_retry){merge_retry=map_merge_wanted=true;map_merge_refresh();interface_revision=0;goto rebuild;}
    // Codex: ключ каждого участка учитывает исходные слои и выбранные движком кадры; целый экран не замораживается.
    // Клод: в журнал платы — сколько слов был список, когда карта уходит в клетки (раз на вход в плотный режим).
    if((dl_overflow||words>DL_LIMIT)&&!tiled_retry&&!dense_mode){char m[96];std::snprintf(m,sizeof(m),"HMM2: map list overflow words=%u map=%u screen=%u",words,unsigned(map_draws.size()),unsigned(draws.size()));hmm_log(m);
        // Клод: раскладка для журнала платы: слова до и после прохода ручек, из них слова частей карты; слои экрана в окне карты.
        {unsigned inside=0;for(const auto &d:draws)if(!d.layer){const auto r=intersect(d.r,map_window);inside+=r.w&&r.h;}
         std::snprintf(m,sizeof(m),"HMM2: list detail pre=%u post=%u mapw=%u inmap=%u",before_rehandle,after_rehandle,map_words,inside);hmm_log(m);}
#ifdef HMM2_HOST
        // Клод: разбор переполнения на ПК — слова частей карты по видам и весь список в файл HMM2_OVERFLOW_DL.
        if(const char *path=std::getenv("HMM2_OVERFLOW_DL")){
            size_t kinds[4]{},layers[4]{},merged=0,skipped=0;
            for(const auto &p:map_parts){kinds[p.kind&3]+=p.commands.size();layers[p.kind&3]+=p.count;}
            for(auto v:map_merge_at){if(v>=0)++merged;else if(v==-2)++skipped;}
            std::printf("HMM2 overflow parts: static=%zu/%zu animated=%zu/%zu fog=%zu/%zu edges=%zu/%zu interface=%zu merged=%zu skipped=%zu\n",
                kinds[0],layers[0],kinds[1],layers[1],kinds[2],layers[2],kinds[3],layers[3],interface_dl.commands.size(),merged,skipped);
            char name[512];std::snprintf(name,sizeof(name),"%s.%u",path,serial+1);
            if(FILE *f=std::fopen(name,"wb")){std::fwrite(raw_dl.data(),4,raw_dl.size(),f);std::fclose(f);}
        }
#endif
    }
    if((dl_overflow||words>DL_LIMIT)&&!tiled_retry&&cache_map_tiles()){dense_mode=true;tiled_retry=true;interface_revision=0;goto rebuild;}
    // Codex: динамические хвосты переполнили DL — точный запасной список целых клеток; бюджет сокращается.
    if((dl_overflow||words>DL_LIMIT)&&tiled_retry&&!tiles_baked.empty()){
        if(tile_map_active){tile_map_draws.swap(tiles_baked);tile_map_signature=0;}
        else {screen.draws.swap(tiles_baked);changed(screen);tiles_signature=0;}
        tiles_baked.clear();interface_revision=0;live_budget=std::max(16u,live_budget/2);goto rebuild;
    }
    // Codex: при совпадении подписи запасной tiles_baked уже освобождён прошлым кадром.
    // Новый интерфейс может не поместиться с прежним живым хвостом: пересобираем только
    // клетки карты, не заставляя CPU распаковывать большие фоны самого окна.
    if((dl_overflow||words>DL_LIMIT)&&tiled_retry&&!baked_retry){
        baked_retry=true;
        if(cache_map_tiles(false,true)){interface_revision=0;goto rebuild;}
    }
    // Codex: восстановленное окно может оставить сложный интерфейс поверх карты.
    // Если даже целые клетки карты не помещаются вместе с ним, прежний общий путь
    // запекает также перекрывающие слои, сохраняя все пиксели и порядок рисования.
    if((dl_overflow||words>DL_LIMIT)&&tiled_retry&&!whole_retry){
        whole_retry=true;
        if(cache_map_tiles(true)){interface_revision=0;goto rebuild;}
    }
    // Клод: целые клетки в окне карты не помогли — по всему экрану (прежний запасной путь).
    if((dl_overflow||words>DL_LIMIT)&&tiled_retry&&whole_retry&&!full_retry){
        full_retry=true;
        if(cache_map_tiles(true,false,true)){interface_revision=0;goto rebuild;}
    }
    if(dl_overflow||words>DL_LIMIT){
#ifdef HMM2_HOST
        // Codex: сохраняем переполненный список для разбора; в FT812 он никогда не отправляется.
        if(const char *path=std::getenv("HMM2_DL_DIAGNOSTIC")){FILE*f=std::fopen(path,"wb");if(f){std::fwrite(dl,4,words,f);std::fclose(f);}}
        std::printf("HMM2 rejected DL: frame=%u words=%u draws=%zu\n",serial+1,words,draws.size());
#endif
        char rejected[80];std::snprintf(rejected,sizeof(rejected),"HMM2 rejected DL frame=%u words=%u",serial+1,words);hmm_log(rejected);
        hmm_fail("FT display list exceeds 8 KiB");
    }
#ifdef HMM2_HOST
    // Клод: на ПК (HMM2_MAPDL_STATS) — итоговая длина списка каждого кадра и слова до/после прохода ручек: для обхода карт.
    if(std::getenv("HMM2_MAPDL_STATS"))std::printf("FINALDL words=%u tiled=%d pre=%u post=%u mapw=%u screen=%zu\n",words,int(tiled_retry),before_rehandle,after_rehandle,map_words,draws.size());
#endif
    // Клод: публикация и новые якоря курсора — без задачи курсора; её DLSWAP мог ещё не сработать, дожидаемся.
    GpuBusy busy;Prof prof_publish(P_PUBLISH);
    {uint32_t v;const uint32_t begun=hmm_core->millis();do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("FT DLSWAP read");if(hmm_core->millis()-begun>2000)hmm_fail("FT DLSWAP timeout");}while(v);}
    bool swap=copro_unsettled||lastwords!=words||std::memcmp(dl,lastdl,words*4);
    if(swap){publish_dl();uint32_t v=2;put(0x302054,&v,1);displayed^=1;std::memcpy(lastdl,dl,words*4);lastwords=words;}
    else for(auto &e:textures.entries)if(e.valid&&(e.pins&(1u<<textures.build_slot)))e.pins|=1u<<displayed;
    if(compacted)for(unsigned h=0;h<32;++h)if(bitmap_seen[h]){for(unsigned f=0;f<5;++f)if(bitmap_seen[h]&(1u<<f))bitmap_prior[h][f]=bitmap_last[h][f];bitmap_known[h]|=bitmap_seen[h];}
    bitmap_prior_valid=compacted;
    cursor_count=cursor_overflow?0:cursor_pending_count;
    std::copy(cursor_pending,cursor_pending+cursor_count,cursor_committed);
    status_emitted_hidden.store(status_hidden);status_reveal_at.store(status_patchable&&!cursor_overflow?status_since+STATUS_DELAY_MS:0);
    cursor_anchor_x=cursor_mouse_x;cursor_anchor_y=cursor_mouse_y;
    const unsigned palette_bank=palette_epoch&1;
    std::memcpy(palette_bank_rgb[palette_bank],palette_rgb,sizeof(palette_rgb));palette_bank_epoch[palette_bank]=palette_epoch;
    if(ring_used&&!tiled_retry)ring_display_bank=ring_build_bank;
    ++serial;
    prepare_ms=unsigned(hmm_core->millis()-start)-link_ms;
    if(map_tiles_shown&&!tiled_retry)hmm_log("HMM2: hardware sprite map restored");
    map_tiles_shown=tiled_retry;
    /* Клод: склейка (map_merge_plan) снимается, когда кадр уложился с запасом и без неё (слова + по 2 на каждый пропущенный ею
     * кусок < 1800): с проходом ручек она нужна редко, а включённая пересчитывалась в каждом кадре анимации плотной карты — память
     * (на ходу по Alteris отказ 20 КБ) и время. Понадобится снова — вернётся через ступень повтора переполнения. */
    if(map_merge_wanted&&!merge_retry&&!tiled_retry&&words+2*map_merge_skipped<1800){map_merge_wanted=false;map_merge_refresh();}
    if(!tiled_retry){dense_mode=false;live_budget=160;}
    /* Клод: «Map loading...» держится с начала StartGame до проявления карты (hmm_status_hold_arm в HumanTurn); запасной срок — 60 с.
     * Прежнее снятие по трём «лёгким» кадрам и задачей курсора при ожидании ввода убрано: подпись теперь ставится раньше, и пока
     * StartGame рисует интерфейс с пустой картой, лёгкие кадры сняли бы её (плата, сборка 95: чёрная карта без подписи). */
    if(status_hold){const uint32_t now=hmm_core->millis();
        const bool calm=prepare_ms<40&&traffic<16384&&status_last_frame&&now-status_last_frame<400;
        status_frame_calm.store(prepare_ms<40&&traffic<16384);status_frame_at.store(now);
        status_calm=calm?status_calm+1:0;status_last_frame=now;
        if(now-status_hold_since>60000){status_image=nullptr;status_hold=false;status_hold_live.store(false);}}
    /* Клод: запасной список нужен только внутри кадра; прошлый список клеток — только пока кадры идут клетками. Иначе после одного
     * кадра клеток они держали его слои до конца игры: GateHell (сборка 80, QEMU) — 100+ КиБ PSRAM, и картинке 640×480 не хватило места. */
    Sequence<Draw>().swap(tiles_baked);if(!tiled_retry&&!tiles_out.empty())tiles_forget();
    if(!tiled_retry){Sequence<Draw>().swap(tile_map_draws);tile_map_signature=0;}
    if(serial==1||serial%100==0){char message[256];std::snprintf(message,sizeof(message),"HMM2 GPU: frame=%u ICN=%u peak=%u LINK=%u Z80_RAMG=%u DL=%u raw_DL=%u prep=%u tx=%u swap=%u cells=%u uploaded=%u live=%u budget=%u limited=%u",serial,unsigned(draws.size()+map_draws.size()),stat_draws_peak,traffic,gpu_upload_bytes,words*4,raw_words*4,prepare_ms,link_ms,unsigned(swap),stat_cells,stat_cell_uploads,stat_live_layers,live_budget,unsigned(tiled_retry));hmm_log(message);stat_cells=stat_cell_uploads=stat_live_layers=stat_draws_peak=0;
        /* Клод: время участков за 100 кадров, мс при 240 МГц (включительно): frame — весь hmm_gpu_frame, emit — интерфейс и карта в DL,
         * parts — сборка частей карты, rehandle — проход ручек, merge — план склейки, cells — клетки CPU, occlude — отсечение, publish —
         * отправка DL; offer/record/canvas/cover/pixels/fill — запись слоёв движком (это время входит в engine строки HMM2 PERF). */
        auto ms=[](unsigned id){return unsigned(prof_cycles[id]/240000u);};
        // Codex: ABI журнала режет текст по 120 байт; обе строки целиком
        // сохраняют счётчики, в том числе отдельную подготовку текстур кольца.
        std::snprintf(message,sizeof(message),"HMM2 TIME frame=%u emit=%u parts=%u rehandle=%u merge=%u cells=%u occlude=%u publish=%u",
            ms(P_FRAME),ms(P_EMIT),ms(P_PARTS),ms(P_REHANDLE),ms(P_MERGE),ms(P_CELLS),ms(P_OCCLUDE),ms(P_PUBLISH));hmm_log(message);
#if defined(HMM2_HOST) || defined(HMM2_VERBOSE_GPU)
        std::snprintf(message,sizeof(message),"HMM2 TIME2 offer=%u record=%u canvas=%u cover=%u pixels=%u fill=%u tileprep=%u",
            ms(P_OFFER),ms(P_RECORD),ms(P_CANVAS),ms(P_COVER),ms(P_PIXELS),ms(P_FILL),ms(P_TILEPREP));
        hmm_log(message);
#endif
        std::snprintf(message,sizeof(message),"HMM2 TIME3 body_store=%u background=%u live=%u sprite_build=%u sprite_replay=%u",
            ms(P_BODY_STORE),ms(P_BACKGROUND),ms(P_LIVE),ms(P_SPRITE_BUILD),ms(P_SPRITE_REPLAY));hmm_log(message);std::memset(prof_cycles,0,sizeof(prof_cycles));
        std::snprintf(message,sizeof(message),"HMM2 MOTION camera=%u actor=%u retry=%u handles=%u/%u",ring_camera_reuse,ring_subject_reuse,ring_subject_retry,stat_handles_reuse,stat_handles_full);hmm_log(message);
        ring_camera_reuse=ring_subject_reuse=ring_subject_retry=0;
#if defined(HMM2_HOST) || defined(HMM2_VERBOSE_GPU)
        if(ring_enabled()){std::snprintf(message,sizeof(message),"HMM2 RING hit=%u built=%u fallback=%u recorded=%u",ring_hits,ring_builds,ring_fallbacks,ring_recorded);hmm_log(message);ring_hits=ring_builds=ring_fallbacks=ring_recorded=0;}
#ifdef HMM2_HOST
        {size_t retained=0;for(const auto &p:ring_cells)if(p)retained+=p->pending.capacity()*sizeof(RingDraw);
        std::printf("HMM2 RING buffers grows=%u retained=%zu\n",ring_pending_grows,retained);ring_pending_grows=0;}
#endif
        std::snprintf(message,sizeof(message),"HMM2 CLUSTER hit=%u built=%u fallback=%u",cluster_hits,cluster_builds,cluster_fallbacks);hmm_log(message);cluster_hits=cluster_builds=cluster_fallbacks=0;
        std::snprintf(message,sizeof(message),"HMM2 HERO base_hit=%u base_build=%u parts=%u retry=%u",skeleton_hits,skeleton_builds,hero_slot_parts,skeleton_fallbacks);hmm_log(message);skeleton_hits=skeleton_builds=hero_slot_parts=skeleton_fallbacks=0;
        {unsigned hit,built,bytes;hmm_monster_parts_stats(&hit,&built,&bytes,1);std::snprintf(message,sizeof(message),"HMM2 PARTS hit=%u built=%u bytes=%u",hit,built,bytes);hmm_log(message);}
        std::snprintf(message,sizeof(message),"HMM2 PLAN hit=%u built=%u move=%u dynamic=%u",body_hits,body_builds,body_move_hits,body_dynamic_hits);hmm_log(message);body_hits=body_builds=body_move_hits=body_dynamic_hits=0;
        std::snprintf(message,sizeof(message),"HMM2 OPT merge_full=%u merge_reuse=%u handles_full=%u handles_reuse=%u",stat_merge_full,stat_merge_reuse,stat_handles_full,stat_handles_reuse);
        hmm_log(message);stat_merge_full=stat_merge_reuse=stat_handles_full=stat_handles_reuse=0;
#else
        unsigned hit,built,bytes;hmm_monster_parts_stats(&hit,&built,&bytes,1);
        std::snprintf(message,sizeof(message),"HMM2 CACHE body=%u/%u dynamic=%u hero=%u/%u recipes=%u/%u bytes=%u",
            body_hits,body_builds,body_dynamic_hits,skeleton_hits,skeleton_builds,hit,built,bytes);hmm_log(message);body_dynamic_hits=0;
        body_hits=body_builds=body_move_hits=skeleton_hits=skeleton_builds=skeleton_fallbacks=hero_slot_parts=0;
        cluster_hits=cluster_builds=cluster_fallbacks=ring_hits=ring_builds=ring_fallbacks=ring_recorded=0;
        stat_merge_full=stat_merge_reuse=stat_handles_full=stat_handles_reuse=0;
#endif
    }
    frame_bytes=traffic;frame_link=link_ms;traffic=link_ms=gpu_upload_bytes=0;
}
// Codex: только наполнение существующих кэшей, без команд рисования и смены кадров.
void hmm_gpu_warm_sprite(uint32_t key){
    const unsigned cpu=key>>30;key&=0x3fffffffu;
    const auto &im=hmm_resource_get(key);
    const bool effect=(hmm_resource_flags(im)&2)!=0;
    // Codex: собственный CPU-эффект готовит только фазы с эффектом; перекрытое
    // войско требует всех фаз. Остальные кадры продолжают жить только в RAM_G.
    if(cpu){if(cpu==1&&!effect)return;HmmImagePins scope;im.image();}
    else if(!effect){const auto &r=hmm_resource_ft(key,false);uint32_t address;int upload;GpuBusy busy;
        if(hft_cache_get(&textures,UINT64_C(0x1000000000000000)|r.offset,r.bytes,&address,&upload)&&upload){
            hmm_remote_graphics(r.offset,address,r.bytes);gpu_upload_bytes+=r.bytes;
            // Codex: новые страницы ещё не показывались; предзагрузка не закрепляет их как экран.
            textures.entries[textures.last_entry].pins&=uint8_t(~(1u<<textures.build_slot));
        }
    }
}

#include "hmm_message_fade.inc"
