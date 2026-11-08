/* Codex: временное закрепление читаемых пикселей на время исходной операции изображения. */
#pragma once
#include <cstdint>
namespace fheroes2 {class Image;}
bool hmm_gpu_restore_allowed();
extern "C" void hmm_monster_parts_clear();
extern "C" void hmm_monster_parts_stats(unsigned *,unsigned *,unsigned *,int);
extern "C" void hmm_window_restore_failed();
struct HmmFtImage {uint32_t offset,bytes;uint16_t stride,height,cell,flags;};
struct HmmOpaqueRect {uint16_t x,y,w,h;};
const HmmOpaqueRect *hmm_resource_opaque(uint32_t);
// Клод: отпечаток маски кадра; одинаковый ненулевой отпечаток на том же месте закрывает прежний кадр целиком.
uint64_t hmm_resource_silhouette(uint32_t);
const HmmFtImage &hmm_resource_ft(uint32_t,bool);
// Клод: подпись поверх кадра (hmm_loading.cpp); nullptr — убрать.
void hmm_gpu_status(const fheroes2::Image *,int,int,bool);
void hmm_gpu_status_hold(const fheroes2::Image *,int,int);
bool hmm_world_take_fresh();
bool hmm_gpu_animation_limited();
bool hmm_gpu_message_begin(const fheroes2::Image * = nullptr);
bool hmm_gpu_message_begin(const fheroes2::Image *,bool);
void hmm_gpu_message_end();
bool hmm_gpu_message_owned();
void hmm_gpu_message_present(const uint8_t *,int,int);
void hmm_gpu_window_blits(int,int,int,int,int);
class HmmMessageLayer {
    bool active;
public:
    HmmMessageLayer():active(hmm_gpu_message_begin()){}
    explicit HmmMessageLayer(const fheroes2::Image &out):active(hmm_gpu_message_begin(&out)){}
    HmmMessageLayer(const fheroes2::Image &out,bool popup):active(hmm_gpu_message_begin(&out,popup)){}
    ~HmmMessageLayer(){if(active)hmm_gpu_message_end();}
    HmmMessageLayer(const HmmMessageLayer&)=delete;
    HmmMessageLayer &operator=(const HmmMessageLayer&)=delete;
};
const uint8_t *hmm_resource_palette();
uint32_t hmm_resource_pin(uint32_t);
void hmm_resource_restore_pin(uint32_t,uint32_t);
bool hmm_resource_id(const fheroes2::Image&,uint32_t&);
// Codex: вызывается только после успешного hmm_resource_id, источник — StoredSprite.
unsigned hmm_resource_flags(const fheroes2::Image&);
const fheroes2::Image &hmm_resource_get(uint32_t);
// Codex: подготовка постоянного рабочего набора в полной области на входе в бой.
bool hmm_resource_warm_allowed();
void hmm_gpu_warm_sprite(uint32_t);
// Codex: CPU читает нужный участок большого фона без полного блока пикселей и маски.
bool hmm_resource_region(uint32_t,int,int,int,int,fheroes2::Image&);
bool hmm_gpu_offer(const fheroes2::Image&,int,int,fheroes2::Image&,int,int,int,int,bool,bool,const uint8_t* = nullptr);
void hmm_gpu_pixels(const fheroes2::Image&,uint8_t*,bool);
void hmm_gpu_drop(const fheroes2::Image&);
void hmm_gpu_move(const fheroes2::Image&,const fheroes2::Image&);
bool hmm_gpu_fill(fheroes2::Image&,int,int,int,int,uint8_t,uint8_t=0);
void hmm_gpu_touch(fheroes2::Image&,int,int,int,int);
// Клод: цель для слоёв FT812 вместо точек CPU, участок чистой работы CPU (hmm_canvas.inc). ApplyTransform — hmm_gpu_fill с маской 2–9.
bool hmm_gpu_layer_target(const fheroes2::Image&);
bool hmm_gpu_tracked(const fheroes2::Image&);
bool hmm_gpu_cpu_begin();
// Клод: затемнение всего экрана слоем FT812 (fadeDisplay, ui_tool.cpp): готовность для участка и альфа чёрного поверх экрана (0 — нет).
bool hmm_gpu_fade_ready(int x,int y,int w,int h);
void hmm_gpu_fade(uint8_t black);
void hmm_gpu_cpu_end(bool);
// Клод: AlphaBlit спрайта ресурса с альфой 1…254 — слой FT812 с COLOR_A (hmm_canvas.inc); false — прежний путь CPU.
bool hmm_gpu_alpha(const fheroes2::Image&,int,int,fheroes2::Image&,int,int,int,int,uint8_t,bool);
// Клод: контур отряда в бою слоями FT812 (hmm_canvas.inc); false — прежний путь CPU.
bool hmm_gpu_contour(const fheroes2::Image&,fheroes2::Image&,int,int,bool,uint8_t);
// Клод: ролики (hmm_clip.cpp). Буфер кадра в RAM_G, декодирование E6+VIDEOFRAME, слой кадра на экране.
#define HMM_CLIP_SLOTS 4
extern "C" uint32_t hmm_gpu_clip_buffer(unsigned,unsigned,unsigned,unsigned);
extern "C" void hmm_gpu_clip_decode(uint32_t,uint32_t,uint32_t);
extern "C" void hmm_gpu_clip_end();
void hmm_gpu_clip_draw(fheroes2::Image&,int,int,int,int,unsigned);
void hmm_gpu_clip_show(fheroes2::Image&,int,int,unsigned,uint32_t,unsigned,unsigned);
void hmm_gpu_enter();
void hmm_gpu_leave();
bool hmm_gpu_active();
void hmm_gpu_raw();
// Codex: срок жизни уже показанного участка; запись поверх него отменяет повторное использование.
uint32_t hmm_gpu_region_begin(const void*,const fheroes2::Image&,int,int,int,int);
uint32_t hmm_gpu_region_end(bool);
// Клод: программный курсор на время вывода кадра — последним слоем DL, без записи в Display;
// nullptr — курсора нет (Display::render, патч screen.cpp в patch_stage1_canvas.py).
void hmm_gpu_cursor(const fheroes2::Image*,int,int);
// Клод: окно карты (GameArea::Redraw): прямоугольник на экране и левый верхний угол в пикселях карты.
void hmm_gpu_map_view(fheroes2::Image&,int,int,int,int,int,int,const void*,int,bool,int,int);
// Клод: конец перерисовки окна карты — режим карты (hmm_canvas.inc) больше не принимает слои.
void hmm_gpu_map_end();
// Codex: точечная перезапись кольца карты; false сохраняет исходный путь порта.
bool hmm_gpu_ring_start(const void *owner,int flags,unsigned phase);
struct HmmSubjectPart {uint32_t resource;int x,y,w,h,sx,sy,step;bool opaque;};
bool hmm_gpu_ring_subject(const HmmSubjectPart *old_parts,unsigned old_count,const HmmSubjectPart *new_parts,unsigned new_count);
bool hmm_gpu_ring_need(int x,int y,int radius=0);
void hmm_gpu_ring_skip();
void hmm_gpu_ring_dirty(int x,int y,int radius);
unsigned hmm_gpu_ring_generation();
// Клод: область действия режима карты на время GameArea::Redraw (патч prepare_stage1.py); выход —
// по любому return.
class HmmMapScope {
public:
    HmmMapScope(fheroes2::Image &dst,bool on,int x,int y,int w,int h,int ox,int oy,const void *owner,int flags,bool stable,int tile_x,int tile_y):active(on){if(on)hmm_gpu_map_view(dst,x,y,w,h,ox,oy,owner,flags,stable,tile_x,tile_y);}
    ~HmmMapScope(){if(active)hmm_gpu_map_end();}
    HmmMapScope(const HmmMapScope&)=delete;
private:
    bool active;
};
// Клод: отдать кэши PSRAM перед чтением мира (Game::Load).
void hmm_release_for_world();
void hmm_resource_menu_cache(bool);
// Клод: экран города (Castle::OpenDialog, патч patch_fast_frame.py): большие картинки держатся раскодированными до выхода.
void hmm_resource_town_cache(bool);
class HmmTownCache {
public:
    HmmTownCache(){hmm_resource_town_cache(true);}
    ~HmmTownCache(){hmm_resource_town_cache(false);}
    HmmTownCache(const HmmTownCache&)=delete;
};
void hmm_cache_enter();
void hmm_cache_leave();
class HmmImagePins {
public:
    HmmImagePins(){
#ifdef HMM2_PACKED
        hmm_cache_enter();
        hmm_gpu_enter();
#endif
    }
    ~HmmImagePins(){
#ifdef HMM2_PACKED
        hmm_cache_leave();
        hmm_gpu_leave();
#endif
    }
    HmmImagePins(const HmmImagePins&)=delete;
};

// Codex: границы фрагментов отмечают исходные функции, не меняя выбор спрайтов и порядок.
int hmm_gpu_map_kind(int);
class HmmMapKind {
    int previous;
public:
    explicit HmmMapKind(int kind):previous(hmm_gpu_map_kind(kind)){}
    ~HmmMapKind(){hmm_gpu_map_kind(previous);}
};

// Codex: уже выбранный оригинальным GameArea спрайт — прямо в фрагмент карты.
bool hmm_gpu_map_sprite(const fheroes2::Image&,int,int,fheroes2::Image&,int,int,int,int,bool,bool,uint8_t=255);

// Codex: на плотной карте сохраняется ограничение анимаций сборки 91: вода и окрестность фокуса.

// Codex: разрешение только для тика MAPS_DELAY без другого запроса перерисовки карты.
bool hmm_gpu_map_animation(bool);
bool hmm_gpu_map_animation_reuse();
class HmmMapAnimation {
    bool previous;
public:
    explicit HmmMapAnimation(bool enabled):previous(hmm_gpu_map_animation(enabled)){}
    ~HmmMapAnimation(){hmm_gpu_map_animation(previous);}
};
int hmm_gpu_map_static_begin();
void hmm_gpu_map_static_end(int);
class HmmMapStatic {
    int slot;
public:
    HmmMapStatic():slot(hmm_gpu_map_static_begin()){}
    ~HmmMapStatic(){hmm_gpu_map_static_end(slot);}
    bool reused()const{return slot<0;}
};
