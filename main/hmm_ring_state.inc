/* Codex: кольцо хранит исходные команды клетки в координатах карты. Старый
 * список нужен лишь при чтении Image процессором; штатный кадр его не собирает. */
// Codex: координаты фрагмента внутри клетки 0…32, координаты ресурса 0…2048.
// Узкая запись экономит PSRAM; перед исполнением восстанавливается обычный Draw.
struct RingDraw {
    uint32_t resource=0,mapping=0,order=0;int16_t sx=0,sy=0;
    uint8_t x=0,y=0,w=0,h=0;int8_t step=1;
    bool raw:1,opaque:1,fill:1,repeat:1,cpu:1,layer:1;uint8_t color=0,mask=0;
    std::array<int16_t,4> original{};std::shared_ptr<Bitmap> bitmap;
    explicit RingDraw(const Draw &d):resource(d.resource),mapping(d.mapping),sx(int16_t(d.sx)),sy(int16_t(d.sy)),
        x(uint8_t(d.r.x)),y(uint8_t(d.r.y)),w(uint8_t(d.r.w)),h(uint8_t(d.r.h)),step(int8_t(d.step)),
        raw(d.raw),opaque(d.opaque),fill(d.fill),repeat(d.repeat),cpu(d.cpu),layer(d.layer),color(d.color),mask(d.mask),bitmap(d.bitmap){}
    operator Draw()const{Draw d;d.r={x,y,w,h};d.sx=sx;d.sy=sy;d.step=step;d.resource=resource;d.mapping=mapping;
        d.raw=raw;d.opaque=opaque;d.fill=fill;d.repeat=repeat;d.cpu=cpu;d.layer=layer;d.color=color;d.mask=mask;d.bitmap=bitmap;return d;}
};
struct RingCell {
    std::vector<RingDraw> source,pending;
    uint64_t id=0,live_id=0,base_id=0;
    uint64_t layout=0;
    unsigned bytes=0,tail=0,phase=0;
    uint32_t frame_address=0;bool frame_cached=false;
    int x=0,y=0;
    bool valid=false,usable=false,dirty=false,animated=false,hero=false;
};
std::array<std::unique_ptr<RingCell>,512> ring_cells;
void ring_trim_pending(){for(auto &p:ring_cells)if(p&&p->pending.empty())std::vector<RingDraw>().swap(p->pending);}
uint64_t ring_next_id=1,ring_live_next_id=1,ring_base_next_id=1;
unsigned ring_hits,ring_builds,ring_fallbacks,ring_recorded;
unsigned ring_record_order;
std::array<uint16_t,22*17> ring_dirty_sum;bool ring_dirty_sum_valid;
bool ring_collect,ring_frame,ring_expanded;
const void *ring_owner;int ring_flags;unsigned ring_phase;
unsigned ring_generation=1;
unsigned ring_revision=1;
unsigned ring_contents_revision=1,ring_live_revision,ring_static_revision=1;
unsigned ring_base_revision=1;
bool ring_hero_changed;
bool ring_can_resume,ring_subject_changed,ring_view_reusable,ring_deferred;
unsigned ring_camera_reuse,ring_subject_reuse,ring_subject_retry;
std::array<uint16_t,512> ring_resume_dirty;unsigned ring_resume_dirty_count;
#ifdef HMM2_HOST
unsigned ring_pending_grows;
#endif
int ring_live_x,ring_live_y,ring_live_cols,ring_live_rows;
Sequence<Draw> ring_live;
void body_plan_clear();
void ring_clusters_clear(bool);
void ring_sprite_plan_clear();
int ring_x,ring_y,ring_cols,ring_rows;
int ring_frame_tx,ring_frame_ty;
Rect ring_window;
void ring_clear(){ring_deferred=ring_can_resume=ring_view_reusable=ring_subject_changed=false;body_plan_clear();ring_clusters_clear(true);ring_sprite_plan_clear();ring_live.clear();ring_live_revision=0;for(auto &p:ring_cells)p.reset();ring_collect=ring_frame=ring_expanded=false;ring_owner=nullptr;++ring_generation;++ring_revision;++ring_contents_revision;}
int ring_floor(int v){return v>=0?v/32:-((-v+31)/32);}
RingCell *ring_cell(int x,int y){
    auto &slot=ring_cells[(unsigned(y)&15)*32+(unsigned(x)&31)];return slot&&slot->x==x&&slot->y==y?slot.get():nullptr;
}
HMM_SIZE_RENDER void ring_prepare(){
    if(!ring_deferred)return;ring_deferred=false;ring_resume_dirty_count=0;
    // Deferred entry is allowed only for the same phase and exact cell window.
    // A failed subject match now requests the original full capture of heroes.
    for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){auto *p=ring_cell(x,y);
        if(!p||!p->valid)hmm_fail("deferred ring cell missing");
        p->dirty=p->hero;p->pending.clear();
        if(p->dirty)ring_resume_dirty[ring_resume_dirty_count++]=uint16_t((unsigned(y)&15)*32+(unsigned(x)&31));
    }
}
bool ring_same_source(const RingDraw &a,const RingDraw &b){
    // Compare compact records directly: converting both to Draw copies shared
    // ownership and touches padding for every static layer of every dirty cell.
    return a.resource==b.resource&&a.mapping==b.mapping&&a.sx==b.sx&&a.sy==b.sy&&
        a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h&&a.step==b.step&&a.raw==b.raw&&a.opaque==b.opaque&&
        a.fill==b.fill&&a.repeat==b.repeat&&a.cpu==b.cpu&&a.layer==b.layer&&a.color==b.color&&a.mask==b.mask&&a.bitmap==b.bitmap;
}
HMM_SIZE_RENDER bool ring_subject_update(const HmmSubjectPart *old_parts,unsigned old_count,const HmmSubjectPart *new_parts,unsigned new_count){
    if(!ring_collect||!ring_can_resume)return false;
    struct Fragment {int x,y;RingDraw draw;unsigned at=~0u;};
    try{
        std::vector<Fragment> old_fragments,new_fragments;
        auto split=[&](const HmmSubjectPart *parts,unsigned count,std::vector<Fragment> &out){
            out.reserve(count);
            for(unsigned i=0;i<count;++i){const auto &p=parts[i];Draw d;d.resource=p.resource;d.r={p.x,p.y,p.w,p.h};d.sx=p.sx;d.sy=p.sy;d.step=p.step;d.opaque=p.opaque;d.mask=2;
                const auto r=intersect(d.r,{ring_x*32,ring_y*32,ring_cols*32,ring_rows*32});if(!r.w||!r.h)continue;
                for(int y=ring_floor(r.y);y<=ring_floor(r.y+r.h-1);++y)for(int x=ring_floor(r.x);x<=ring_floor(r.x+r.w-1);++x){
                    const Rect box{x*32,y*32,32,32},hit=intersect(d.r,box);Draw clipped=d;clipped.sx+=(hit.x-d.r.x)*d.step;clipped.sy+=hit.y-d.r.y;clipped.r={hit.x-box.x,hit.y-box.y,hit.w,hit.h};
                    RingDraw part(clipped);part.original={int16_t(d.r.x-box.x),int16_t(d.r.y-box.y),int16_t(d.r.w),int16_t(d.r.h)};out.push_back({x,y,std::move(part)});
                }
            }
        };
        split(old_parts,old_count,old_fragments);split(new_parts,new_count,new_fragments);
        if(old_fragments.size()!=new_fragments.size())return false;
        for(auto &p:old_fragments){const auto *cell=ring_cell(p.x,p.y);if(!cell||!cell->valid)return false;
            unsigned matches=0;for(unsigned j=0;j<cell->source.size();++j){const auto &d=cell->source[j];
                if(ring_same_source(p.draw,d)&&p.draw.original==d.original){p.at=j;++matches;}}
            if(matches!=1)return false;
            unsigned old_matches=0,new_matches=0;
            for(const auto &q:old_fragments)old_matches+=q.x==p.x&&q.y==p.y&&(q.draw.resource>>16)==(p.draw.resource>>16);
            for(const auto &q:new_fragments)new_matches+=q.x==p.x&&q.y==p.y&&(q.draw.resource>>16)==(p.draw.resource>>16);
            if(old_matches!=1||new_matches!=1)return false;
        }
        // Validation completed before the first mutation. Matching the same
        // cell and ICN preserves the original hero queue/layer at that cell.
        for(auto &p:old_fragments){auto *cell=ring_cell(p.x,p.y);auto &prior=cell->source[p.at];
            for(auto &q:new_fragments)if(q.x==p.x&&q.y==p.y&&(q.draw.resource>>16)==(p.draw.resource>>16)){
                q.draw.order=prior.order;
                if(!ring_same_source(prior,q.draw)||prior.original!=q.draw.original){prior=std::move(q.draw);cell->live_id=ring_live_next_id++;ring_subject_changed=true;}
                break;
            }
        }
        for(unsigned i=0;i<ring_resume_dirty_count;++i)ring_cells[ring_resume_dirty[i]]->dirty=false;
        return true;
    }catch(const std::bad_alloc&){return false;}
}
bool ring_same(const Draw &a,const Draw &b){
    return std::memcmp(&a,&b,offsetof(Draw,mask)+1)==0&&a.bitmap==b.bitmap;
}
bool ring_hero(const RingDraw &d){return !d.fill&&d.mask>=2;}
bool ring_same_base(const std::vector<RingDraw> &a,const std::vector<RingDraw> &b,unsigned first){
    unsigned i=first,j=first;
    while(i<a.size()&&j<b.size()){
        const bool ah=ring_hero(a[i]),bh=ring_hero(b[j]);if(ah!=bh)return false;
        if(ah){do{++i;}while(i<a.size()&&ring_hero(a[i]));do{++j;}while(j<b.size()&&ring_hero(b[j]));}
        else{if(!ring_same_source(a[i],b[j]))return false;++i;++j;}
    }
    return i==a.size()&&j==b.size();
}
bool ring_capture(Draw &d){
    if(!ring_collect)return false;
    ring_prepare();
    const unsigned order=ring_record_order++;
    const int tx=map_view_x-map_origin_x,ty=map_view_y-map_origin_y;
    const auto r=intersect(d.r,{ring_x*32+tx,ring_y*32+ty,ring_cols*32,ring_rows*32});
    if(!r.w||!r.h)return true;
    for(int y=ring_floor(r.y-ty);y<=ring_floor(r.y+r.h-1-ty);++y)for(int x=ring_floor(r.x-tx);x<=ring_floor(r.x+r.w-1-tx);++x){
        auto *cell=ring_cell(x,y);if(!cell||!cell->dirty)continue;
        const Rect box{x*32+tx,y*32+ty,32,32};const auto hit=intersect(d.r,box);Draw clipped=d;
        clipped.sx+=(hit.x-d.r.x)*d.step;clipped.sy+=hit.y-d.r.y;clipped.r={hit.x-box.x,hit.y-box.y,hit.w,hit.h};
        RingDraw part(clipped);
        part.original={int16_t(d.r.x-box.x),int16_t(d.r.y-box.y),int16_t(d.r.w),int16_t(d.r.h)};
        part.order=order;
#ifdef HMM2_HOST
        const auto capacity=cell->pending.capacity();
#endif
        cell->pending.push_back(std::move(part));
#ifdef HMM2_HOST
        ring_pending_grows+=capacity!=cell->pending.capacity();
#endif
    }
    return true;
}
void ring_commit(){
    ring_hero_changed=ring_subject_changed;
    bool geometry_changed=ring_frame_tx!=map_view_x-map_origin_x||ring_frame_ty!=map_view_y-map_origin_y;
    bool contents_changed=ring_subject_changed;ring_subject_changed=false;
    if(!ring_deferred)for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){
        auto *p=ring_cell(x,y);if(!p||!p->dirty)continue;auto &cell=*p;++ring_recorded;
        unsigned tail=0;bool anim=false,hero=false;
        for(const auto &d:cell.pending){anim|=!d.fill&&d.mask==1;hero|=!d.fill&&d.mask==2;}
        // Codex: анимация и герои остаются FT-спрайтами, общими для всех
        // экземпляров. В клетке хранится только неизменный фон до них.
        for(;tail<cell.pending.size();++tail)if(!cell.pending[tail].fill&&cell.pending[tail].mask>=1)break;
        const bool same=cell.valid&&tail==cell.tail&&std::equal(cell.pending.begin(),cell.pending.begin()+tail,cell.source.begin(),ring_same_source);
        const bool complete=ring_optimized()?
            same&&cell.pending.size()==cell.source.size()&&std::equal(cell.pending.begin()+tail,cell.pending.end(),cell.source.begin()+tail,ring_same_source):
            cell.valid&&cell.pending.size()==cell.source.size()&&std::equal(cell.pending.begin(),cell.pending.end(),cell.source.begin(),[](const RingDraw &a,const RingDraw &b){return a.order==b.order&&ring_same_source(a,b);});
        if(!complete){cell.live_id=ring_live_next_id++;contents_changed=true;}
        if(!complete){
            if(same&&ring_same_base(cell.pending,cell.source,tail))ring_hero_changed=true;
            else{cell.base_id=ring_base_next_id++;++ring_base_revision;}
        }
        if(!same){cell.id=ring_next_id++;cell.bytes=0;cell.usable=true;++ring_static_revision;}
        cell.source.swap(cell.pending);cell.pending.clear();cell.tail=tail;
#ifdef HMM2_HOST
        if(std::getenv("HMM2_NO_RING_SCRATCH"))std::vector<RingDraw>().swap(cell.pending);
#endif
        cell.valid=true;cell.animated=anim;cell.hero=hero;cell.phase=ring_phase;cell.dirty=false;
    }
    if(contents_changed)++ring_contents_revision;
    if(geometry_changed||contents_changed)++ring_revision;
    ring_collect=ring_deferred=false;ring_frame=true;ring_expanded=false;
    ring_frame_tx=map_view_x-map_origin_x;ring_frame_ty=map_view_y-map_origin_y;
}
#ifndef HMM2_HOST
// Codex: развёртывание нужно только снимкам/диалогам; экономим ELF, сохраняя
// оптимизацию скорости для обычного пути кольца и подготовки новой полосы.
__attribute__((optimize("Os")))
#endif
void ring_expand(){
    if(!ring_frame||ring_expanded)return;
#ifdef HMM2_HOST
    if(std::getenv("HMM2_MAPDL_STATS"))std::puts("HMM2 RING expanded for image readback");
#endif
    // Codex: проверка входов понадобится только следующему Redraw. Отдаём её
    // память до создания снимка, а не после пика копирования слоёв диалога.
    hmm_ring_world_clear();
    map_draws.clear();const int tx=ring_frame_tx,ty=ring_frame_ty;
    // Codex: снимку возвращаем геометрию исходных Blit, а не нарезку кольца.
    // Порядок внутри каждой клетки задаёт зависимости; независимые клетки
    // можно обходить иначе. Это сохраняет и старый запасной путь cache_map_tiles.
    struct Node {const RingDraw *source;int x,y;unsigned used=0,in=0,order=0,hash_next=~0u,first_edge=~0u;};
    auto restore=[](const RingDraw &d,int x,int y){Draw part=d;const auto &r=d.original;
        part.sx+=(r[0]-part.r.x)*part.step;part.sy+=r[1]-part.r.y;
        part.r={r[0]+x,r[1]+y,r[2],r[3]};
        if(!part.fill&&part.mask>=2)part.mask=part.mask==2;return part;};
    Sequence<Node> nodes;Sequence<std::array<unsigned,2>> edges;unsigned cell_number=0;
    // Ring expansion is a rare fallback, but a dense viewport can contain
    // thousands of fragments. Search only matching geometry buckets; scanning
    // every earlier node for each fragment repeatedly streamed PSRAM on Evo.
    auto buckets=std::make_unique<std::array<unsigned,1024>>();buckets->fill(~0u);
    for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){
        const auto *cell=ring_cell(x,y);if(!cell)continue;++cell_number;unsigned previous=~0u;
        for(const auto &d:cell->source){const int bx=x*32+tx,by=y*32+ty;const Draw part=restore(d,bx,by);
            uint32_t hash=2166136261u;
            const uint32_t key[]={part.resource,uint32_t(part.r.x),uint32_t(part.r.y),uint32_t(part.r.w),uint32_t(part.r.h),uint32_t(part.sx),uint32_t(part.sy),part.mapping};
            for(auto v:key)hash=(hash^v)*16777619u;
            auto &head=(*buckets)[hash&1023];unsigned at=~0u;
            for(unsigned i=head;i!=~0u;i=nodes[i].hash_next){const auto &n=nodes[i];
                if(i<at&&n.used!=cell_number&&n.source->resource==part.resource&&ring_same(restore(*n.source,n.x,n.y),part))at=i;
            }
            if(at==~0u){at=unsigned(nodes.size());nodes.push_back({&d,bx,by,cell_number,0,d.order,head});head=at;}
            else nodes[at].used=cell_number;
            if(previous!=~0u){edges.push_back({at,nodes[previous].first_edge});nodes[previous].first_edge=unsigned(edges.size()-1);++nodes[at].in;}previous=at;
        }
    }
    buckets.reset();
    // Preserve the exact original choice: lowest command order, then earliest
    // node. A ready heap and outgoing adjacency visit each edge only once.
    auto before=[&](unsigned a,unsigned b){return nodes[a].order!=nodes[b].order?nodes[a].order<nodes[b].order:a<b;};
    std::vector<unsigned> ready;ready.reserve(nodes.size());
    auto push=[&](unsigned value){
        unsigned i=unsigned(ready.size());ready.push_back(value);
        while(i){const unsigned parent=(i-1)/2;if(!before(value,ready[parent]))break;ready[i]=ready[parent];i=parent;}
        ready[i]=value;
    };
    for(unsigned i=0;i<nodes.size();++i)if(!nodes[i].in)push(i);
    for(size_t count=0;count<nodes.size();++count){
        if(ready.empty())hmm_fail("ring snapshot ordering cycle");
        const unsigned index=ready.front(),value=ready.back();ready.pop_back();
        if(!ready.empty()){
            unsigned i=0;
            for(unsigned child=1;child<ready.size();child=i*2+1){
                if(child+1<ready.size()&&before(ready[child+1],ready[child]))++child;
                if(!before(ready[child],value))break;ready[i]=ready[child];i=child;
            }
            ready[i]=value;
        }
        auto &n=nodes[index];
        map_draws.push_back(restore(*n.source,n.x,n.y));n.in=~0u;
        for(unsigned e=n.first_edge;e!=~0u;e=edges[e][1]){const unsigned next=edges[e][0];if(!--nodes[next].in)push(next);}
    }
    Sequence<Node>().swap(nodes);Sequence<Node>::release_unused();
    Sequence<std::array<unsigned,2>>().swap(edges);Sequence<std::array<unsigned,2>>::release_unused();
    ring_expanded=true;
}
