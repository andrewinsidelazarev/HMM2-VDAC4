#ifdef HMM2_HOST
// Клод: стек вызовов для журналов крупных растров на ПК (kernel32).
extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long,unsigned long,void**,unsigned long*);
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char*);
#endif
/* Codex: происхождение изображения — список исходных слоёв. Copy и ImageRestorer
 * копируют геометрию и ссылки, не заставляя ESP повторно распаковывать спрайты.
 * Пиксели материализуются только при прямом доступе или неподдержанном эффекте. */
extern "C" void hmm_memory_plan();
extern "C" void hmm_ring_world_clear();
namespace {
#ifdef HMM2_HOST
#define HMM_SIZE_RENDER
#define HMM_FAST_RENDER
#else
#define HMM_SIZE_RENDER __attribute__((optimize("Os"),noinline))
#define HMM_FAST_RENDER __attribute__((optimize("O2"),noinline))
#endif
using Image=fheroes2::Image;
struct Rect {int x=0,y=0,w=0,h=0;};
Rect intersect(Rect a,Rect b){int r=std::min(a.x+a.w,b.x+b.w),d=std::min(a.y+a.h,b.y+b.h);a.x=std::max(a.x,b.x);a.y=std::max(a.y,b.y);a.w=std::max(0,r-a.x);a.h=std::max(0,d-a.y);return a;}
#include "hmm_sequence.inc"
struct TileFrame;
struct Bitmap {uint64_t id;int w,h;bool single;std::vector<uint8_t> pixels;std::shared_ptr<TileFrame> frame;Rect tile_rect;unsigned paint_begin=0,paint_count=0;Bitmap *next=nullptr;};
struct Draw {
    Rect r;int sx=0,sy=0,step=1;uint32_t resource=0;unsigned mapping=0;
    // Клод: cpu — снимок записи CPU (flush); следующий соседний участок сливается с ним.
    // layer — заместитель окна карты в списке экрана: его слои лежат в map_draws (режим карты).
    // Клод: color у слоя без fill — альфа AlphaBlit (1…254), 0 — обычный непрозрачный слой. Отдельное поле
    // увеличило бы Draw на 4 байта: на GateHell в списках ~14 тыс. слоёв, это весь запас PSRAM.
    bool raw=false,opaque=false,fill=false,repeat=false,cpu=false,layer=false;uint8_t color=0,mask=0;
    std::shared_ptr<Bitmap> bitmap;
};
struct TileFrame {Sequence<Draw> draws;const Sequence<Draw> *map_source=nullptr;Sequence<uint16_t> layers;Rect roi;TileFrame *next=nullptr;};
// Codex: снимки владеют кадром до последнего ImageRestorer. Только после последней
// ссылки буферы переходят в свободный список; следующий кадр забирает их обратно.
TileFrame *free_frames;Bitmap *free_bitmaps[2];
// Codex: рабочие массивы клеток нужны после загрузки мира. Один блок на мир,
// повторное использование в каждом кадре; пик загрузки не платит за эти 4036 байт.
struct TileWorkspace {std::array<uint32_t,21*16> counts;std::array<uint32_t,21*16+1> offsets;std::array<int,21*16> cell_slot;};
std::unique_ptr<TileWorkspace> tile_workspace;
std::vector<std::vector<uint8_t>> tile_pixel_buffers;
std::shared_ptr<TileFrame> take_frame(){
    auto *p=free_frames;if(p)free_frames=p->next;else p=new TileFrame;
    return std::shared_ptr<TileFrame>(p,[](TileFrame *v){v->draws.clear();v->layers.clear();v->map_source=nullptr;v->next=free_frames;free_frames=v;});
}
HMM_RENDER_NOINLINE std::shared_ptr<Bitmap> hold_bitmap(Bitmap *p){
    // Codex: свободный снимок CPU сохраняет байты и id. Идентичный следующий
    // снимок использует ту же текстуру RAM_G, без новой копии и вытеснения атласов.
    return std::shared_ptr<Bitmap>(p,[](Bitmap *v){auto &head=free_bitmaps[!v->frame];if(v->frame)v->pixels.clear();v->frame.reset();v->next=head;head=v;});
}
std::shared_ptr<Bitmap> take_bitmap(size_t bytes=0){
    auto **slot=&free_bitmaps[bytes!=0];
    // Codex: снимки CPU не смешиваются с клетками. Берём наименьший подходящий
    // буфер, иначе самый большой свободный: чередование размеров не раздувает пул.
    if(bytes)for(auto **at=slot;*at;at=&(*at)->next){
        const size_t a=(*at)->pixels.capacity(),b=(*slot)->pixels.capacity();
        if((a>=bytes&&b<bytes)||(a>=bytes&&a<b)||(a<bytes&&b<bytes&&a>b))slot=at;
    }
    auto *p=*slot;if(p)*slot=p->next;else p=new Bitmap;
    p->paint_begin=p->paint_count=0;
    return hold_bitmap(p);
}
void release_frame_storage(){
    std::vector<std::vector<uint8_t>>().swap(tile_pixel_buffers);
    for(auto &head:free_bitmaps)while(auto *p=head){head=p->next;delete p;}
    while(auto *p=free_frames){free_frames=p->next;delete p;}
    Sequence<Draw>::release_unused();Sequence<uint16_t>::release_unused();
}
void bitmap_materialize(Bitmap &);
void bitmap_release_pixels(Bitmap &b){if(b.frame&&!b.pixels.empty()){b.pixels.clear();tile_pixel_buffers.push_back(std::move(b.pixels));}}
unsigned canvas_revision=1;
// Клод: touched — номер кадра последнего обращения процессора к точкам (canvases_release_idle).
struct Canvas {unsigned revision=0;Sequence<Draw> draws;Rect pending;bool writing=false,prepared=false;uint8_t *pixels=nullptr;size_t compact_at=1024;unsigned touched=0;};
void changed(Canvas &c){c.revision=++canvas_revision;c.prepared=false;}
// Клод: прошлый кадр клеток плотной карты (cache_map_tiles в hmm_gpu.cpp): подпись слоёв, готовый список экрана и его ревизия.
uint64_t tiles_signature;Sequence<Draw> tiles_out;unsigned tiles_revision;
void tiles_forget(){tiles_signature=0;Sequence<Draw>().swap(tiles_out);}
unsigned depth;bool ready,materializing;
// Codex: статические Image разных единиц трансляции могут пережить реестр слоёв.
// После уничтожения реестра их деструкторы уже не обращаются к его дереву.
const Image *message_target;
const Image *canvas_key(const Image *im){return message_target&&im==&fheroes2::Display::instance()?message_target:im;}
struct CanvasRegistry:std::map<const Image*,Canvas>{
    using Base=std::map<const Image*,Canvas>;
    auto erase(Base::iterator it){return Base::erase(it);}
    auto find(const Image *im){return Base::find(canvas_key(im));}
    Canvas &operator[](const Image *im){return Base::operator[](canvas_key(im));}
    auto emplace(const Image *im,Canvas &&c){return Base::emplace(canvas_key(im),std::move(c));}
    size_t erase(const Image *im){return Base::erase(canvas_key(im));}
    ~CanvasRegistry(){ready=false;}
};
unsigned canvas_frame; // Клод: счётчик кадров для canvases_release_idle
CanvasRegistry canvases;
struct MessageSurface:Image {MessageSurface(){_disableTransformLayer();resize(640,480);}};
std::vector<std::unique_ptr<MessageSurface>> message_layers;
Canvas message_geometry;
unsigned message_geometry_revision,message_base_revision;size_t message_geometry_layers;
Canvas &frame_canvas();
// Клод: изображения, в которые сейчас пишет процессор (Canvas::writing). hmm_gpu_leave сбрасывает только их;
// прежде он на выходе из каждой операции с изображением обходил все списки слоёв (сотни узлов std::map).
std::vector<const Image*> writers;
// Клод: режим карты. GameArea::Redraw перерисовывает всё окно карты (около 420 Blit/Copy на кадр).
// На время перерисовки эти слои идут прямо в map_draws, без cover и без поиска списков слоёв:
// в списке экрана окно карты — один заместитель (Draw::layer). Всё окно заменяется следующей
// перерисовкой целиком. Любая другая операция, читающая экран (сохранение фона, CPU-доступ к точкам,
// запись не из Redraw во время перерисовки), сначала разворачивает заместитель в обычные слои
// (map_materialize) — дальше всё идёт прежним путём, картинка та же.
/* Клод: разбивка времени кадра для журнала платы (строка HMM2 TIME раз в 100 кадров): такты процессора по участкам, включительно
 * (вложенные участки входят и во внешний). Стенд QEMU считает этот код в 2–3 раза быстрее платы (без задержек PSRAM), поэтому
 * мерить нужно на плате. Счётчик тактов Xtensa (CCOUNT) — без изменений ядра; на ПК — часы steady_clock. */
enum {P_OFFER,P_COVER,P_FILL,P_PIXELS,P_CANVAS,P_FRAME,P_PARTS,P_EMIT,P_REHANDLE,P_MERGE,P_CELLS,P_OCCLUDE,P_PUBLISH,P_RECORD,P_TILEPREP,P_BODY_STORE,P_BACKGROUND,P_LIVE,P_SPRITE_BUILD,P_SPRITE_REPLAY,P_COUNT};
#ifdef HMM2_HOST
inline uint32_t prof_now(){return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()*6/25);}
#else
inline uint32_t prof_now(){uint32_t c;__asm__ __volatile__("rsr.ccount %0":"=r"(c));return c;}
#endif
uint64_t prof_cycles[P_COUNT];
// Codex: счётчики подтверждают, какие быстрые пути действительно работали в замере.
unsigned stat_merge_full,stat_merge_reuse,stat_handles_full,stat_handles_reuse;
struct Prof {unsigned id;uint32_t start;explicit Prof(unsigned i):id(i),start(prof_now()){}~Prof(){prof_cycles[id]+=prof_now()-start;}
    Prof(const Prof&)=delete;Prof &operator=(const Prof&)=delete;};
Sequence<Draw> map_draws;const Image *map_target;bool map_active;
// Codex: опыт кольца включается отдельно до завершения пиксельной проверки.
bool ring_enabled(){
#ifdef HMM2_HOST
    static const bool enabled=std::getenv("HMM2_RING_TILES")!=nullptr;return enabled;
#else
    return true;
#endif
}
bool ring_optimized(){
#ifdef HMM2_HOST
    static const bool enabled=std::getenv("HMM2_RING_REFERENCE")==nullptr;return enabled;
#else
    return true;
#endif
}
// Codex: как RF_Frame/Render_MapAnimCmd Z80: статические фрагменты, динамика и туман
// имеют отдельные сроки жизни, но остаются на местах исходного GameArea::Redraw.
struct MapPart {
    size_t begin=0,count=0;int kind=0,tx=0,ty=0;bool dirty=true,fill=false;unsigned palette=0;
    // Клод: подпись склеек кусков этой части (map_merge_plan): изменилась — часть собирается заново.
    uint64_t merge_hash=0;
    std::vector<uint32_t> commands;
    std::vector<uint32_t> textures;
};
std::vector<MapPart> map_parts;
size_t map_position,map_part_index,map_part_count;int map_kind;Rect map_bounds;int map_origin_x,map_origin_y,map_view_x,map_view_y,map_shift_x,map_shift_y;
#include "hmm_ring_state.inc"
/* Codex: точный список изменившейся геометрии вместо хэша всех прямоугольников.
 * Общий сдвиг камеры не меняет пересечения. При большом изменении нужен полный
 * план; при движении героя проверяются лишь пары с изменившимися прямоугольниками. */
std::array<uint16_t,64> merge_changed;unsigned merge_changed_count;bool merge_geometry_reset;
// Codex: пропуск статических участков исходного Redraw только при чистом тике анимации.
// Сохраняются позиции в исходной последовательности, без перестановки слоёв.
struct MapSpan {uint16_t begin,part,count,end,end_part,end_count,children,flags;};
std::vector<MapSpan> map_spans;size_t map_span_position;std::vector<unsigned> map_span_stack;
bool map_animation_only,map_static_allowed,map_plan_valid,map_plan_stable;
const void *map_plan_owner;int map_plan_flags,map_plan_tile_x,map_plan_tile_y;Rect map_window;
/* Клод: экран целиком закрыл окно карты непрозрачным слоем (кадр боя, город, полноэкранное окно) после её последнего показа.
 * Тогда кадр не карта, и короткий путь плотной карты (клетки при >800 слоях) к нему не относится: на плате (сборка 81, Alteris)
 * бой после плотной карты шёл клетками процессора со спрайтами с SD — 179 КБ SD и ~450 мс на кадр. Снимает hmm_gpu_map_view. */
bool map_covered;extern bool map_released;
void note_map_cover(const Image &out,const Rect &r){
    if(message_target)return;
    if(&out==&fheroes2::Display::instance()&&map_window.w&&map_window.h&&r.x<=map_window.x&&r.y<=map_window.y&&
       r.x+r.w>=map_window.x+map_window.w&&r.y+r.h>=map_window.y+map_window.h)map_covered=true;
}
const void *region_owner;const Image *region_target;Rect region_rect;uint32_t region_revision;bool region_prepared;
void region_bump(){if(!++region_revision)++region_revision;}
void region_write(const Image &im,Rect r,bool clip_map=true){
    if(&im!=region_target)return;
    if(clip_map&&map_active&&&im==map_target)r=intersect(r,map_window);
    const auto v=intersect(r,region_rect);if(v.w&&v.h){region_bump();region_prepared=false;}
}
void map_cache_clear();
void map_tiles_clear();
void map_clips_clear();
void map_cache_release(MapPart &);
bool same_draw(const Draw &a,const Draw &b){
    // Codex: только инициализированные поля без padding и shared_ptr; сдвиг проверяется отдельно.
    static_assert(offsetof(Draw,mask)==43 && sizeof(Rect)==16);
    return a.r.x+map_shift_x==b.r.x&&a.r.y+map_shift_y==b.r.y&&
           std::memcmp(&a.r.w,&b.r.w,36)==0&&a.bitmap==b.bitmap;
}
void map_part_finish(){
    if(map_part_index>=map_parts.size())return;
    auto &p=map_parts[map_part_index];
    if(p.count!=map_part_count){p.count=map_part_count;p.dirty=true;}
}
void map_record(Draw &&d){
    Prof prof(P_RECORD);
    // Codex: у ресурсного слоя mask не используется растеризатором: метка динамики
    // сохраняет границу запекаемого фона без увеличения Draw и расхода PSRAM.
    if(!d.fill)d.mask=map_kind==4?2:map_kind==5?3:map_kind==1;
    if(ring_capture(d))return;
    if(!d.fill&&d.mask>=2)d.mask=d.mask==2;
    // Codex: вне кольца остаётся редкий прямой список. Старые части/склейки
    // нужны лишь оракулу ПК и не дублируют новую архитектуру в ELF.
    if(ring_enabled()){
        if(map_position==map_draws.size())map_draws.push_back(std::move(d));else map_draws[map_position]=std::move(d);
        ++map_position;map_bounds=map_window;return;
    }
    if(!map_position)map_bounds=d.r;
    else {int right=std::max(map_bounds.x+map_bounds.w,d.r.x+d.r.w),bottom=std::max(map_bounds.y+map_bounds.h,d.r.y+d.r.h);
        map_bounds.x=std::min(map_bounds.x,d.r.x);map_bounds.y=std::min(map_bounds.y,d.r.y);map_bounds.w=right-map_bounds.x;map_bounds.h=bottom-map_bounds.y;}
    // Codex: изменение героя не пересобирает длинный общий проход теней/монстров.
    // Граница из 64 исходных слоёв сохраняет порядок и не растеризует ничего на CPU.
    if(map_part_index<map_parts.size()&&map_part_count&&(map_parts[map_part_index].kind!=map_kind||(map_kind==1&&map_part_count>=64))){
        map_part_finish();++map_part_index;map_part_count=0;
    }
    if(map_part_index==map_parts.size())map_parts.emplace_back();
    auto &p=map_parts[map_part_index];
    if(!map_part_count){
        if(p.begin!=map_position||p.kind!=map_kind)p.dirty=true;
        p.begin=map_position;p.kind=map_kind;p.fill=false;p.tx+=map_shift_x;p.ty+=map_shift_y;
    }
    if(d.fill){p.fill=true;if(map_shift_x||map_shift_y)p.dirty=true;}
    if(map_position==map_draws.size()){map_draws.push_back(std::move(d));p.dirty=true;merge_geometry_reset=true;}
    else {
        const auto &r=map_draws[map_position].r;
        if(r.x+map_shift_x!=d.r.x||r.y+map_shift_y!=d.r.y||r.w!=d.r.w||r.h!=d.r.h){
            if(merge_changed_count<merge_changed.size()&&map_position<65536)merge_changed[merge_changed_count++]=uint16_t(map_position);
            else merge_geometry_reset=true;
        }
        if(!same_draw(map_draws[map_position],d)){map_draws[map_position]=std::move(d);p.dirty=true;}
        else if(map_shift_x||map_shift_y)map_draws[map_position].r=d.r;
    }
    ++map_position;++map_part_count;
}
/* Клод: оригинал 1.1.17 режет крупные спрайты карты — монстров, их тени, тени героя и лодки — на куски по клеткам 32×32
 * (renderImagesOnTiles), чтобы соблюсти порядок рисования. Каждый кусок — отдельный слой FT812 со своими BITMAP_SIZE и
 * TRANSFORM_C/F, 4–6 слов. На Alteris 788 из 1228 слоёв карты были кусками монстров и их теней — 3061 слово из 4422 при
 * пределе 2048, и карта уходила в клетки, которые растеризует процессор: на плате 100+ мс сборки кадра, мегабайты с SD и рост
 * памяти до отказа (сборка 93, 04.11).
 * План склейки: обрезанные куски одного спрайта (тот же ресурс, таблица, отражение, вид части и одно начало источника), точно
 * составляющие прямоугольник, выводятся одним слоем — на месте первого или последнего куска. Группы принимаются по очереди:
 * группа принимается, только если ни одна пара слоёв, чей взаимный порядок при этом меняется, не пересекается (остальные — как
 * уже решено); два спрайта, чей порядок в разных клетках разный (основа и анимация монстра), склеиваются хотя бы один. Куски
 * одного спрайта не перекрываются — картинка та же точка в точку.
 * Сам map_draws не меняется: клетки, растр CPU и статические участки работают с исходными кусками; план действует только
 * при сборке частей (prepare_map). Память: постоянно — map_merge_at (int16 на слой) и склеенные слои; кандидаты — временный
 * массив только на время расчёта (первая версия держала ~150 КиБ рабочих массивов, и ход по Alteris на стенде упёрся в PSRAM). */
std::vector<int16_t> map_merge_at;std::vector<Draw> map_merged;size_t map_merge_skipped;
/* Codex: группы принятого плана сохраняют якоря и куски. Общий сдвиг камеры
 * переносит прямоугольники; смена кадров спрайтов и движение проверяются точно.
 * У сборки 98 полный пересчёт на плате стоил 20–28 мс на плотной карте. */
struct MergeGroup {uint16_t anchor,first,count;Rect box;};
std::vector<MergeGroup> map_merge_groups;std::vector<uint16_t> map_merge_members;
// Codex: конечная позиция исходного куска нужна только для проверки переставленных пар.
std::vector<uint16_t> map_merge_anchor;
HMM_SIZE_RENDER void map_merge_plan(){
    Prof prof(P_MERGE);
    ++stat_merge_full;
    const size_t n=map_draws.size();
    map_merge_at.assign(n,-1);map_merged.clear();map_merge_skipped=0;map_merge_groups.clear();map_merge_members.clear();
    if(n<2||n>30000)return;
    struct Piece {uint32_t resource;unsigned mapping;uint32_t flags;int16_t ox,oy;int16_t index;};
    std::vector<Piece> pieces;
    // Кандидаты — обрезанные куски ресурсов (не заливки, не туман, не снимки). Клод: два прохода — сначала счёт, затем массив
    // точного размера: рост удвоением просил на ходу по Alteris 20 480 байт при крупнейшем свободном куске 14 080 (стенд, сборка 95).
    for(unsigned pass=0;pass<2;++pass){size_t part=0,count=0;
     for(size_t j=0;j<n;++j){
        while(part+1<map_parts.size()&&j>=map_parts[part].begin+map_parts[part].count)++part;
        const auto &d=map_draws[j];const int kind=part<map_parts.size()?map_parts[part].kind:0;
        if(d.fill||d.bitmap||d.repeat||d.layer||!d.resource||d.r.w<=0||d.r.h<=0||kind==2)continue;
        const auto &im=hmm_resource_get(d.resource);
        if(!d.sx&&!d.sy&&d.r.w==im.width()&&d.r.h==im.height())continue;
        if(!pass){++count;continue;}
        const int ox=d.step<0?d.r.x+d.sx:d.r.x-d.sx,oy=d.r.y-d.sy;
        const uint32_t flags=uint32_t(d.raw)|(uint32_t(d.opaque)<<1)|(uint32_t(d.cpu)<<2)|(uint32_t(d.step<0)<<3)|(uint32_t(d.color)<<8)|(uint32_t(d.mask)<<16)|(uint32_t(kind)<<24);
        pieces.push_back({d.resource,d.mapping,flags,int16_t(ox),int16_t(oy),int16_t(j)});
     }
     if(!pass){if(count<2)return;pieces.reserve(count);}}
    if(pieces.size()<2)return;
    // Группы — подряд идущие после сортировки по ключу и исходному порядку.
    std::sort(pieces.begin(),pieces.end(),[](const Piece &a,const Piece &b){
        return a.resource!=b.resource?a.resource<b.resource:a.mapping!=b.mapping?a.mapping<b.mapping:a.flags!=b.flags?a.flags<b.flags:a.ox!=b.ox?a.ox<b.ox:a.oy!=b.oy?a.oy<b.oy:a.index<b.index;});
    auto same=[](const Piece &a,const Piece &b){return a.resource==b.resource&&a.mapping==b.mapping&&a.flags==b.flags&&a.ox==b.ox&&a.oy==b.oy;};
    // Место вывода каждого слоя: своё, у принятой группы — место её якоря (anchor_of: только перенесённые куски).
    std::vector<int16_t> &anchor_of=map_merge_at;   // временно: -1 — на своём месте, иначе индекс якоря
    /* Клод: сетка 32×32 по слоям карты — переносимый кусок могут перекрыть только слои рядом на экране. Прежде проверялись все слои
     * между старым и новым местом куска и все уже перенесённые куски: на ходу по Alteris план стоил дороже всей сборки кадра
     * (стенд, сборка 97). Условие то же: перестановка плоха, если слой перекрывает кусок и их порядок меняется; слои, не задевающие
     * кусок, на результат не влияют, поэтому перебор только соседей по сетке даёт тот же план. */
    const Rect grid=map_bounds;const int gw=std::max(1,(grid.w+31)/32),gh=std::max(1,(grid.h+31)/32);
    auto cells_of=[&](const Rect &r,int &x0,int &y0,int &x1,int &y1){
        auto cell=[](int v,int limit){return v<0?0:std::min(limit-1,v/32);};
        x0=cell(r.x-grid.x,gw);y0=cell(r.y-grid.y,gh);x1=cell(r.x+r.w-1-grid.x,gw);y1=cell(r.y+r.h-1-grid.y,gh);};
    std::vector<uint16_t> cell_start(size_t(gw)*size_t(gh)+1,0),cell_list;
    for(unsigned pass=0;pass<2;++pass){
        std::vector<uint16_t> fill;if(pass){cell_list.resize(cell_start.back());fill.assign(cell_start.begin(),cell_start.end()-1);}
        for(size_t j=0;j<n;++j){const auto &r=map_draws[j].r;if(r.w<=0||r.h<=0)continue;int x0,y0,x1,y1;cells_of(r,x0,y0,x1,y1);
            for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx){const size_t c=size_t(cy)*size_t(gw)+size_t(cx);
                if(pass)cell_list[fill[c]++]=uint16_t(j);else ++cell_start[c+1];}}
        if(!pass)for(size_t c=0;c+1<cell_start.size();++c)cell_start[c+1]+=cell_start[c];
    }
    // Метка кусков текущей группы (их взаимный порядок меняется, но они не перекрываются — проверено отдельно).
    std::vector<uint16_t> stamp(n,0);uint16_t attempt_id=0;
    auto final_of=[&](size_t k){return anchor_of[k]>=0?size_t(anchor_of[k]):k;};
    auto before=[&](size_t a,size_t b){const size_t fa=final_of(a),fb=final_of(b);return fa!=fb?fa<fb:a<b;};
    auto overlap=[&](size_t a,size_t b){const auto &p=map_draws[a].r,&q=map_draws[b].r;return p.x<q.x+q.w&&q.x<p.x+p.w&&p.y<q.y+q.h&&q.y<p.y+p.h;};
    struct Accepted {uint16_t first,count;int16_t anchor;Rect box;};std::vector<Accepted> accepted;
    // Попытка группы pieces[b..e): точный прямоугольник, место первого или последнего куска, проверка изменившихся пар.
    auto attempt=[&](size_t b,size_t e)->bool{
        const size_t count=e-b;
        if(count<2||count>8||pieces[e-1].index-pieces[b].index>2048)return false;
        Rect box=map_draws[size_t(pieces[b].index)].r;int64_t area=0;
        for(size_t a=b;a<e;++a){const auto &r=map_draws[size_t(pieces[a].index)].r;area+=int64_t(r.w)*r.h;
            const int right=std::max(box.x+box.w,r.x+r.w),bottom=std::max(box.y+box.h,r.y+r.h);
            box.x=std::min(box.x,r.x);box.y=std::min(box.y,r.y);box.w=right-box.x;box.h=bottom-box.y;
            for(size_t c=b;c<a;++c)if(overlap(size_t(pieces[a].index),size_t(pieces[c].index)))return false;}
        if(area!=int64_t(box.w)*box.h)return false;
        if(!++attempt_id){std::fill(stamp.begin(),stamp.end(),uint16_t(0));attempt_id=1;}
        for(size_t a=b;a<e;++a)stamp[size_t(pieces[a].index)]=attempt_id;
        for(unsigned option=0;option<2;++option){
            const int16_t target=option?pieces[e-1].index:pieces[b].index;
            for(size_t a=b;a<e;++a)if(pieces[a].index!=target)anchor_of[size_t(pieces[a].index)]=target;
            bool bad=false;
            for(size_t a=b;a<e&&!bad;++a){
                const size_t m=size_t(pieces[a].index);if(m==size_t(target))continue;
                int x0,y0,x1,y1;cells_of(map_draws[m].r,x0,y0,x1,y1);
                for(int cy=y0;cy<=y1&&!bad;++cy)for(int cx=x0;cx<=x1&&!bad;++cx){const size_t c=size_t(cy)*size_t(gw)+size_t(cx);
                    for(unsigned q=cell_start[c];q<cell_start[c+1];++q){const size_t k=cell_list[q];
                        if(k==m||stamp[k]==attempt_id)continue;
                        if((k<m)!=before(k,m)&&overlap(k,m)){bad=true;break;}}}
            }
            if(!bad){accepted.push_back({uint16_t(b),uint16_t(count),target,box});return true;}
            for(size_t a=b;a<e;++a)anchor_of[size_t(pieces[a].index)]=-1;
        }
        return false;
    };
    // Группы пробуются в порядке их первых кусков; не вышедшие целиком — затем кучками: куски, близкие по порядку (не дальше
    // 128 слоёв друг от друга), например тело монстра отдельно от нижней полоски в клетке ниже.
    std::vector<std::pair<uint16_t,uint16_t>> runs;
    for(size_t b=0;b<pieces.size();){size_t e=b+1;while(e<pieces.size()&&same(pieces[b],pieces[e]))++e;if(e-b>=2)runs.emplace_back(uint16_t(b),uint16_t(e));b=e;}
    std::sort(runs.begin(),runs.end(),[&](const auto &x,const auto &y){return pieces[x.first].index<pieces[y.first].index;});
    std::vector<uint8_t> done(runs.size(),0);
    for(size_t r=0;r<runs.size();++r)done[r]=attempt(runs[r].first,runs[r].second);
    for(size_t r=0;r<runs.size();++r){
        const size_t b=runs[r].first,e=runs[r].second;if(done[r]||e-b<3)continue;
        size_t c=b;for(size_t a=b+1;a<=e;++a)if(a==e||pieces[a].index-pieces[a-1].index>128){if(c!=b||a!=e)attempt(c,a);c=a;}
    }
    // Codex: якорь рисует склеенный слой, остальные куски пропускаются. Группы сохраняются для map_merge_reuse.
    for(auto &v:map_merge_at)v=-1;
    map_merged.reserve(accepted.size());map_merge_groups.reserve(accepted.size());
    for(const auto &g:accepted){map_merge_skipped+=g.count-1u;
        map_merge_groups.push_back({uint16_t(g.anchor),uint16_t(map_merge_members.size()),uint16_t(g.count),g.box});
        for(size_t a=g.first;a<size_t(g.first)+g.count;++a)map_merge_members.push_back(uint16_t(pieces[a].index));
        const auto &p=pieces[g.first];Draw m=map_draws[size_t(g.anchor)];
        m.r=g.box;m.sx=(p.flags&8u)?p.ox-g.box.x:g.box.x-p.ox;m.sy=g.box.y-p.oy;
        map_merge_at[size_t(g.anchor)]=int16_t(map_merged.size());map_merged.push_back(std::move(m));
        for(size_t a=g.first;a<size_t(g.first)+g.count;++a)if(pieces[a].index!=g.anchor)map_merge_at[size_t(pieces[a].index)]=-2;
    }
    map_merge_anchor.resize(n);for(size_t i=0;i<n;++i)map_merge_anchor[i]=uint16_t(i);
    for(const auto &g:map_merge_groups)for(unsigned a=0;a<g.count;++a)map_merge_anchor[map_merge_members[g.first+a]]=g.anchor;
}
// Клод: подпись склеек части изменилась — часть собирается заново (вызывается после map_merge_plan).
void map_merge_mark(){
    const size_t n=map_draws.size();
    for(auto &p:map_parts){
        uint64_t h=1469598103934665603ull;
        for(size_t i=p.begin;i<p.begin+p.count&&i<n&&i<map_merge_at.size();++i){const int v=map_merge_at[i];if(v==-1)continue;
            uint64_t vals[5]={uint64_t(i),uint64_t(uint32_t(v)),0,0,0};
            // Codex: кэш части уже несёт общий VERTEX_TRANSLATE; сдвиг камеры
            // не должен заставлять заново компилировать её исходные команды.
            if(v>=0){const auto &m=map_merged[size_t(v)];vals[2]=(uint64_t(uint32_t(m.r.x+map_origin_x-map_view_x))<<32)|uint32_t(m.r.y+map_origin_y-map_view_y);vals[3]=(uint64_t(uint32_t(m.r.w))<<32)|uint32_t(m.r.h);vals[4]=(uint64_t(uint32_t(m.sx))<<32)|uint32_t(m.sy);}
            for(auto x:vals){h^=x;h*=1099511628211ull;}}
        if(h!=p.merge_hash){p.merge_hash=h;p.dirty=true;}
    }
}
/* Клод: план нужен, только пока список FT812 карты переполняется (map_merge_wanted ставит hmm_gpu_frame вместо ухода в клетки,
 * снимает — когда карту закрыл экран боя или города). Без нужды он не считается и не держит памяти: на GateHell у сборки 93
 * запас PSRAM был ~14 КиБ, и постоянный план его съедал. План пересчитывается, только когда в карте что-то изменилось;
 * без памяти на расчёт — без склеек. */
bool map_merge_wanted;
/* Codex: план проверяется точно, без вероятностного равенства хэшей. У неизменных
 * прямоугольников все отношения перекрытия прежние. Для изменившихся проверяется
 * каждая пара, чей порядок меняет план. Частично распускать группы нельзя: это
 * могло бы изменить порядок относительно другой ранее принятой группы. */
bool map_merge_reuse(){
    Prof prof(P_MERGE);const size_t n=map_draws.size();
    if(map_merged.size()!=map_merge_groups.size())return false;
    auto overlaps=[](const Rect &p,const Rect &q){return p.x<q.x+q.w&&q.x<p.x+p.w&&p.y<q.y+q.h&&q.y<p.y+p.h;};
    for(unsigned c=0;c<merge_changed_count;++c){const size_t m=merge_changed[c];if(m>=n)return false;
        const size_t fm=map_merge_anchor[m];const auto &r=map_draws[m].r;
        for(size_t k=0;k<n;++k){const size_t fk=map_merge_anchor[k];if(fm==fk)continue;
            if((k<m)!=(fk<fm)&&overlaps(r,map_draws[k].r))return false;}
    }
    for(size_t i=0;i<map_merge_groups.size();++i){auto &g=map_merge_groups[i];
        bool geometry=false;for(unsigned c=0;c<merge_changed_count;++c)if(map_merge_anchor[merge_changed[c]]==g.anchor){geometry=true;break;}
        uint32_t resource=0,flags=0;unsigned mapping=0;int ox=0,oy=0;Rect box=g.box;int64_t area=0;
        for(unsigned a=0;a<g.count;++a){const size_t k=map_merge_members[g.first+a];const auto &d=map_draws[k];
            if(d.fill||d.bitmap||d.repeat||d.layer||!d.resource||d.r.w<=0||d.r.h<=0)return false;
            const int x=d.step<0?d.r.x+d.sx:d.r.x-d.sx,y=d.r.y-d.sy;
            const uint32_t f=uint32_t(d.raw)|(uint32_t(d.opaque)<<1)|(uint32_t(d.cpu)<<2)|(uint32_t(d.step<0)<<3)|(uint32_t(d.color)<<8)|(uint32_t(d.mask)<<16);
            if(!a){resource=d.resource;mapping=d.mapping;flags=f;ox=x;oy=y;}
            else if(d.resource!=resource||d.mapping!=mapping||f!=flags||x!=ox||y!=oy)return false;
            if(geometry){
                if(!a)box=d.r;
                const int right=std::max(box.x+box.w,d.r.x+d.r.w),bottom=std::max(box.y+box.h,d.r.y+d.r.h);
                box.x=std::min(box.x,d.r.x);box.y=std::min(box.y,d.r.y);box.w=right-box.x;box.h=bottom-box.y;area+=int64_t(d.r.w)*d.r.h;
                for(unsigned b=0;b<a;++b)if(overlaps(d.r,map_draws[map_merge_members[g.first+b]].r))return false;
            }
        }
        if(geometry&&area!=int64_t(box.w)*box.h)return false;
        g.box=box;auto &m=map_merged[i];m=map_draws[g.anchor];m.r=box;m.sx=(flags&8u)?ox-box.x:box.x-ox;m.sy=box.y-oy;
    }
    ++stat_merge_reuse;return true;
}
void map_merge_refresh(){
#ifndef HMM2_HOST
    // Codex: на ESP обычная карта уже собрана кольцом текстур; старый план
    // склейки остаётся оракулом ПК и не занимает ограниченный размер ELF.
    map_merge_wanted=false;
#else
    if(!map_merge_wanted){
        if(!map_merge_at.empty()||!map_merged.empty()){std::vector<int16_t>().swap(map_merge_at);std::vector<Draw>().swap(map_merged);map_merge_skipped=0;
            std::vector<MergeGroup>().swap(map_merge_groups);std::vector<uint16_t>().swap(map_merge_members);std::vector<uint16_t>().swap(map_merge_anchor);map_merge_mark();}
        return;
    }
    bool changed=map_merge_at.size()!=map_draws.size();for(const auto &p:map_parts)changed|=p.dirty;
    if(changed){
        bool reuse=!merge_geometry_reset&&map_merge_anchor.size()==map_draws.size()&&map_merge_at.size()==map_draws.size();
#ifdef HMM2_HOST
        // Codex: полный пересчёт остаётся независимым оракулом на ПК.
        {static const bool off=std::getenv("HMM2_NO_MERGE_CACHE")!=nullptr;if(off)reuse=false;}
#endif
        try{if(!reuse||!map_merge_reuse())map_merge_plan();}
        catch(...){std::vector<int16_t>().swap(map_merge_at);std::vector<Draw>().swap(map_merged);
            map_merge_groups.clear();map_merge_members.clear();map_merge_anchor.clear();}
        map_merge_mark();}
    merge_changed_count=0;merge_geometry_reset=false;
#endif
}
/* Клод: окно поверх плотной карты (сюжетное сообщение на старте Alteris: 338 слоёв окна над картой) — карта под непрозрачным окном
 * не видна, но выводилась целиком: 2942 слова до прохода ручек, 2256 после. Переполнение уводило кадр в «целые клетки», CPU запекал
 * в клетки и рамку карты, и эти клетки оставались в списке экрана (+68 слоёв, ~200 слов в каждом кадре) — после окна героя кадры
 * снова переполнялись (стенд, сборка 95). Теперь при переполнении (map_cover_wanted) куски карты, целиком лежащие под непрозрачным
 * прямоугольником слоя экрана выше заместителя, в части карты не входят (map_hidden): этих точек и так не видно. Непрозрачность —
 * как у occlude: флаг opaque или прямоугольники маски из DAT; слои с альфой, повторы и заливки-преобразования не закрывают.
 * Набор прямоугольников изменился — части карты собираются заново. */
bool map_cover_wanted;std::vector<Rect> map_covers;uint64_t map_cover_hash;
void map_covers_refresh(const Sequence<Draw> &screen){
    std::vector<Rect> covers;
#ifdef HMM2_HOST
    // Клод: на ПК отсечение включается и без переполнения — для сверки кадров с ним и без него.
    {static const bool force=std::getenv("HMM2_FORCE_COVER")!=nullptr,off=std::getenv("HMM2_NO_COVER")!=nullptr;if(force)map_cover_wanted=true;if(off)map_cover_wanted=false;}
#endif
    if(map_cover_wanted){
        auto add=[&](const Draw &d){
            if(d.layer||d.color||d.repeat)return;
            const auto v=intersect(d.r,map_window);if(!v.w||!v.h)return;
            if(d.opaque){if(!(d.fill&&d.mask))covers.push_back(v);return;}
            if(!d.resource||d.bitmap||d.fill)return;
            if(const auto *q=hmm_resource_opaque(d.resource))for(unsigned k=0;k<4;++k){
                Rect r{d.r.x+(d.step>0?int(q[k].x)-d.sx:d.sx-int(q[k].x)-int(q[k].w)+1),d.r.y+int(q[k].y)-d.sy,int(q[k].w),int(q[k].h)};
                r=intersect(intersect(r,d.r),map_window);if(r.w&&r.h)covers.push_back(r);}
        };
        size_t i=0;while(i<screen.size()&&!screen[i].layer)++i;
        for(++i;i<screen.size();++i)add(screen[i]);
    }
    uint64_t h=1469598103934665603ull;for(const auto &r:covers)for(int v:{r.x,r.y,r.w,r.h}){h^=uint32_t(v);h*=1099511628211ull;}
    if(h!=map_cover_hash){body_plan_clear();map_cover_hash=h;map_covers.swap(covers);for(auto &p:map_parts)p.dirty=true;
#ifdef HMM2_HOST
        if(std::getenv("HMM2_COVER_LOG")){std::printf("COVERS %zu:",map_covers.size());for(const auto &r:map_covers)std::printf(" %d,%d,%d,%d",r.x,r.y,r.w,r.h);std::printf("%c",10);}
#endif
    }
}
bool map_hidden(const Draw &d){
    for(const auto &c:map_covers)if(d.r.x>=c.x&&d.r.y>=c.y&&d.r.x+d.r.w<=c.x+c.w&&d.r.y+d.r.h<=c.y+c.h)return true;
    return false;
}
void map_record_end(){
    if(ring_enabled()){map_draws.resize(map_position);map_bounds=map_window;return;}
    map_part_finish();
    const size_t n=map_part_count?map_part_index+1:map_part_index;
    for(size_t i=n;i<map_parts.size();++i)map_cache_release(map_parts[i]);
    map_parts.resize(n);map_draws.resize(map_position);
    map_spans.resize(map_span_position);map_plan_valid=map_plan_stable;
    map_bounds=intersect(map_bounds,map_window);
    // Codex: план хранит экранные координаты, его группы движутся вместе с камерой.
    for(auto &g:map_merge_groups){g.box.x+=map_shift_x;g.box.y+=map_shift_y;}
    for(auto &m:map_merged){m.r.x+=map_shift_x;m.r.y+=map_shift_y;}
    if(!ring_enabled())map_merge_refresh();
}
// Codex: палитры fade растут страницами; рост 64→128 таблиц требовал 32 КиБ
// при крупнейшем блоке 19 КиБ и завершал длинный бой исключением.
#include "hmm_palette_pages.inc"
PaletteTables mappings;
// Клод: «сплошная» таблица — тени (2–5) и осветление (6–15) спрайта под ней прозрачны (контур отряда, hmm_gpu_contour).
std::vector<bool> mapping_solid;
std::vector<std::weak_ptr<Bitmap>> bitmaps;
uint64_t bitmap_id=1;
// Codex: производительность измеряется внешней трассой QEMU. Счётчики на каждом
// примитиве убраны из рабочего рендера, чтобы не расходовать такты и лимит ELF.
const uint8_t *hmm_transform_table();
Rect bounds(const Image &im){return {0,0,im.width(),im.height()};}
Draw clip(Draw d,Rect r){r=intersect(r,d.r);d.sx+=(r.x-d.r.x)*d.step;d.sy+=r.y-d.r.y;d.r=r;return d;}
void subtract(std::vector<Rect> &parts,Rect area){
    std::vector<Rect> next;
    for(auto p:parts){auto r=intersect(p,area);if(!r.w||!r.h){next.push_back(p);continue;}
        Rect rest[]={{p.x,p.y,p.w,r.y-p.y},{p.x,r.y+r.h,p.w,p.y+p.h-r.y-r.h},
                     {p.x,r.y,r.x-p.x,r.h},{r.x+r.w,r.y,p.x+p.w-r.x-r.w,r.h}};
        for(auto q:rest)if(q.w&&q.h)next.push_back(q);
    }parts.swap(next);
}
void unite(std::vector<Rect> &parts,Rect r){
    // Codex: объединение геометрических полос не читает пиксели изображения.
    for(size_t i=0;i<parts.size();){auto c=parts[i];
        if(c.x==r.x&&c.w==r.w&&(c.y+c.h==r.y||r.y+r.h==c.y)){r.y=std::min(r.y,c.y);r.h+=c.h;}
        else if(c.y==r.y&&c.h==r.h&&(c.x+c.w==r.x||r.x+r.w==c.x)){r.x=std::min(r.x,c.x);r.w+=c.w;}
        else {++i;continue;}
        parts.erase(parts.begin()+i);i=0;
    }
    parts.push_back(r);
}
void repeat_fog(Sequence<Draw> &draws){
    // Codex: происхождение CLOF32 известно движку; объединяем геометрию,
    // не сравнивая и не рисуя пиксели. Период включает все четыре исходных тайла.
    // Codex: уплотняем уже обработанный префикс на месте. Необработанный хвост
    // не сдвигается; перенос Draw не копирует атомарный счётчик shared_ptr.
    size_t written=0;
    // Codex: до первого repeat нет кандидатов для склейки; неповторяющийся префикс не сканируется вновь.
    size_t first_repeat=draws.size();
    for(size_t read=0;read<draws.size();++read){auto &d=draws[read];
        const bool fog=d.resource&&!d.bitmap&&(hmm_resource_ft(d.resource,d.raw).flags&4);
        if(fog&&d.step<0){d.sx+=int(d.resource&65535)*32;d.resource&=0xffff0000u;d.repeat=true;}
        else if(fog){
            const int ox=d.sx-d.r.x,oy=d.sy-d.r.y;
            int bx=(ox&31)+(((int(d.resource&65535)+(ox>>5)+(oy>>5))&3)*32),by=oy&31;
            d.resource&=0xffff0000u;d.repeat=true;
            for(size_t j=written;j>first_repeat;){auto &a=draws[--j];
                if(a.repeat&&a.resource==d.resource&&a.mapping==d.mapping&&
                   ((a.sx-a.r.x)&127)==bx&&((a.sy-a.r.y)&127)==by){
                    Rect r=a.r;
                    if(r.x==d.r.x&&r.w==d.r.w&&(r.y+r.h==d.r.y||d.r.y+d.r.h==r.y)){
                        d.r.y=std::min(r.y,d.r.y);d.r.h+=r.h;
                    }else if(r.y==d.r.y&&r.h==d.r.h&&(r.x+r.w==d.r.x||d.r.x+d.r.w==r.x)){
                        d.r.x=std::min(r.x,d.r.x);d.r.w+=r.w;
                    }else continue;
                    std::move(draws.begin()+j+1,draws.begin()+written,draws.begin()+j);j=--written;continue;
                }
                auto overlap=intersect(a.r,d.r);if(overlap.w&&overlap.h)break;
            }
            d.sx=(d.r.x+bx)&127;d.sy=(d.r.y+by)&127;
        }
        if(d.repeat)first_repeat=std::min(first_repeat,written);
        // Codex: при отсутствии слияния слой уже стоит на месте, записи не нужны.
        if(written!=read)draws[written]=std::move(d);++written;
    }
    draws.resize(written);
}
bool same_layer_source(const Draw &a,const Draw &d){
    return !a.layer&&!d.layer&&a.resource==d.resource&&a.bitmap==d.bitmap&&a.mapping==d.mapping&&a.raw==d.raw&&
        a.step==d.step&&a.fill==d.fill&&a.repeat==d.repeat&&a.opaque==d.opaque&&a.color==d.color&&
        (d.fill?a.mask==d.mask:(a.sx-a.r.x*a.step==d.sx-d.r.x*d.step&&a.sy-a.r.y==d.sy-d.r.y));
}
void join_layers(Sequence<Draw> &draws,size_t lookback=16){
    // Codex: ImageRestorer возвращает части одного слоя. Склеиваем соседние
    // прямоугольники с тем же происхождением, сохраняя порядок перекрытий.
    // Codex: результат занимает префикс исходного списка: число слоёв только убывает.
    // Автобой Ariel иначе одновременно держал второй массив и отказал на странице 3328 Б.
    size_t written=0;
    // Codex: отрицательный ответ фильтра исключает прежний совместимый слой.
    // Коллизия лишь оставляет исходный поиск; биты не снимаются при слиянии/удалении.
    std::array<uint32_t,64> seen{};
    // Keep source tags beside the output. A miss avoids loading a complete
    // Draw (and its shared pointer) from PSRAM in the 128-layer map search.
    // Collisions still use the full comparison and preserve the merge order.
    Sequence<uint16_t> sources;sources.resize(draws.size());
    struct Background {const Bitmap *bitmap=nullptr;size_t at=0;};Background backgrounds[8];
    // Клод: копия Draw на каждый слой — атомарный счётчик shared_ptr; исходный список всё равно заменяется, поэтому перенос.
    // Поиск назад ограничен 16 совпадениями, проверка перекрытий — 64 слоями: на списке в тысячи записей они были квадратичны. Вне окна слои просто не склеиваются, картинка та же.
    for(auto &item:draws){Draw d=std::move(item);bool merged_early=false;
        // Codex: восстановление малого участка уже видимого непрозрачного фона
        // из того же Bitmap не добавляет пикселей. Чужое перекрытие запрещает пропуск.
        const auto *bitmap=d.bitmap.get();
        if(d.opaque&&bitmap&&!bitmap->frame&&bitmap->single&&bitmap->w*bitmap->h>=4096&&d.r.w*d.r.h<=1024){
            const auto &hint=backgrounds[(reinterpret_cast<uintptr_t>(bitmap)>>4)&7];
            if(hint.bitmap==bitmap&&hint.at<written&&written-hint.at<=64){
                const auto &base=draws[hint.at];const auto hit=intersect(base.r,d.r);
                if(same_layer_source(base,d)&&hit.w==d.r.w&&hit.h==d.r.h){bool blocked=false;
                    for(size_t k=hint.at+1;k<written;++k){const auto v=intersect(draws[k].r,d.r);if(v.w&&v.h&&!same_layer_source(draws[k],d)){blocked=true;break;}}
                    if(!blocked)continue;
                }
            }
        }
        uint32_t hash=d.resource^uint32_t(reinterpret_cast<uintptr_t>(d.bitmap.get())>>4);
        hash=(hash^d.mapping)*16777619u;
        hash=(hash^uint32_t(d.step)^(unsigned(d.raw)|(unsigned(d.fill)<<1)|(unsigned(d.repeat)<<2)|(unsigned(d.opaque)<<3)))*16777619u;
        if(d.fill)hash=(hash^unsigned(d.color)^(unsigned(d.mask)<<8))*16777619u;
        else {hash=(hash^uint32_t(d.sx-d.r.x*d.step)^(unsigned(d.color)<<24))*16777619u;hash=(hash^uint32_t(d.sy-d.r.y))*16777619u;}
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
        const unsigned at=(hash>>5)&63;const uint32_t bit=1u<<(hash&31);const bool candidate=!d.layer&&(seen[at]&bit);seen[at]|=bit;
        const uint16_t source=uint16_t(hash);
        for(size_t j=written,checked=0;candidate&&j&&checked<lookback;++checked){--j;
            if(written-j>std::max<size_t>(64,lookback))break;
            if(sources[j]!=source)continue;
            const auto &a=draws[j];
            if(!same_layer_source(a,d))continue;
            Rect r=d.r;
            if(a.r.x==r.x&&a.r.w==r.w&&(a.r.y+a.r.h==r.y||r.y+r.h==a.r.y)){
                r.y=std::min(r.y,a.r.y);r.h+=a.r.h;
            }else if(a.r.y==r.y&&a.r.h==r.h&&(a.r.x+a.r.w==r.x||r.x+r.w==a.r.x)){
                r.x=std::min(r.x,a.r.x);r.w+=a.r.w;
            }else continue;
            bool blocked=false;for(size_t k=j+1;k<written;++k){auto overlap=intersect(a.r,draws[k].r);if(overlap.w&&overlap.h){blocked=true;break;}}
            if(blocked){
                // Codex: можно перенести позднюю часть назад, если она не перекрывает промежуточные слои.
                blocked=false;for(size_t k=j+1;k<written;++k){auto overlap=intersect(d.r,draws[k].r);if(overlap.w&&overlap.h){blocked=true;break;}}
                if(blocked)continue;
                auto &earlier=draws[j];earlier.sx+=(r.x-earlier.r.x)*earlier.step;earlier.sy+=r.y-earlier.r.y;earlier.r=r;merged_early=true;break;
            }
            d.sx+=(r.x-d.r.x)*d.step;d.sy+=r.y-d.r.y;d.r=r;
            for(auto &hint:backgrounds)if(hint.bitmap){if(hint.at==j)hint.bitmap=nullptr;else if(hint.at>j)--hint.at;}
            std::move(draws.begin()+j+1,draws.begin()+written,draws.begin()+j);
            std::move(sources.begin()+j+1,sources.begin()+written,sources.begin()+j);--written;j=written;checked=0;
        }
        if(!merged_early){
            if(d.opaque&&bitmap&&!bitmap->frame&&bitmap->single&&d.r.w*d.r.h>=4096){
                auto &hint=backgrounds[(reinterpret_cast<uintptr_t>(bitmap)>>4)&7];
                if(hint.bitmap!=bitmap||draws[hint.at].r.w*draws[hint.at].r.h<d.r.w*d.r.h)hint={bitmap,written};
            }
            sources[written]=source;draws[written++]=std::move(d);
        }
    }
    draws.resize(written);
}
// Join all fragments of one map instance, even when an entire render row
// separates them. Tombstones avoid moving hundreds of shared Draw records
// after every join; the exact overlap checks preserve the original pixels.
HMM_SIZE_RENDER void join_map_layers(Sequence<Draw> &draws){
    const unsigned count=unsigned(draws.size());if(count<2)return;
    constexpr uint16_t none=0xffff;
    if(count>=none){join_layers(draws,128);return;}
    std::unique_ptr<std::array<uint16_t,1024>> heads(new(std::nothrow) std::array<uint16_t,1024>);
    std::unique_ptr<uint16_t[]> next(new(std::nothrow) uint16_t[count]);
    if(!heads||!next){join_layers(draws,128);return;}
    // Queries read only rectangles, not shared Draw records from PSRAM.
    // Block bounds may include removed layers: this only causes extra checks.
    // A growing merged rectangle always expands its block before the next query.
    constexpr unsigned block_shift=4,block_size=1u<<block_shift;
    std::unique_ptr<Rect[]> rects(new(std::nothrow) Rect[count]);
    std::unique_ptr<Rect[]> blocks(new(std::nothrow) Rect[(count+block_size-1)>>block_shift]);
#ifdef HMM2_HOST
    if(std::getenv("HMM2_JOIN_REFERENCE")){rects.reset();blocks.reset();}
    unsigned tested=0,skipped=0,merges=0;
    const bool verify=std::getenv("HMM2_JOIN_VERIFY")!=nullptr;
#endif
    auto overlaps=[](const Rect &a,const Rect &b){
        return a.w>0&&a.h>0&&b.w>0&&b.h>0&&a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
    };
    auto expand=[](Rect &a,const Rect &r){
        if(r.w<=0||r.h<=0)return;
        if(!a.w){a=r;return;}
        const int x=std::min(a.x,r.x),y=std::min(a.y,r.y);
        a.w=std::max(a.x+a.w,r.x+r.w)-x;a.h=std::max(a.y+a.h,r.y+r.h)-y;a.x=x;a.y=y;
    };
    const bool indexed=rects&&blocks;
    if(indexed)for(unsigned i=0;i<count;++i){rects[i]=draws[i].r;expand(blocks[i>>block_shift],rects[i]);}
    heads->fill(none);
    for(unsigned i=0;i<count;++i){auto &d=draws[i];if(d.layer||!d.r.w||!d.r.h)continue;
        uint32_t h=d.resource^uint32_t(reinterpret_cast<uintptr_t>(d.bitmap.get())>>4);
        h=(h^d.mapping)*16777619u;
        if(d.fill)h=(h^d.color^(uint32_t(d.mask)<<8))*16777619u;
        else{h=(h^uint32_t(d.sx-d.r.x*d.step))*16777619u;h=(h^uint32_t(d.sy-d.r.y))*16777619u;}
        h^=h>>16;auto &head=(*heads)[h&1023];next[i]=head;head=uint16_t(i);
        unsigned active=i;bool joined;
        do{joined=false;
            for(unsigned j=head;j!=none;j=next[j]){if(j==active||!draws[j].r.w)continue;
                const unsigned lo=std::min(j,active),hi=std::max(j,active);
                auto &a=draws[lo];auto &b=draws[hi];if(!same_layer_source(a,b))continue;
                Rect r=a.r;
                if(r.x==b.r.x&&r.w==b.r.w&&(r.y+r.h==b.r.y||b.r.y+b.r.h==r.y)){r.y=std::min(r.y,b.r.y);r.h+=b.r.h;}
                else if(r.y==b.r.y&&r.h==b.r.h&&(r.x+r.w==b.r.x||b.r.x+b.r.w==r.x)){r.x=std::min(r.x,b.r.x);r.w+=b.r.w;}
                else continue;
                bool blocked_late=false,blocked_early=false;
                if(indexed){
                    for(unsigned k=lo+1;k<hi&&!(blocked_late&&blocked_early);){
                        if(!(k&(block_size-1))&&k+block_size<=hi){const auto &box=blocks[k>>block_shift];
                            if((blocked_late||!overlaps(a.r,box))&&(blocked_early||!overlaps(b.r,box))){
                                k+=block_size;
#ifdef HMM2_HOST
                                skipped+=block_size;
#endif
                                continue;
                            }
                        }
                        const auto &q=rects[k++];
                        if(!blocked_late)blocked_late=overlaps(a.r,q);
                        if(!blocked_early)blocked_early=overlaps(b.r,q);
#ifdef HMM2_HOST
                        ++tested;
#endif
                    }
                }else{
                    // Optional scratch allocation failed: retain the exact old
                    // merge algorithm, including its selection and layer order.
                    for(unsigned k=lo+1;k<hi&&!(blocked_late&&blocked_early);++k){const auto &q=draws[k];if(!q.r.w)continue;
                        auto v=intersect(a.r,q.r);blocked_late|=v.w&&v.h;
                        v=intersect(b.r,q.r);blocked_early|=v.w&&v.h;
                    }
                }
#ifdef HMM2_HOST
                if(indexed&&verify){
                    bool old_late=false,old_early=false;
                    for(unsigned k=lo+1;k<hi&&!(old_late&&old_early);++k){const auto &q=draws[k];if(!q.r.w)continue;
                        auto v=intersect(a.r,q.r);old_late|=v.w&&v.h;
                        v=intersect(b.r,q.r);old_early|=v.w&&v.h;
                    }
                    if(old_late!=blocked_late||old_early!=blocked_early)hmm_fail("join overlap index changed layer order");
                }
#endif
                if(blocked_late&&blocked_early)continue;
                const unsigned keep=blocked_late?lo:hi,drop=blocked_late?hi:lo;auto &target=draws[keep];
                target.sx+=(r.x-target.r.x)*target.step;target.sy+=r.y-target.r.y;target.r=r;draws[drop].r.w=0;
                if(indexed){rects[keep]=r;rects[drop].w=0;expand(blocks[keep>>block_shift],r);}
#ifdef HMM2_HOST
                ++merges;
#endif
                active=keep;joined=true;break;
            }
        }while(joined);
    }
#ifdef HMM2_HOST
    if(std::getenv("HMM2_JOIN_STATS"))std::printf("JOIN_MAP n=%u indexed=%u tested=%u skipped=%u merges=%u\n",count,unsigned(indexed),tested,skipped,merges);
#endif
    size_t out=0;for(size_t i=0;i<draws.size();++i)if(draws[i].r.w){if(out!=i)draws[out]=std::move(draws[i]);++out;}draws.resize(out);
}
struct CullRect {uint64_t silhouette=0;const HmmOpaqueRect *opaque=nullptr;Rect visible;};
Sequence<CullRect> cull_rects;
struct CullJob {const Sequence<Draw> *draws;Sequence<CullRect> *rects;size_t count;};
HMM_RENDER_NOINLINE void hmm_render_job_cull(void *arg){
    const auto &job=*static_cast<CullJob*>(arg);
    // Codex: бит означает геометрическое покрытие прямоугольником, а не цвет.
    // Фиксированные 38400 байт заменяют квадратичное дробление тысяч прямоугольников.
    // Клод: столбцы по 32 точки подряд по y (covered[x][y]): внутренние циклы идут по соседним словам, а не через 80 байт.
    static uint32_t covered[20][480];
    // Codex: пустой/полный блок 32x32 проверяется без прохода по 32 строкам.
    // Частичная маска очищается только при первом покрытии блока в этом кадре.
    static uint8_t kind[20][15];std::memset(kind,0,sizeof(kind));
    auto cover_rect=[&](Rect r){
        r=intersect(r,{0,0,640,480});if(!r.w||!r.h)return;
        const int first=r.x/32,last=(r.x+r.w-1)/32;
        for(int x=first;x<=last;++x){uint32_t mask=~0u;
            if(x==first)mask&=~0u<<(r.x&31);
            if(x==last)mask&=~0u>>(31-((r.x+r.w-1)&31));
            for(int by=r.y/32;by<=(r.y+r.h-1)/32;++by){
                auto &k=kind[x][by];if(k==2)continue;
                const int lo=std::max(r.y,by*32),hi=std::min(r.y+r.h,by*32+32);
                if(mask==~0u&&lo==by*32&&hi==by*32+32){k=2;continue;}
                if(!k){std::memset(covered[x]+by*32,0,32*sizeof(uint32_t));k=1;}
                for(int y=lo;y<hi;++y)covered[x][y]|=mask;
            }
        }
    };
    // Клод: кадр с тем же силуэтом на том же месте выше по списку закрывает слой целиком:
    // непрозрачные точки нового кадра совпадают с видимыми точками старого. Иначе
    // анимации без восстановления фона (фонарь главного меню) копят слои до переполнения DL.
    struct Seen {uint64_t id;Rect r;int sx,sy,step;};
    static Seen seen[512];static uint16_t slot[1024];unsigned seen_count=0;std::memset(slot,0xff,sizeof(slot));
    auto hidden=[&](const Draw &d,uint64_t id){
        if(!id)return false;
        uint64_t h=id^(uint64_t(uint32_t(d.r.x)<<16^uint32_t(d.r.y))*0x9e3779b97f4a7c15ull)^
                   (uint64_t(uint32_t(d.sx)<<16^uint32_t(d.sy))*0xc2b2ae3d27d4eb4full)^uint64_t(uint32_t(d.step));
        unsigned i=unsigned(h^(h>>32))&1023;
        for(;slot[i]!=0xffff;i=(i+1)&1023){const auto &s=seen[slot[i]];
            if(s.id==id&&s.r.x==d.r.x&&s.r.y==d.r.y&&s.r.w==d.r.w&&s.r.h==d.r.h&&s.sx==d.sx&&s.sy==d.sy&&s.step==d.step)return true;}
        // Клод: таблица заполнена не больше чем наполовину; сверх 512 силуэтов правило просто не применяется.
        if(seen_count<512){seen[seen_count]={id,d.r,d.sx,d.sy,d.step};slot[i]=uint16_t(seen_count++);}
        return false;
    };
    for(size_t index=job.count;index--;){
        const auto *it=&(*job.draws)[index];auto &result=(*job.rects)[index];
        auto r=intersect(it->r,{0,0,640,480});if(!r.w||!r.h)continue;
        if(hidden(*it,result.silhouette))continue;
        int top=480,bottom=0,left=640,right=0;
        const int first=r.x/32,last=(r.x+r.w-1)/32;
        for(int x=first;x<=last;++x){uint32_t mask=~0u,uncovered=0;
            if(x==first)mask&=~0u<<(r.x&31);
            if(x==last)mask&=~0u>>(31-((r.x+r.w-1)&31));
            for(int by=r.y/32;by<=(r.y+r.h-1)/32;++by){
                const auto k=kind[x][by];if(k==2)continue;
                const int lo=std::max(r.y,by*32),hi=std::min(r.y+r.h,by*32+32);
                if(!k){top=std::min(top,lo);bottom=std::max(bottom,hi);uncovered|=mask;continue;}
                for(int y=lo;y<hi;++y){uint32_t bits=mask&~covered[x][y];
                    if(bits){top=std::min(top,y);bottom=std::max(bottom,y+1);uncovered|=bits;}
                }
            }
            if(uncovered){int l=0,h=32;while(!(uncovered&(1u<<l)))++l;while(!(uncovered&(1u<<(h-1))))--h;
                left=std::min(left,x*32+l);
                right=std::max(right,x*32+h);
            }
        }
        // Codex: оставляем один охватывающий прямоугольник. Внутренние просветы
        // не размножают команды; полностью скрытый слой вообще не входит в DL.
        if(bottom>top)result.visible=it->r.w*it->r.h>=32768?Rect{left,top,right-left,bottom-top}:it->r;
        if(it->opaque)cover_rect(r);
        else if(result.opaque){
            // Codex: покрытие получено из оригинальной маски при упаковке DAT.
            // При отражении и обрезке переносим его геометрию, не читая изображение.
            const auto *rects=result.opaque;
            for(unsigned i=0;i<4;++i){const auto q=rects[i];
                Rect r{it->r.x+(it->step>0?int(q.x)-it->sx:it->sx-int(q.x)-q.w+1),it->r.y+int(q.y)-it->sy,int(q.w),int(q.h)};
                r=intersect(r,it->r);if(r.w&&r.h)cover_rect(r);
            }
        }
    }

}
// Codex: геометрия считается без копирования shared_ptr и без выделений на core0.
// Игровой поток заранее разрешает метаданные, а пока идёт расчёт — готовит звук.
void occlude(Sequence<Draw> &draws){
    Prof prof(P_OCCLUDE);
    if(draws.empty())return;
    auto &rects=cull_rects;rects.clear();rects.resize(draws.size());
    for(size_t i=0;i<draws.size();++i){const auto &d=draws[i];auto &r=rects[i];
        if(d.resource){if(!d.opaque)r.opaque=hmm_resource_opaque(d.resource);
            if(!d.bitmap&&!d.fill&&!d.raw&&!d.repeat)r.silhouette=hmm_resource_silhouette(d.resource);}
    }
    CullJob job{&draws,&rects,draws.size()};RenderGuard guard;
    const bool parallel=draws.size()>=64?render_submit(hmm_render_job_cull,&job):(hmm_render_job_cull(&job),false);
    if(parallel)hmm_audio_tick();render_wait();
    size_t out=0;
    for(size_t i=0;i<draws.size();++i){const auto r=rects[i].visible;if(!r.w||!r.h)continue;
        auto &d=draws[i];d.sx+=(r.x-d.r.x)*d.step;d.sy+=r.y-d.r.y;d.r=r;
        if(out!=i)draws[out]=std::move(d);++out;
    }
    draws.resize(out);
}
// Клод: cover правит список на месте. Прежде каждый вызов строил новый вектор из всех слоёв
// (около 600 записей по 56 байт экрана карты, 233 вызова на кадр): трасса QEMU — около 7,5 млн
// команд полной перерисовки карты из 12,7 млн, на плате — мегабайты записи в PSRAM за кадр.
// Результат тот же: порядок слоёв и части разрезанных слоёв не меняются.
HMM_SIZE_RENDER void cover(Canvas &c,Rect area,bool cut=true){
    Prof prof(P_COVER);
    auto &v=c.draws;const size_t n=v.size();
    // Задет ли слой: при cut — любое пересечение, без cut — только слой целиком внутри area.
    auto hit=[&](const Draw &d){const auto r=intersect(d.r,area);return r.w&&r.h&&(cut||(r.w==d.r.w&&r.h==d.r.h));};
    size_t i=0;while(i<n&&!hit(v[i]))++i;
    if(i==n)return;
    changed(c);
    if(!cut){
        // Без разреза слои только удаляются — уплотняем хвост сдвигом.
        size_t w=i;
        for(size_t k=i+1;k<n;++k)if(!hit(v[k])){v[w]=std::move(v[k]);++w;}
        v.erase(v.begin()+w,v.end());return;
    }
    // С разрезом слой может стать четырьмя частями: хвост с первого задетого собираем отдельно.
    Sequence<Draw> tail;tail.reserve(n-i+4);
    for(size_t k=i;k<n;++k){
        Draw &d=v[k];const auto r=intersect(d.r,area);
        if(!r.w||!r.h){tail.push_back(std::move(d));continue;}
        Rect parts[]={{d.r.x,d.r.y,d.r.w,r.y-d.r.y},{d.r.x,r.y+r.h,d.r.w,d.r.y+d.r.h-r.y-r.h},
                      {d.r.x,r.y,r.x-d.r.x,r.h},{r.x+r.w,r.y,d.r.x+d.r.w-r.x-r.w,r.h}};
        for(auto p:parts)if(p.w>0&&p.h>0)tail.push_back(clip(d,p));
    }
    v.erase(v.begin()+i,v.end());
    for(auto &d:tail)v.push_back(std::move(d));
}
HMM_RENDER_NOINLINE bool bitmap_matches(const Bitmap &old,const Image &im,const uint8_t *p,Rect r){
    if(old.pixels.empty()||old.frame||old.w!=r.w||old.h!=r.h||old.single!=im.singleLayer())return false;
    for(int k=0;k<(im.singleLayer()?1:2);++k)for(int y=0;y<r.h;++y)
        if(std::memcmp(old.pixels.data()+(k*r.h+y)*r.w,p+(k*im.height()+r.y+y)*im.width()+r.x,r.w))return false;
    return true;
}
std::shared_ptr<Bitmap> capture_pixels(const Image &im,const uint8_t *p,Rect r){
    const int planes=im.singleLayer()?1:2;
    for(auto it=bitmaps.begin();it!=bitmaps.end();){
        if(auto old=it->lock()){
            if(bitmap_matches(*old,im,p,r))return old;
            ++it;
        }else it=bitmaps.erase(it);
    }
    for(auto **at=&free_bitmaps[1];*at;at=&(*at)->next)if(bitmap_matches(**at,im,p,r)){
        auto *old=*at;*at=old->next;auto b=hold_bitmap(old);bitmaps.push_back(b);return b;
    }
    auto b=take_bitmap(size_t(r.w)*r.h*planes);b->w=r.w;b->h=r.h;b->single=im.singleLayer();
    // Codex: свободный буфер сохраняет ёмкость, но не удваивает прежний размер.
    // Снимок 6012 байт иначе запрашивал 11360 при крупнейшем блоке 10496.
    b->pixels.clear();b->pixels.resize(size_t(r.w)*r.h*planes);
    for(int k=0;k<planes;++k)for(int y=0;y<r.h;++y)std::memcpy(b->pixels.data()+(k*r.h+y)*r.w,p+(k*im.height()+r.y+y)*im.width()+r.x,r.w);
    b->id=bitmap_id++;bitmaps.push_back(b);return b;
}
// Codex: на границе кадра лишние целые страницы возвращаются местному пулу.
// Для сокращения списка не выделяется второй непрерывный массив его элементов.
void shrink_layers(Sequence<Draw> &v){
    v.trim();
}
void canvases_shrink(){
    for(auto &item:canvases)shrink_layers(item.second.draws);
    shrink_layers(map_draws);
}
// Клод: длинный список сразу чистится от скрытых слоёв. Запрос 425984 байта в журнале сборки 68 — это рост вектора до 8192 слоёв (bad_alloc в бою),
// при том что на экран шло около 1200 слов DL: почти всё в списке было закрыто. Следующая чистка — когда список снова вырастет вдвое.
void occlude(Sequence<Draw> &draws);
void compact(const Image &im,Canvas &c){
    if(c.draws.size()<c.compact_at||!im.singleLayer()||im.width()>640||im.height()>480)return;
    occlude(c.draws);c.compact_at=std::max<size_t>(1024,c.draws.size()*2);
}
void flush(const Image &im,Canvas &c){
    if(!c.writing)return;
    Draw d;d.r=c.pending;d.bitmap=capture_pixels(im,c.pixels,d.r);d.opaque=im.singleLayer();d.raw=im.singleLayer();d.cpu=true;
    cover(c,d.r,!im.singleLayer());c.draws.push_back(std::move(d));changed(c);c.writing=false;c.pending={};
    compact(im,c);
}
Canvas &canvas(const Image &im){
    Prof prof(P_CANVAS);
    auto found=canvases.find(&im);
    if(found!=canvases.end()){flush(im,found->second);return found->second;}
    Canvas c;
    uint32_t id;
    Draw d;d.r=bounds(im);
    if(hmm_resource_id(im,id)){d.resource=id;d.opaque=im.singleLayer();}
    else if(!im.empty()&&im._hmmPeek()){
        materializing=true;const uint8_t *p=im.image();materializing=false;
        d.bitmap=capture_pixels(im,p,d.r);d.opaque=im.singleLayer();
    }else {d.fill=true;d.mask=im.singleLayer()?0:1;d.opaque=im.singleLayer();}
    if(d.r.w&&d.r.h)c.draws.push_back(std::move(d));
    changed(c);return canvases.emplace(&im,std::move(c)).first->second;
}
Canvas &screen_canvas(){
    const auto *saved=message_target;message_target=nullptr;
    auto &c=canvas(fheroes2::Display::instance());message_target=saved;return c;
}
// Hardware occlusion must include the physical battlefield and modal layers.
HMM_SIZE_RENDER Canvas &frame_canvas(){
    auto &base=screen_canvas();
    if(message_layers.empty()){
        message_geometry.draws.clear();message_geometry_revision=message_base_revision=0;message_geometry_layers=0;
        return base;
    }
    unsigned revision=0;for(const auto &layer:message_layers)revision=std::max(revision,canvas(*layer).revision);
    if(base.revision!=message_base_revision||revision!=message_geometry_revision||message_layers.size()!=message_geometry_layers){
        message_geometry.draws=base.draws;
        for(const auto &layer:message_layers)for(const auto &d:canvas(*layer).draws)message_geometry.draws.push_back(d);
        changed(message_geometry);message_base_revision=base.revision;
        message_geometry_revision=revision;message_geometry_layers=message_layers.size();
    }
    return message_geometry;
}
// Клод: заместитель окна карты → обычные слои на его месте (обрезка по заместителю: occlude мог его сузить).
// Режим карты на этом кончается: следующие слои Redraw пойдут прежним путём, после развёрнутых.
// Клод: expand — для клеток плотной карты (cache_map_tiles): слои карты обрезаются не по заместителю, а по сетке клеток, покрывающей
// окно, чтобы крайняя клетка растеризовалась целиком; [*first, *first+*count) — их место в списке экрана. Только при одном заместителе.
/* Клод: в журнал (первые 16 раз) — кто развернул карту в слои экрана: тогда склейка кусков (map_merge_plan) не действует, и
 * плотная карта переполняет список FT812. map_why и map_why_rect ставит вызывающий. */
const char *map_why="?";Rect map_why_rect;
HMM_SIZE_RENDER void map_materialize(const Rect *expand=nullptr,size_t *first=nullptr,size_t *count=nullptr){
    ring_expand();ring_clear();hmm_ring_world_clear();
    {static unsigned told;if(told<16&&!map_draws.empty()&&!expand){++told;char m[96];
        std::snprintf(m,sizeof(m),"HMM2: map to layers (%s) %d,%d %dx%d draws=%u",map_why,map_why_rect.x,map_why_rect.y,map_why_rect.w,map_why_rect.h,unsigned(map_draws.size()));hmm_log(m);}
     map_why="?";map_why_rect={};}
    // Codex: перед передачей слоёв снимку частные клетки GPU больше не нужны.
    map_tiles_clear();
    map_static_allowed=map_plan_valid=map_plan_stable=false;map_spans.clear();map_span_position=0;
    if(first)*first=*count=0;
    if(map_active)map_record_end();
    map_active=false;
    if(!map_target){map_draws.clear();map_cache_clear();return;}
    auto it=canvases.find(map_target);
    if(it!=canvases.end()){changed(it->second);auto &v=it->second.draws;
        // Codex: последнему заместителю передаём владение слоями: map_draws
        // сразу очищается. При нескольких заместителях предыдущие получают копии.
        auto remaining=std::count_if(v.begin(),v.end(),[](const Draw &d){return d.layer;});
        if(expand&&remaining!=1)expand=nullptr;
        for(size_t i=v.size();i--;)if(v[i].layer){
            const Rect r=expand?*expand:v[i].r;Sequence<Draw> parts;
            --remaining;
            // Codex: последний заместитель получает сами страницы, а не третью
            // копию их ёмкости. Обрезка и порядок слоёв остаются прежними.
            if(remaining)parts.reserve(map_draws.size());else parts.swap(map_draws);
            auto &source=remaining?map_draws:parts;size_t kept=0;
            for(auto &m:source){
                const auto hit=intersect(m.r,r);if(!hit.w||!hit.h)continue;
                Draw e=remaining?m:std::move(m);e.sx+=(hit.x-e.r.x)*e.step;e.sy+=hit.y-e.r.y;e.r=hit;
                if(remaining)parts.push_back(std::move(e));else parts[kept++]=std::move(e);
            }
            if(!remaining)parts.resize(kept);
            if(expand&&first){*first=i;*count=parts.size();}
            v.erase(v.begin()+i);v.insert(v.begin()+i,std::make_move_iterator(parts.begin()),std::make_move_iterator(parts.end()));
        }
    }
    map_draws.clear();map_cache_clear();
}
// Клод: mapping_hashes — хеш каждой таблицы mappings; полное сравнение 256 байт только при равном хеше.
// Прежде каждая таблица сравнивалась побайтно (memcmp ПЗУ) со всеми накопленными.
std::vector<uint32_t> mapping_hashes;
unsigned mapping_id(const uint8_t *p,unsigned prior=0,bool solid=false){
    std::array<uint8_t,256> result;uint32_t hash=2166136261u;
    // Клод: сплошность переходит и на составную таблицу (затемнение экрана поверх контура).
    solid=solid||(prior&&mapping_solid[prior-1]);
    for(unsigned i=0;i<256;++i){result[i]=p[prior?mappings[prior-1][i]:i];hash=(hash^result[i])*16777619u;}
    hash^=solid;
    for(unsigned i=0;i<mappings.size();++i)if(mapping_hashes[i]==hash&&mapping_solid[i]==solid&&mappings[i]==result)return i+1;
    mappings.push_back(result);mapping_hashes.push_back(hash);mapping_solid.push_back(solid);return unsigned(mappings.size());
}
// Клод: слой AlphaBlit на CPU — той же исходной fheroes2::AlphaBlit (смешение по палитре и GetPALColorId), поэтому
// индексы совпадают с оригиналом. Полосы по 16 строк: такую запись hmmCpuStrips не дробит, а materializing выключает перехваты GPU.
HMM_SIZE_RENDER void raster_alpha(const Image &in,const Draw &d,int stride,uint8_t *p){
    struct Strip:Image{Strip(int w,int h){_disableTransformLayer();resize(w,h);}};
    const bool flip=d.step<0;const int inX=flip?in.width()-1-d.sx:d.sx;
    for(int y0=0;y0<d.r.h;y0+=16){
        const int rows=std::min(16,d.r.h-y0);Strip part(d.r.w,rows);uint8_t *strip=part.image();
        for(int y=0;y<rows;++y)std::memcpy(strip+y*d.r.w,p+(d.r.y+y0+y)*stride+d.r.x,d.r.w);
        fheroes2::AlphaBlit(in,inX,d.sy+y0,part,0,0,d.r.w,rows,d.color,flip);
        for(int y=0;y<rows;++y)std::memcpy(p+(d.r.y+y0+y)*stride+d.r.x,strip+y*d.r.w,d.r.w);
    }
}
HMM_SIZE_RENDER void raster(const Image &im,Canvas &c,uint8_t *p,Rect roi,bool window=false){
    const auto *table=hmm_transform_table();const int area=im.width()*im.height();uint8_t *mask=im.singleLayer()?nullptr:p+area;
    for(int y=roi.y;y<roi.y+roi.h;++y){std::memset(p+y*im.width()+roi.x,0,roi.w);if(mask)std::memset(mask+y*im.width()+roi.x,1,roi.w);}
    // Клод: сначала проверка пересечения по ссылке. Копия Draw с атомарным shared_ptr на каждый слой при записи CPU по точкам
    // давала (число слоёв × число точек) копий: на длинном списке это десятки секунд без единого кадра.
    for(const auto &item:c.draws){
        {const auto hit=intersect(item.r,roi);if(!hit.w||!hit.h)continue;}
        // Клод: заместитель карты здесь закрыт слоями поверх (map_visible_in), иначе карта уже развёрнута: пикселей у него нет.
        if(item.layer)continue;
        Draw d=clip(item,roi);
        if(window&&d.raw&&d.fill){
            // Keep a transparent hole for the original hardware video quad.
            for(int y=d.r.y;y<d.r.y+d.r.h;++y)std::memset(mask+y*im.width()+d.r.x,1,d.r.w);
            continue;
        }
        Image region;const uint8_t *src=nullptr,*transform=nullptr,*fog[4]{};int sw=0;uint32_t previous_pin=0,fog_pins[4]{};
        if(d.bitmap){bitmap_materialize(*d.bitmap);src=d.bitmap->pixels.data();sw=d.bitmap->w;if(!d.bitmap->single)transform=src+sw*d.bitmap->h;
            // Клод: снимок с альфой (hmm_gpu_alpha составного изображения) — исходным AlphaBlit, как слой ресурса.
            if(d.color){struct View:Image{View(const Bitmap &b){if(b.single)_disableTransformLayer();resize(b.w,b.h);}}view(*d.bitmap);
                std::memcpy(view.image(),d.bitmap->pixels.data(),d.bitmap->pixels.size());raster_alpha(view,d,im.width(),p);
                bitmap_release_pixels(*d.bitmap);continue;}}
        else if(d.repeat){for(unsigned i=0;i<4;++i){const auto key=(d.resource&0xffff0000u)|i;fog_pins[i]=hmm_resource_pin(key);fog[i]=hmm_resource_get(key).image();}}
        else if(!d.fill){previous_pin=hmm_resource_pin(d.resource);const auto &original=hmm_resource_get(d.resource);
            const int left=d.step<0?d.sx-d.r.w+1:d.sx;const bool cropped=hmm_resource_region(d.resource,left,d.sy,d.r.w,d.r.h,region);
            const auto &in=cropped?region:original;if(cropped){d.sx=d.step<0?d.r.w-1:0;d.sy=0;}
            if(d.color){raster_alpha(in,d,im.width(),p);hmm_resource_restore_pin(d.resource,previous_pin);continue;}
            src=in.image();sw=in.width();if(!in.singleLayer())transform=in.transform();}
        for(int y=0;y<d.r.h;++y){
            // Codex: длинное CPU-восстановление фона обслуживает звук порциями строк.
            if(!(y&15))hmm_audio_poll();
            const int line=(d.r.y+y)*im.width()+d.r.x;
            // Codex: точная копия непрозрачных строк без проверки маски на каждом пикселе.
            if(d.fill&&!d.mask){
                std::memset(p+line,d.mapping?mappings[d.mapping-1][d.color]:d.color,d.r.w);
                if(mask)std::memset(mask+line,0,d.r.w);continue;
            }
            if(!d.fill&&!d.repeat&&(!transform||d.raw)&&d.step==1&&!d.mapping){
                std::memcpy(p+line,src+(d.sy+y)*sw+d.sx,d.r.w);
                if(mask)std::memset(mask+line,0,d.r.w);continue;
            }
            for(int x=0;x<d.r.w;++x){
            const int pos=(d.r.y+y)*im.width()+d.r.x+x,sx=d.sx+x*d.step,sy=d.sy+y;
            const int at=d.repeat?((sy&31)*32+(sx&31)):sy*sw+sx;
            const auto *pixels=d.repeat?fog[((sx>>5)+(sy>>5))&3]:src;
            uint8_t t=d.fill?d.mask:!transform||d.raw?0:transform[at];
            uint8_t color=d.fill?d.color:pixels[at];if(d.mapping)color=mappings[d.mapping-1][color];
            if(t==1||(t>1&&!d.fill&&d.mapping&&mapping_solid[d.mapping-1]))continue;
            if(t>1&&(!mask||!mask[pos]))p[pos]=table[t*256+p[pos]];
            else {p[pos]=color;if(mask)mask[pos]=window&&d.fill&&t>=2&&t<=5?uint8_t(t+14):t;}
        }
        }
        // Codex: уже использованный слой можно вытеснить. Ранее закреплённые входы вызывающего сохраняем.
        if(d.repeat)for(unsigned i=0;i<4;++i)hmm_resource_restore_pin((d.resource&0xffff0000u)|i,fog_pins[i]);
        else if(!d.bitmap&&!d.fill)hmm_resource_restore_pin(d.resource,previous_pin);
        if(d.bitmap)bitmap_release_pixels(*d.bitmap);
    }
}
// Codex: пиксели участка нужны только при промахе RAM_G или чтении процессором.
HMM_SIZE_RENDER void bitmap_materialize(Bitmap &b){
    if(!b.frame||!b.pixels.empty())return;
    // Codex: один буфер на глубину вложенной растеризации, а не на каждую клетку.
    if(b.pixels.empty()&&!tile_pixel_buffers.empty()){b.pixels.swap(tile_pixel_buffers.back());tile_pixel_buffers.pop_back();}
    // Клод: клетка растеризуется целиком, 32×32, а не только видимая в окне часть: крайняя клетка не меняется при сдвиге карты
    // на несколько точек (cache_map_tiles). Вне окна её пикселей не видно — вывод обрезает её по окну.
    Canvas source;source.draws.reserve(b.paint_count);const auto visible=b.tile_rect;
    const auto &draws=b.frame->map_source?*b.frame->map_source:b.frame->draws;
    for(unsigned i=0;i<b.paint_count;++i){auto d=clip(draws[b.frame->layers[b.paint_begin+i]],visible);if(!d.r.w||!d.r.h)continue;
        d.r.x-=b.tile_rect.x;d.r.y-=b.tile_rect.y;source.draws.push_back(std::move(d));}
    occlude(source.draws);
    unsigned common=source.draws.empty()?0:source.draws.front().mapping;
    for(const auto &d:source.draws)if(d.mapping!=common){common=0;break;}
    if(common)for(auto &d:source.draws)d.mapping=0;
    struct TileImage:Image{TileImage(int w,int h,bool single){_hmmSetDimensions(w,h,single);}}image(b.w,b.h,b.single);b.pixels.resize(size_t(b.w)*b.h*(b.single?1:2));
    struct Guard{bool saved=materializing;Guard(){materializing=true;}~Guard(){materializing=saved;}}guard;
    raster(image,source,b.pixels.data(),{0,0,b.w,b.h});
    if(common)for(auto &pixel:b.pixels)pixel=mappings[common-1][pixel];
}
}
void hmm_gpu_enter(){++depth;}
bool hmm_gpu_active(){return ready;}
HMM_SIZE_RENDER bool hmm_gpu_message_begin(const fheroes2::Image *out){
    return hmm_gpu_message_begin(out,true);
}
HMM_SIZE_RENDER bool hmm_gpu_message_begin(const fheroes2::Image *out,bool popup){
    if(!ready||materializing||map_active||(out&&out!=&fheroes2::Display::instance()))return false;
    ring_trim_pending();
    screen_canvas();
    auto layer=std::unique_ptr<MessageSurface>(new MessageSurface);
    Canvas c;changed(c);canvases.emplace(layer.get(),std::move(c));
    message_layers.push_back(std::move(layer));message_target=message_layers.back().get();
    message_fade_open(popup);
    return true;
}
HMM_SIZE_RENDER void hmm_gpu_message_end(){
    message_fade_close();
    message_target=message_layers.size()>1?message_layers[message_layers.size()-2].get():nullptr;
    message_layers.pop_back();
}
void hmm_gpu_leave(){
    if(!depth||--depth)return;
    for(const Image *im:writers){auto it=canvases.find(im);if(it!=canvases.end())flush(*it->first,it->second);}
    writers.clear();
}
void hmm_gpu_drop(const fheroes2::Image &im){
    if(!ready)return;
    if(&im==region_target){region_bump();region_owner=nullptr;region_target=nullptr;}
    if(&im==map_target){map_target=nullptr;map_active=false;map_draws.clear();map_cache_clear();}
    canvases.erase(&im);
}
void hmm_gpu_move(const fheroes2::Image &a,const fheroes2::Image &b){
    if(!ready||!hmm_running||hmm_exception_pending())return;
    if(&a==region_target||&b==region_target)region_bump();
    // Клод: список экрана с заместителем карты переезжает только развёрнутым.
    if(&a==map_target||&b==map_target){map_why="move";map_materialize();}
    auto ai=canvases.find(&a),bi=canvases.find(&b);
    if(ai!=canvases.end())changed(ai->second);if(bi!=canvases.end())changed(bi->second);
    // Клод: если переезжает список, в который пишет процессор, его запись надо сбросить на выходе
    // уже под новым изображением (перемещения вне операций не должны копить список writers).
    if((ai!=canvases.end()&&ai->second.writing)||(bi!=canvases.end()&&bi->second.writing))
        for(auto &writer:writers){if(writer==&a)writer=&b;else if(writer==&b)writer=&a;}
    if(ai!=canvases.end()&&bi!=canvases.end())std::swap(ai->second,bi->second);
    else if(ai!=canvases.end()){auto node=canvases.extract(ai);node.key()=canvas_key(&b);canvases.insert(std::move(node));}
    else if(bi!=canvases.end()){auto node=canvases.extract(bi);node.key()=canvas_key(&a);canvases.insert(std::move(node));}
}
/* Клод: виден ли заместитель карты (слой layer) в участке r — или его там закрывают непрозрачные слои поверх (фон окна).
 * Прежде любое обращение процессора к точкам Display разворачивало карту целиком (map_materialize): тысячи слоёв спрайтов
 * вместо готовых клеток плотной карты, и следующий кадр растеризовал их заново со спрайтами с SD. Окно поверх карты (загрузка,
 * сохранение, сообщения) правит точки только на своём фоне — карта под ним не нужна. */
// Закрыт — если его часть в r целиком внутри одного непрозрачного слоя поверх (фон окна — один Copy). Иначе карта разворачивается, как прежде.
bool map_visible_in(const Canvas &c,Rect r){
    const auto &v=c.draws;
    for(size_t i=0;i<v.size();++i){if(!v[i].layer)continue;const auto q=intersect(v[i].r,r);if(!q.w||!q.h)continue;
        bool hidden=false;
        for(size_t k=i+1;k<v.size()&&!hidden;++k){const auto h=intersect(v[k].r,q);hidden=v[k].opaque&&h.w==q.w&&h.h==q.h;}
        if(!hidden)return true;
    }
    return false;
}
HMM_SIZE_RENDER void hmm_gpu_pixels(const fheroes2::Image &im,uint8_t *p,bool write){
    Prof prof(P_PIXELS);
    if(!ready||materializing||im.empty())return;
    // Клод: у map_target список слоёв есть всегда (создаёт hmm_gpu_map_view, hmm_gpu_drop сбрасывает map_target).
    auto it=canvases.find(&im);if(it==canvases.end())return;
    auto &c=it->second;if(c.writing)return;
    Rect r=write&&c.pending.w&&c.pending.h?c.pending:bounds(im);
    // Клод: поточечные записи CPU (SetTransformPixel перед каждой точкой) сливаются с только что
    // снятым CPU-участком в один прямоугольник до 16384 точек. Иначе каждая точка была отдельным
    // слоем: тень клетки боя (DrawHexagonShadow) — сотни слоёв, а на поле — десятки тысяч
    // (bad_alloc в QEMU). Участок растеризуется заново целиком, поэтому слияние всегда точно.
    if(write&&!c.draws.empty()&&c.draws.back().cpu){
        const Rect a=c.draws.back().r;Rect u;
        u.x=std::min(a.x,r.x);u.y=std::min(a.y,r.y);
        u.w=std::max(a.x+a.w,r.x+r.w)-u.x;u.h=std::max(a.y+a.h,r.y+r.h)-u.y;
        if(u.w*u.h<=16384)r=u;
    }
    // Клод: карта разворачивается, только если она видна в участке, который процессор будет читать или писать (r — уже итоговый).
    if(&im==map_target&&map_visible_in(c,r)){map_why=write?"cpu write":"cpu read";map_why_rect=r;map_materialize();}
    if(write)region_write(im,r);
    c.touched=canvas_frame;
#ifdef HMM2_HOST
    /* Клод: на ПК (HMM2_RASTER_LOG) — каждый растр процессором картинки от 32 768 точек: размер, участок и стек вызовов (адреса
     * относительно начала программы, разбор по hmm2_host.map). Так находятся разом все места, где игра просит процессор нарисовать
     * большую картинку в пиксели (книга заклинаний: 348 КБ, сборки 94–95 на плате). */
    if(std::getenv("HMM2_RASTER_LOG")&&size_t(im.width())*size_t(im.height())>=32768){
        void *frames[14];const unsigned n=RtlCaptureStackBackTrace(1,14,frames,nullptr);const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        std::printf("CPU RASTER %dx%d layers=%d write=%d r=%d,%d,%d,%d stack",im.width(),im.height(),im.singleLayer()?1:2,int(write),r.x,r.y,r.w,r.h);
        for(unsigned i=0;i<n;++i)std::printf(" %llx",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i])-base));
        std::printf("%c",10);std::fflush(stdout);
    }
#endif
    materializing=true;raster(im,c,p,r);materializing=false;
    if(write){c.pending=r;c.writing=true;c.pixels=p;writers.push_back(&im);}
}
HMM_SIZE_RENDER void hmm_gpu_touch(fheroes2::Image &im,int x,int y,int w,int h){
    if(!ready||materializing||im.empty())return;
    region_write(im,{x,y,w,h});
    auto &c=canvas(im);c.pending=intersect({x,y,w,h},bounds(im));
}
void hmm_gpu_raw(){}
/* Клод: mask 2–9 — ApplyTransform (тень окна, подложка под текстом, рамки) слоем FT812: заливка с маской-преобразованием, слои под
 * ней остаются (без cover), FT812 рисует полупрозрачный прямоугольник (emit). Прежде преобразование читало пиксели Display: каждое
 * обращение разворачивало всю карту под окном (map_materialize) и растеризовало её спрайты процессором — окно загрузки на Alteris
 * стояло десятки секунд и читало 31 МБ с SD. Растеризатор CPU (raster) понимает такой слой той же таблицей, что исходный
 * ApplyTransform. Таблицы 10–15 (цветные), двухслойные изображения и изображения без списка слоёв — прежним путём CPU. */
HMM_SIZE_RENDER bool hmm_gpu_fill(fheroes2::Image &im,int x,int y,int w,int h,uint8_t color,uint8_t mask){
    if(hmm_exception_pending())return true;
    Prof prof(P_FILL);
    if(!ready||materializing)return false;
    const bool shade=mask>=2;
    if(shade&&(mask>9||!im.singleLayer()||canvases.find(&im)==canvases.end()))return false;
    auto r=intersect({x,y,w,h},bounds(im));if(!r.w||!r.h)return true;
    region_write(im,r);if(!mask)note_map_cover(im,r);
    Draw d;d.r=r;d.fill=true;d.opaque=mask==0;d.color=color;d.mask=mask;
    // Клод: в режиме карты заливка — слой карты; cover не нужен, окно заменяется целиком.
    if(map_active&&&im==map_target){map_record(std::move(d));return true;}
    auto &c=canvases[&im];flush(im,c);if(!shade)cover(c,r,mask!=0);
    c.draws.push_back(std::move(d));changed(c);return true;
}
// Клод: у изображения есть список слоёв (снимок узора рамки в кэше hmmDitherLayer ещё действителен).
bool hmm_gpu_tracked(const fheroes2::Image &im){return ready&&canvases.find(&im)!=canvases.end();}
// Клод: out — изображение со списком слоёв (обычно Display): туда рисуем слоями FT812, а не точками CPU.
bool hmm_gpu_layer_target(const fheroes2::Image &out){return ready&&!materializing&&out.singleLayer()&&canvases.find(&out)!=canvases.end();}
// Клод: участок чисто процессорной работы над временными изображениями — перехваты GPU в нём молчат (как при растеризации).
bool hmm_gpu_cpu_begin(){const bool saved=materializing;materializing=true;return saved;}
bool hmm_gpu_fade_ready(int x,int y,int w,int h){return ready&&!materializing&&x<=0&&y<=0&&x+w>=640&&y+h>=480&&canvases.find(&fheroes2::Display::instance())!=canvases.end();}
void hmm_gpu_cpu_end(bool saved){materializing=saved;}
// Клод: кадр ролика — непрозрачный слой fill+raw; растеризатор CPU видит его заливкой, а DL рисует битмап.
void hmm_gpu_clip_draw(fheroes2::Image &im,int x,int y,int w,int h,unsigned slot){
    if(!ready||materializing)return;
    const auto r=intersect({x,y,w,h},bounds(im));if(!r.w||!r.h)return;
    region_write(im,r);
    Draw d;d.r=r;d.fill=true;d.raw=true;d.opaque=true;d.color=uint8_t(slot);d.sx=r.x-x;d.sy=r.y-y;
    auto &c=canvases[&im];flush(im,c);cover(c,r,false);
    c.draws.push_back(std::move(d));changed(c);
}
// Codex: геометрия окна вычислена самим GameArea. Необычный источник или эффект
// возвращается в исходные AlphaBlit/Copy; слой и отражение имеют ту же семантику.
/* Клод: alpha 1…254 — затухающий объект карты (BlitOnTile: герой, монстр после боя, лодка) — слой FT812 с COLOR_A, как у
 * hmm_gpu_alpha. Прежде такой AlphaBlit в режиме карты шёл процессором и разворачивал всю карту на каждом кадре затухания. */
bool hmm_gpu_map_sprite(const Image &in,int ix,int iy,Image &out,int ox,int oy,int w,int h,bool flip,bool copy,uint8_t alpha){
    if(!map_active||&out!=map_target)return false;
    if(w<=0||h<=0||!alpha)return true;
    if(ix<0||iy<0||ix+w>in.width()||iy+h>in.height())return false;
    uint32_t key;if(!hmm_resource_id(in,key)||(!copy&&(hmm_resource_flags(in)&2)))return false;
    Draw d;d.r={ox,oy,w,h};d.sx=flip?in.width()-ix-1:ix;d.sy=iy;d.step=flip?-1:1;d.resource=key;
    d.raw=copy&&out.singleLayer();d.opaque=d.raw||in.singleLayer();
    if(alpha!=255){d.color=alpha;d.raw=d.opaque=false;}
    // Codex: обрезанные края не заставляют пересобирать внутреннюю полосу тайлов при сдвиге.
    const int kind=map_kind;if(!kind&&copy&&(ix||iy||w!=in.width()||h!=in.height()))map_kind=3;
    map_record(std::move(d));map_kind=kind;return true;
}
HMM_SIZE_RENDER bool hmm_gpu_offer(const fheroes2::Image &in,int ix,int iy,fheroes2::Image &out,int ox,int oy,int w,int h,bool flip,bool copy,const uint8_t *map){
    if(hmm_exception_pending())return true;
    {static unsigned loading_ticks;if(!(++loading_ticks&15))hmm_loading_tick();}

    Prof prof(P_OFFER);
    if(!ready||materializing||w<=0||h<=0)return false;
    region_write(out,{ox,oy,w,h});if(&in!=&out&&(copy||in.singleLayer()))note_map_cover(out,{ox,oy,w,h});
    uint32_t key;const bool source_resource=hmm_resource_id(in,key);
    if(source_resource&&!copy&&(hmm_resource_ft(key,false).flags&2)){
        hmm_gpu_touch(out,ox,oy,w,h);return false;
    }
    if(map && &in!=&out && !in.singleLayer()){hmm_gpu_touch(out,ox,oy,w,h);return false;}
    const int left=flip?in.width()-ix-w:ix;
    // Codex: ImageRestorer панелей справа не читает окно карты и не разворачивает его слои.
    // Codex: независимый снимок читает слои заместителя, не разрушая живой DL карты.
    // Самокопирование и чтение в середине Redraw по-прежнему завершают режим карты.
    if(&in==map_target&&(&in==&out||map_active)){const auto hit=intersect({left,iy,w,h},map_bounds);if(hit.w&&hit.h){map_why="copy";map_why_rect={left,iy,w,h};map_materialize();}}
    // Клод: таблица для одной и той же прежней таблицы слоя считается один раз на вызов, а не на каждый слой.
    unsigned memo_prior=~0u,memo_id=0;
    auto place=[&](Draw d,Sequence<Draw> &to){
        d=clip(d,{left,iy,w,h});if(!d.r.w||!d.r.h)return;
        const int delta=d.r.x-left;
        if(flip){d.sx+=(d.r.w-1)*d.step;d.step=-d.step;d.r.x=ox+w-delta-d.r.w;}else d.r.x=ox+delta;
        d.r.y=oy+d.r.y-iy;
        if(copy&&out.singleLayer()&&source_resource){d.raw=true;d.opaque=true;}
        if(map){if(d.mapping!=memo_prior){memo_prior=d.mapping;memo_id=mapping_id(map,d.mapping);}d.mapping=memo_id;}
        if(&to==&map_draws)map_record(std::move(d));else to.push_back(std::move(d));
    };
    // Клод: режим карты — слои Redraw прямо в map_draws, без cover. Кадр AGG даёт один слой без поиска
    // его списка в canvases (тот же Draw, что построил бы canvas()).
    if(map_active&&&out==map_target&&&in!=&out){
        if(source_resource){Draw d;d.r=bounds(in);d.resource=key;d.opaque=in.singleLayer();if(d.r.w&&d.r.h)place(d,map_draws);}
        else for(const auto &d:canvas(in).draws)place(d,map_draws);
        return true;
    }
    // StoredSprite is immutable, so one resource draw describes it completely.
    // Do not retain a separate Canvas and Draw allocation for every visited
    // animation frame/font glyph over a long game or battle.
    if(source_resource&&&in!=&out){
        auto &c=canvas(out);
        if(copy||in.singleLayer())cover(c,{ox,oy,w,h},!out.singleLayer());
        Draw d;d.r=bounds(in);d.resource=key;d.opaque=in.singleLayer();
        place(std::move(d),c.draws);compact(out,c);changed(c);return true;
    }
    // Codex: независимый источник не меняется во время Copy/Blit. Снимок списка
    // нужен только при копировании изображения в себя (например, палитровый fade).
    auto &input_canvas=canvas(in);
    const bool whole=!flip&&(copy||in.singleLayer()||(map&&&in==&out))&&ix==0&&iy==0&&ox==0&&oy==0&&w==in.width()&&h==in.height()&&w==out.width()&&h==out.height();
    bool layered=false;
    if(whole)for(const auto &d:input_canvas.draws)if(d.layer){layered=true;break;}
    // Codex: исходный fade меняет отображение цветов полного снимка. Геометрия подготавливается один раз.
    if(map&&whole&&&in!=&out&&&out==&fheroes2::Display::instance()&&!layered&&!input_canvas.prepared){
        occlude(input_canvas.draws);repeat_fog(input_canvas.draws);join_layers(input_canvas.draws);input_canvas.prepared=true;
    }
    const bool prepared=whole&&input_canvas.prepared&&!layered;
    // Codex: полный Copy не меняет координаты. Сам снимок копируется линейно,
    // а ApplyPalette обновляет только таблицы исходных слоёв, без cover/clip каждого спрайта.
    if(whole&&!layered&&!source_resource){
        if(&in==&out&&!map)return true;
        auto &c=canvas(out);if(&in!=&out)c.draws=input_canvas.draws;
        if(map)for(auto &d:c.draws){if(d.mapping!=memo_prior){memo_prior=d.mapping;memo_id=mapping_id(map,d.mapping);}d.mapping=memo_id;}
        changed(c);c.prepared=prepared;return true;
    }
    const auto &input=input_canvas.draws;Sequence<Draw> saved;
    for(const auto &d:input)if(d.layer){const auto hit=intersect(d.r,{left,iy,w,h});if(hit.w&&hit.h){ring_expand();break;}}
    if(&in==&out)saved=input;
    const auto &source=&in==&out?saved:input;auto &c=canvas(out);
#ifdef HMM2_HOST
    if(std::getenv("HMM2_CANVAS_DIAGNOSTIC")&&(source.size()>2000||c.draws.size()>4000))
        std::printf("CANVAS in=%dx%d/%zu out=%dx%d/%zu rect=%d,%d,%d,%d copy=%d self=%d map=%d\n",in.width(),in.height(),source.size(),out.width(),out.height(),c.draws.size(),ox,oy,w,h,int(copy),int(&in==&out),int(map!=nullptr));
#endif
    const Rect dest{ox,oy,w,h};
    if(copy||in.singleLayer()||(&in==&out&&map))cover(c,dest,!out.singleLayer());
    const bool clean=c.draws.empty();
    // Codex: восстановление окна добавляет много слоёв разом. Точный размер вместо
    // удвоения vector не требует лишнего непрерывного блока PSRAM (GateHell: 53 976 байт).
    // Быстрый путь записи карты выше уже завершился; обычные ресурсные Blit не считают слои.
    if(!source_resource){
        size_t added=0;const Rect region{left,iy,w,h};
        for(const auto &d:source){
            const auto hit=intersect(d.r,region);if(!hit.w||!hit.h)continue;
            if(d.layer){for(const auto &m:map_draws){const auto part=intersect(m.r,hit);added+=part.w&&part.h;}}
            else ++added;
        }
        const size_t need=c.draws.size()+added;
        if(need>c.draws.capacity())c.draws.reserve(need);
    }
    for(const auto &d:source){
        if(d.layer){const auto hit=intersect(d.r,{left,iy,w,h});if(hit.w&&hit.h)for(const auto &m:map_draws)place(clip(m,hit),c.draws);}
        else place(d,c.draws);
    }
    compact(out,c);
    changed(c);c.prepared=prepared&&clean;
    return true;
}
// Клод: AlphaBlit спрайта ресурса с постоянной альфой (1…254) — слой FT812 с COLOR_A вместо растеризации участка на CPU.
// Раньше каждый такой вызов разворачивал слои экрана под спрайтом в пиксели, смешивал их процессором и заливал новую
// текстуру: окно пазла (26 кадров по 50 мс, каждый открытый кусок) открывалось на плате очень долго.
// Смешение FT812 — src·a + dst·(1−a), как у оригинала; тени (прозрачность 2…5) дают то же dst·(1−s·a).
// Слой не закрывает лежащих под ним (opaque=false). Режим карты, свой источник и особые ресурсы — прежним путём.
HMM_SIZE_RENDER bool hmm_gpu_alpha(const Image &in,int ix,int iy,Image &out,int ox,int oy,int w,int h,uint8_t alpha,bool flip){
    if(hmm_exception_pending())return true;
    if(!ready||materializing||w<=0||h<=0||&in==&out||alpha==0||alpha==255||in.empty())return false;
    uint32_t key;const bool resource=hmm_resource_id(in,key);
    if((resource&&(hmm_resource_ft(key,false).flags&2))||(map_active&&&out==map_target))return false;
    region_write(out,{ox,oy,w,h});
    // Та же геометрия, что у place в hmm_gpu_offer: окно источника, отражение, место назначения.
    const int left=flip?in.width()-ix-w:ix;
    Draw d;d.r=bounds(in);
    /* Клод: составное изображение (панель героя при входе в замок, вывеска капитана) — одним снимком его собственных слоёв:
     * альфа ложится на готовую картинку, как у оригинала. Прежде AlphaBlit такого изображения шёл полосами CPU по 16 строк поверх
     * фона замка (крупнее 128 КБ — каждая полоса распаковывала его кусок с SD с начала потока). Одинаковый снимок — та же текстура. */
    if(resource)d.resource=key;else d.bitmap=capture_pixels(in,in.image(),d.r);
    d=clip(d,{left,iy,w,h});if(!d.r.w||!d.r.h)return true;
    const int delta=d.r.x-left;
    if(flip){d.sx+=(d.r.w-1)*d.step;d.step=-d.step;d.r.x=ox+w-delta-d.r.w;}else d.r.x=ox+delta;
    d.r.y=oy+d.r.y-iy;d.color=alpha;d.opaque=false;
    auto &c=canvas(out);c.draws.push_back(std::move(d));compact(out,c);changed(c);
    return true;
}
/* Клод: контур отряда в бою (CreateContour + Blit оригинала) — слоями FT812 из того же атласа, без CPU. Контур — прозрачные и
 * теневые точки спрайта, соседние по стороне с непрозрачной. Слои: силуэт спрайта (все цвета → цвет контура, тени и
 * осветление прозрачны) со сдвигами на точку влево/вправо/вверх/вниз, обрезанный рамкой спрайта, и поверх — сам спрайт без теней:
 * внутренние точки возвращают свой цвет, а соседние снаружи остаются цветом контура, как в оригинале. Прежде контур каждый кадр
 * строился процессором из пикселей спрайта (распаковка с SD, 10–20 КБ на кадр в журнале платы) и уходил новой текстурой.
 * false — прежний путь (не ресурс, особые точки, режим карты). Отряд под ним уже нарисован вызывающим (непрозрачно). */
HMM_SIZE_RENDER bool hmm_gpu_contour(const Image &in,Image &out,int ox,int oy,bool flip,uint8_t color){
    if(!ready||materializing||&in==&out||in.singleLayer()||(map_active&&&out==map_target)||canvases.find(&out)==canvases.end())return false;
    uint32_t key;if(!hmm_resource_id(in,key)||(hmm_resource_ft(key,false).flags&2))return false;
    const int w=in.width(),h=in.height();if(w<2||h<2)return true;
    region_write(out,{ox,oy,w,h});
    std::array<uint8_t,256> table;table.fill(color);
    const unsigned outline=mapping_id(table.data(),0,true);
    for(unsigned i=0;i<256;++i)table[i]=uint8_t(i);
    const unsigned body=mapping_id(table.data(),0,true);
    Draw d;d.r={ox,oy,w,h};d.resource=key;if(flip){d.sx=w-1;d.step=-1;}
    auto &c=canvas(out);
    static const int8_t shift[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
    for(const auto &s:shift){Draw e=d;e.r.x+=s[0];e.r.y+=s[1];e=clip(e,d.r);if(e.r.w&&e.r.h){e.mapping=outline;c.draws.push_back(std::move(e));}}
    d.mapping=body;c.draws.push_back(std::move(d));compact(out,c);changed(c);return true;
}
uint32_t hmm_gpu_region_begin(const void *owner,const fheroes2::Image &image,int x,int y,int w,int h){
    const Rect r{x,y,w,h};
    if(owner!=region_owner||&image!=region_target||std::memcmp(&r,&region_rect,sizeof(Rect))){region_bump();region_owner=owner;region_target=&image;region_rect=r;}
    region_prepared=canvas(image).prepared;return region_revision;
}
uint32_t hmm_gpu_region_end(bool preserve){
    if(preserve&&region_prepared&&region_target)canvas(*region_target).prepared=true;
    return region_revision;
}
void hmm_gpu_map_view(fheroes2::Image &out,int x,int y,int w,int h,int ox,int oy,const void *owner,int flags,bool stable,int tile_x,int tile_y){
    if(!ready)return;
    merge_changed_count=0;merge_geometry_reset=false;
    map_clips_clear();
    auto &c=canvas(out);const Rect win{x,y,w,h};bool keep=false,preserve=c.prepared;
    // Codex: после закрытого экрана боя/города начинается полный Redraw карты.
    // canvas уже сохранил завершённую CPU-запись в слоях; буфер Display от fade
    // (307200 байт в аварийной трассе) здесь больше не удерживается.
    if(map_released&&&out==&fheroes2::Display::instance()){out._hmmRelease();release_frame_storage();}
    region_write(out,win,false);
    // Codex: Runtime_UpdateTranslate Z80: точное перемещение неизменившегося фрагмента
    // меняет только VERTEX_TRANSLATE. Обрезка краёв/новые тайлы выявляются полным сравнением.
    map_shift_x=map_origin_x-ox+x-map_view_x;map_shift_y=map_origin_y-oy+y-map_view_y;
    map_origin_x=ox;map_origin_y=oy;map_view_x=x;map_view_y=y;
    // Codex: тот же заместитель сохраняет место среди независимых слоёв интерфейса.
    // Анимация карты не делает интерфейс грязным. Перекрытие окном идёт прежним путём.
    if(&out==map_target)for(size_t i=c.draws.size();i--;)if(c.draws[i].layer){
        const Rect old=c.draws[i].r;keep=old.x==x&&old.y==y&&old.w==w&&old.h==h;
        for(size_t k=i+1;keep&&k<c.draws.size();++k){auto q=intersect(c.draws[k].r,win);if(q.w&&q.h)keep=false;}
        if(!keep){const auto q=intersect(old,win);
            if(q.w==old.w&&q.h==old.h){c.draws.erase(c.draws.begin()+i);changed(c);}else {map_why="view";map_why_rect=q;map_materialize();preserve=false;}}
        break;
    }
    map_static_allowed=map_animation_only&&map_plan_valid&&stable&&keep&&map_plan_owner==owner&&map_plan_flags==flags&&map_plan_tile_x==tile_x&&map_plan_tile_y==tile_y;
    map_plan_owner=owner;map_plan_flags=flags;map_plan_tile_x=tile_x;map_plan_tile_y=tile_y;map_plan_stable=stable;map_span_position=0;map_span_stack.clear();map_window=win;
    if(!keep){cover(c,win,false);if(&out==&fheroes2::Display::instance()){Draw d;d.r=win;d.layer=true;c.draws.push_back(std::move(d));changed(c);c.prepared=preserve;}}
    if(&out==&fheroes2::Display::instance()){hmm_memory_plan();map_covered=map_released=false;map_target=&out;map_active=true;map_position=map_part_index=map_part_count=0;map_kind=0;map_bounds={};}
    ring_view_reusable=ring_frame&&!ring_expanded;ring_frame=false;
}
bool hmm_gpu_map_animation(bool enabled){const bool old=map_animation_only;map_animation_only=enabled;return old;}
bool hmm_gpu_map_animation_reuse(){return map_animation_only&&map_plan_stable;}
unsigned hmm_gpu_ring_generation(){return ring_generation;}
bool hmm_gpu_ring_subject(const HmmSubjectPart *a,unsigned n,const HmmSubjectPart *b,unsigned m){const bool okay=ring_subject_update(a,n,b,m);
    if(okay){if(n)++ring_subject_reuse;else ++ring_camera_reuse;}else ++ring_subject_retry;return okay;}
bool hmm_gpu_ring_start(const void *owner,int flags,unsigned phase){
    if(!ring_enabled()||!map_active)return false;
    const bool resume=ring_view_reusable&&owner==ring_owner&&flags==ring_flags&&phase==ring_phase&&
        ring_window.x==map_window.x&&ring_window.y==map_window.y&&ring_window.w==map_window.w&&ring_window.h==map_window.h;
    const int oldx=ring_x,oldy=ring_y,oldcols=ring_cols,oldrows=ring_rows;
    if(owner!=ring_owner||flags!=ring_flags){ring_clear();ring_owner=owner;ring_flags=flags;}
    ring_x=ring_floor(map_origin_x);ring_y=ring_floor(map_origin_y);
    ring_cols=ring_floor(map_origin_x+map_window.w-1)-ring_x+1;
    ring_rows=ring_floor(map_origin_y+map_window.h-1)-ring_y+1;
    if(ring_cols>21||ring_rows>16)return false;
    ring_can_resume=resume&&oldx==ring_x&&oldy==ring_y&&oldcols==ring_cols&&oldrows==ring_rows;ring_subject_changed=false;
    map_static_allowed=map_plan_valid=false;map_cache_clear();map_draws.clear();map_spans.clear();map_span_stack.clear();
    ring_collect=true;ring_frame=ring_expanded=false;ring_window=map_window;
    ring_record_order=0;
    ring_resume_dirty_count=0;
    ring_dirty_sum_valid=false;
    ring_deferred=ring_can_resume&&hmm_gpu_map_animation_reuse();
    if(ring_deferred){ring_phase=phase;return true;}
    for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){
        auto &p=ring_cells[(unsigned(y)&15)*32+(unsigned(x)&31)];
        if(!p)p.reset(new RingCell);
        if(p->x!=x||p->y!=y){p->valid=false;p->x=x;p->y=y;}
        if(!p->valid)ring_can_resume=false;
        // Codex: вернувшаяся из-за края клетка могла пропустить несколько фаз.
        // Сверяем её собственную фазу, а не фазу предыдущего видимого кадра.
        p->dirty=!p->valid||p->hero||(phase!=p->phase&&p->animated);p->pending.clear();
        if(p->dirty)ring_resume_dirty[ring_resume_dirty_count++]=uint16_t((unsigned(y)&15)*32+(unsigned(x)&31));
    }
    ring_phase=phase;return true;
}
void hmm_gpu_ring_dirty(int x,int y,int radius){
    if(!ring_collect)return;
    ring_prepare();
    ring_dirty_sum_valid=false;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){auto *p=ring_cell(x+dx,y+dy);if(p)p->dirty=true;}
}
void hmm_gpu_ring_skip(){if(ring_collect)++ring_record_order;}
bool hmm_gpu_ring_need(int x,int y,int radius){
    if(!ring_collect)return true;
    ring_prepare();
    // Codex: один суммарный массив вместо 49 чтений PSRAM на каждый источник
    // tile-unfit. Прямоугольник соседей проверяется четырьмя короткими чтениями.
    if(!ring_dirty_sum_valid){
        ring_dirty_sum.fill(0);
        for(int j=0;j<ring_rows;++j)for(int i=0;i<ring_cols;++i){
            const auto *p=ring_cell(ring_x+i,ring_y+j);const unsigned at=(j+1)*22+i+1;
            ring_dirty_sum[at]=uint16_t((p&&p->dirty)+ring_dirty_sum[at-1]+ring_dirty_sum[at-22]-ring_dirty_sum[at-23]);
        }
        ring_dirty_sum_valid=true;
    }
    const int x0=std::max(0,x-radius-ring_x),y0=std::max(0,y-radius-ring_y);
    const int x1=std::min(ring_cols,x+radius+1-ring_x),y1=std::min(ring_rows,y+radius+1-ring_y);
    return x0<x1&&y0<y1&&ring_dirty_sum[y1*22+x1]-ring_dirty_sum[y0*22+x1]-ring_dirty_sum[y1*22+x0]+ring_dirty_sum[y0*22+x0]!=0;
}
int hmm_gpu_map_static_begin(){
#ifndef HMM2_HOST
    // Codex: неизменные клетки пропускает кольцо; второй кэш проходов не нужен.
    return 0;
#else
    if(ring_enabled())return 0;
    if(!map_active||ring_collect)return 0;
    // Codex: компактные индексы; необычно большой список отключает повторное использование.
    if(map_position>65535||map_part_index>65535||map_part_count>65535)map_static_allowed=map_plan_stable=false;
    const size_t index=map_span_position++;
    const bool existed=index<map_spans.size();
    if(!existed)map_spans.push_back({});
    auto &s=map_spans[index];
    if(existed&&map_static_allowed&&!s.flags&&index+s.children<map_spans.size()&&s.begin==map_position&&s.part==map_part_index&&s.count==map_part_count&&s.end<=map_draws.size()){
        if(map_shift_x||map_shift_y){
            // Codex: внутри того же tileROI двигаются исходные вершины и VERTEX_TRANSLATE.
            // Обрезку окна выполняет SCISSOR, поэтому край не меняет состав статического блока.
            for(size_t i=s.begin;i<s.end;++i){map_draws[i].r.x+=map_shift_x;map_draws[i].r.y+=map_shift_y;}
            for(size_t i=map_part_index;i<=s.end_part&&i<map_parts.size();++i){auto &p=map_parts[i];
                if(p.begin>=s.begin&&p.begin<s.end){p.tx+=map_shift_x;p.ty+=map_shift_y;if(p.fill)p.dirty=true;}}
        }
        map_position=s.end;map_part_index=s.end_part;map_part_count=s.end_count;
        // Codex: консервативная граница окна безопасна для перекрывающих его диалогов.
        map_bounds=map_window;map_span_position+=s.children;return -int(index+1);
    }
    s.begin=map_position;s.part=map_part_index;s.count=map_part_count;s.flags=map_kind==1;map_span_stack.push_back(index);return int(index+1);
#endif
}
void hmm_gpu_map_static_end(int slot){
#ifdef HMM2_HOST
    if(slot<=0)return;
    if(!map_span_stack.empty()&&map_span_stack.back()==unsigned(slot-1))map_span_stack.pop_back();
    if(!map_active||size_t(slot)>map_spans.size())return;
    if(map_position>65535||map_part_index>65535||map_part_count>65535)map_static_allowed=map_plan_stable=false;
    const size_t children=map_span_position-size_t(slot);
    if(children>65535)map_static_allowed=map_plan_stable=false;
    auto &s=map_spans[slot-1];s.children=children;s.end=map_position;s.end_part=map_part_index;s.end_count=map_part_count;
#endif
}
// Клод: туман окна склеивается один раз на перерисовку (а не в каждом кадре вывода).
void hmm_gpu_map_end(){if(map_active){if(ring_collect){ring_commit();map_bounds=map_window;}else map_record_end();map_active=false;}}
int hmm_gpu_map_kind(int kind){const int previous=map_kind;if(map_active){
    map_kind=kind;
    // Codex: участок с любой анимацией исполняется заново; его статические дочерние
    // участки при этом могут сохраняться. Отмечается сам вход, даже если кадр пустой.
    if(kind==1)for(auto i:map_span_stack)if(i<map_spans.size())map_spans[i].flags=1;
}return previous;}
extern "C" void hmm_gpu_history_reset(){/* Codex: живые снимки ImageRestorer нельзя терять при давлении на кучу. */}
// Клод: безопасная точка (перед чтением мира в Game::Load) — указателей на пиксели никто не держит.
// Буфер изображения со списком слоёв — только кэш: следующий image() растеризует его заново из списка.
// Экран и окна меню отдают так около 300 КиБ, которых не хватало загрузке сохранения GateHell.
extern "C" void hmm_gpu_release_pixels(){
    if(!ready)return;
    ring_clear();hmm_ring_world_clear();
    tile_workspace.reset();
    tiles_forget();map_tiles_clear();
    release_frame_storage();Sequence<CullRect>().swap(cull_rects);Sequence<CullRect>::release_unused();
    for(auto &item:canvases){flush(*item.first,item.second);item.first->_hmmRelease();}
}
// Codex: аварийный резерв — только свободные страницы рендера. Активные кадры,
// снимки ImageRestorer и области спрайтов/DAT/звука остаются действительными.
/* Клод: буфер точек изображения со списком слоёв — только кэш: каждое обращение процессора растеризует нужное заново. Прежде он
 * жил до загрузки следующего мира: на плате (сборка 93, 04.11) такие буферы выросли до 705 КБ, свободная PSRAM упала до 84 КБ,
 * и запрос 348 КБ на плотной карте завершил игру. Буфер изображения, к которому процессор не обращался два кадра (и в которое
 * сейчас не пишет), отдаётся — в начале кадра и при нехватке памяти. Изображение текущей операции тронуто в этом кадре. */
void canvases_release_idle(){
    for(auto &item:canvases){auto &c=item.second;
        if(c.writing||!item.first->_hmmPeek()||canvas_frame-c.touched<2)continue;
        if(size_t(item.first->width())*size_t(item.first->height())<4096)continue;
        item.first->_hmmRelease();}
}
extern "C" void hmm_gpu_trim_unused(){ring_trim_pending();release_frame_storage();Sequence<CullRect>::release_unused();if(ready)canvases_release_idle();}
extern "C" void *hmm_gpu_heap_page(size_t bytes){return Sequence<Draw>::reuse_page(bytes);}
// Клод: сводка памяти слоёв для журнала нехватки PSRAM (hmm_cache_trim): число списков слоёв,
// байты их векторов, байты живых снимков CPU и пиксельных буферов изображений со списком слоёв.
HMM_SIZE_RENDER static void gpu_memory(unsigned *lists,unsigned *draw_bytes,unsigned *bitmap_bytes,unsigned *pixel_bytes,bool quiet){
    size_t d=0,b=0,p=0;
    d+=map_draws.capacity()*sizeof(Draw)+map_parts.capacity()*sizeof(MapPart)+map_spans.capacity()*sizeof(MapSpan);
    for(const auto &part:map_parts)d+=part.commands.capacity()*sizeof(uint32_t)+part.textures.capacity()*sizeof(uint32_t);
    for(const auto &item:canvases){d+=item.second.draws.capacity()*sizeof(Draw);
        if(item.first->_hmmPeek())p+=size_t(item.first->width())*item.first->height()*(item.first->singleLayer()?1:2);}
    for(const auto &w:bitmaps)if(auto x=w.lock())b+=x->pixels.capacity()+sizeof(Bitmap);
    if(quiet){
        char m[48];std::snprintf(m,sizeof(m),"HMM2 MEMR mid=%u/%u",unsigned(hmm_heap_mid_bytes),unsigned(hmm_heap_mid_peak));hmm_log(m);
    }
    *lists=unsigned(canvases.size());*draw_bytes=unsigned(d);*bitmap_bytes=unsigned(b);*pixel_bytes=unsigned(p);
}
extern "C" void hmm_gpu_memory(unsigned *l,unsigned *d,unsigned *b,unsigned *p){gpu_memory(l,d,b,p,false);}
extern "C" void hmm_gpu_memory_quiet(unsigned *l,unsigned *d,unsigned *b,unsigned *p){gpu_memory(l,d,b,p,true);}
