/* Codex: готовая клетка — PAL8 последнего непрозрачного цвета и несколько
 * прозрачных плоскостей преобразований. Их смешивает сам FT812 в исходном
 * порядке. Палитровые циклы обновляют общую палитру, не растеризуют текстуры. */
constexpr unsigned RING_SHADE_PLANES=8;
uint32_t ring_pixels_buffer[(1+RING_SHADE_PLANES)*256];
// Codex: фон идёт вертикальными полосами, как Render_Strips у Z80.
// Два банка защищают показанный кадр; в них нет монстров и фаз анимации.
constexpr unsigned RING_BG_COLS=21,RING_BG_ROWS=16,RING_BG_BYTES=RING_BG_ROWS*1024;
uint64_t ring_background_ids[2][RING_BG_COLS*RING_BG_ROWS];
unsigned ring_background_column(int x){const int v=x%int(RING_BG_COLS);return unsigned(v<0?v+int(RING_BG_COLS):v);}
unsigned ring_background_slot(int x,int y){return ring_background_column(x)*RING_BG_ROWS+(unsigned(y)&15);}
void ring_background_empty(uint32_t address,unsigned bank,unsigned slot){
    if(ring_background_ids[bank][slot]==~uint64_t(0))return;
    static const uint32_t empty[256]={};put(address+(slot&15)*1024,empty,256);ring_background_ids[bank][slot]=~uint64_t(0);
}
uint32_t ring_shade_argb(unsigned code){
    if(code>=1&&code<=4)return uint32_t(153-34*code)<<24;
    if(code>=5&&code<=14)return lighten_argb[code-5];
    static const uint8_t alpha[4]={88,60,34,17};
    if(code>=15&&code<=18)return uint32_t(alpha[code-15])<<24;
    if(code>=19&&code<=22)return (lighten_argb[code-19]&0xff000000u)|0xffffffu;
    return 0;
}
uint32_t ring_shade_palette(){
    uint32_t address;int upload;
    if(!hft_cache_get(&textures,UINT64_C(0xa000000000000000),1024,&address,&upload))hmm_fail("RAM_G ring palette capacity");
    if(upload){
        static uint32_t p[256];std::memset(p,0,sizeof(p));
        for(unsigned code=1;code<=22;++code)p[code]=ring_shade_argb(code);
        put(address,p,256);
    }
    return address;
}
#ifndef HMM2_HOST
// Codex: фон готовится при изменении статики; горячий вывод спрайтов остаётся O2.
__attribute__((optimize("Os")))
#endif
bool ring_pixels(RingCell &cell){
    auto *base=reinterpret_cast<uint8_t*>(ring_pixels_buffer);
    std::memset(base,0,sizeof(ring_pixels_buffer));
    std::array<uint8_t,1024> depth{},coverage{};
    const auto *translate=hmm_resource_palette();
    for(unsigned di=0;di<cell.tail;++di){const Draw &d=cell.source[di];
        if(d.bitmap||d.repeat||d.layer||(d.fill&&d.raw)||(!d.fill&&d.color))return false;
        uint32_t pin=0;Image region;const uint8_t *pixels=nullptr,*mask=nullptr;int stride=0,sx=d.sx,sy=d.sy;
        if(!d.fill){
            pin=hmm_resource_pin(d.resource);const auto &original=hmm_resource_get(d.resource);
            const int left=d.step<0?d.sx-d.r.w+1:d.sx;
            const bool cropped=hmm_resource_region(d.resource,left,d.sy,d.r.w,d.r.h,region);
            const auto &im=cropped?region:original;if(cropped){sx=d.step<0?d.r.w-1:0;sy=0;}
            pixels=im.image();stride=im.width();if(!im.singleLayer()&&!d.raw)mask=im.transform();
        }
        bool fits=true;
        for(int y=0;y<d.r.h&&fits;++y)for(int x=0;x<d.r.w;++x){
            const int at=(sy+y)*stride+sx+x*d.step,pos=(d.r.y+y)*32+d.r.x+x;
            const unsigned tr=d.fill?d.mask:mask?mask[at]:0;
            if(tr==1||(tr>1&&d.mapping&&mapping_solid[d.mapping-1]))continue;
            if(tr>=2&&tr<=(d.fill?9u:14u)){
                if(depth[pos]==RING_SHADE_PLANES){fits=false;break;}
                base[(1+depth[pos]++)*1024+pos]=uint8_t(d.fill?tr+13:tr-1);
            }else if(tr!=15||d.fill){
                unsigned c=d.fill?d.color:pixels[at];if(d.mapping)c=mappings[d.mapping-1][c];
                base[pos]=translate[c];depth[pos]=0;coverage[pos]=1;
            }
        }
        if(!d.fill)hmm_resource_restore_pin(d.resource,pin);
        if(!fits)return false;
    }
    if(!std::all_of(coverage.begin(),coverage.end(),[](uint8_t v){return v!=0;}))return false;
    const unsigned planes=*std::max_element(depth.begin(),depth.end());
    // Codex: непрозрачный поздний слой отменяет все прежние тени этого пикселя.
    for(unsigned j=0;j<planes;++j)for(unsigned p=0;p<1024;++p)if(depth[p]<=j)base[(j+1)*1024+p]=0;
    // Codex: одноцветная плоскость преобразования — точная маска L1 с исходной
    // COLOR_A. В отличие от L4 она не округляет альфу 119 до 118 на Tang.
    unsigned out=1024;cell.layout=0;
    for(unsigned j=0;j<planes;++j){const auto *src=base+(j+1)*1024;unsigned code=0;bool mixed=false;
        for(unsigned p=0;p<1024;++p)if(src[p]){if(code&&src[p]!=code){mixed=true;break;}code=src[p];}
        if(code&&!mixed){
            cell.layout|=uint64_t(code)<<(j*8);
            for(unsigned b=0;b<128;++b){unsigned bits=0;for(unsigned k=0;k<8;++k)bits|=unsigned(src[b*8+k]!=0)<<(7-k);base[out+b]=uint8_t(bits);}out+=128;
        }else {std::memmove(base+out,src,1024);out+=1024;}
    }
    cell.bytes=out;return true;
}
bool ring_upload(RingCell &cell,uint32_t &address,uint32_t background,unsigned bank){
    bool composed=false;
    if(!cell.bytes){Prof prep(P_TILEPREP);if(!ring_pixels(cell)){cell.usable=false;return false;}composed=true;}
    const unsigned slot=ring_background_slot(cell.x,cell.y);
    if(ring_background_ids[bank][slot]!=cell.id){
        Prof prep(P_TILEPREP);
        if(!composed){if(!ring_pixels(cell)){cell.usable=false;return false;}composed=true;}
        put(background+(slot&15)*1024,ring_pixels_buffer,256);ring_background_ids[bank][slot]=cell.id;++ring_builds;
    }
    const unsigned shade_bytes=cell.bytes-1024;if(!shade_bytes){address=0;return true;}
    int upload=0;
    if(!hft_cache_get(&textures,UINT64_C(0x9000000000000000)|cell.id,shade_bytes,&address,&upload))return false;
    if(upload){
        Prof prep(P_TILEPREP);
        if(!composed){if(!ring_pixels(cell)){cell.usable=false;return false;}}
        put(address,ring_pixels_buffer+256,shade_bytes/4);++ring_builds;
    }
    return true;
}
void ring_vertex(uint32_t address,uint32_t pal,int x,int y,int tx,int ty,unsigned code=0){
    const unsigned handle=code?1:0;const uint32_t argb=code?ring_shade_argb(code):0xffffffffu;
    set(0x05000000|handle);word(0x01000000|address);word(code?0x07080820:0x07804020);word(0x28000000);word(0x08004020);word(0x29000000);
    set(0x2a000000|pal);set(0x1f000001);set(0x04000000|(argb&0xffffff));set(0x10000000|(argb>>24));set(0x15000100);set(0x17000000);set(0x1a000000);
    // Codex: между переходами клетки вершины неизменны; прокрутка меняет
    // VERTEX_TRANSLATE, как Runtime_UpdateTranslate Z80.
    const int local_x=(x-ring_x)*32,local_y=(y-ring_y)*32;
    camera_word(0x2b000000|(uint32_t((tx+ring_x*32+(local_x&~511))*16)&0x1ffff));camera_word(0x2c000000|(uint32_t((ty+ring_y*32+(local_y&~511))*16)&0x1ffff));
    // Codex: VERTEX2II совместим и с редким общим проходом ручек при окне сверху.
    word(0x80000000|(uint32_t(local_x&511)<<21)|(uint32_t(local_y&511)<<12)|(handle<<7));
}
HMM_SIZE_RENDER void ring_build_live(Sequence<Draw> &out,int first_x,int first_y,int cols,int rows,std::vector<HeroSlot> *heroes=nullptr){
    out.clear();
    struct LiveSource {const RingDraw *draw;int16_t x,y;uint16_t hero=0xffff;};Sequence<LiveSource> parts;uint32_t max_order=0;
    for(int y=first_y;y<first_y+rows;++y)for(int x=first_x;x<first_x+cols;++x){
        const auto *p=ring_cell(x,y);if(!p||!p->valid)continue;const auto &cell=*p;
        const Rect box{x*32,y*32,32,32};const unsigned tail=cell.frame_cached?cell.tail:0;
        unsigned run=0;
        for(unsigned j=tail;j<cell.source.size();){const auto &d=cell.source[j];uint16_t slot=0xffff;
            if(heroes&&ring_hero(d)){
                if(heroes->size()>=0xffff)throw std::bad_alloc();slot=uint16_t(heroes->size());heroes->push_back({int16_t(x),int16_t(y),uint16_t(run++)});
                do{++j;}while(j<cell.source.size()&&ring_hero(cell.source[j]));
            }else ++j;
            parts.push_back({&d,int16_t(box.x),int16_t(box.y),slot});max_order=std::max(max_order,d.order);
        }
    }
    // Codex: внутри клетки order строго возрастает. Клетки не пересекаются,
    // поэтому общий порядок по нему сохраняет каждый пиксель и сближает части спрайта.
    // Codex: стабильная поразрядная сортировка целого номера команды.
    // Обычно достаточно двух линейных проходов вместо 10–12 слияний в PSRAM.
    // Равные номера сохраняют исходный порядок клеток, как в прежнем слиянии.
    {Prof sort_time(P_PARTS);Sequence<LiveSource> scratch;scratch.resize(parts.size());
    for(unsigned shift=0;shift<32&&(max_order>>shift);shift+=8){
        unsigned counts[256]{};
        for(const auto &part:parts)++counts[(part.draw->order>>shift)&255];
        unsigned at=0;for(auto &count:counts){const unsigned next=at+count;count=at;at=next;}
        for(const auto &part:parts)scratch[counts[(part.draw->order>>shift)&255]++]=part;
        parts.swap(scratch);
    }
    }
    for(const auto &part:parts){Draw d;
        if(part.hero!=0xffff){d.r={part.x,part.y,32,32};d.layer=d.raw=true;d.mask=255;d.mapping=part.hero;}
        else{d=*part.draw;d.r.x+=part.x;d.r.y+=part.y;}
        out.push_back(std::move(d));
    }
    // Codex: соседние части одного экземпляра становятся целым спрайтом;
    // проверка перекрытий join_layers сохраняет исходный порядок объектов.
    // Codex: CLOF32 хранит четыре фазы в общей периодической текстуре.
    // Живой туман должен выбрать их так же, как обычный список карты.
    repeat_fog(out);
    if(ring_optimized())join_map_layers(out);else join_layers(out,128);
    if(ring_optimized()&&cols<=20&&rows<=15){
        for(auto &d:out){d.r.x-=first_x*32;d.r.y-=first_y*32;}
        occlude(out);join_map_layers(out);
        for(auto &d:out){d.r.x+=first_x*32;d.r.y+=first_y*32;}
    }
}
#include "hmm_ring_clusters.inc"
#ifndef HMM2_HOST
// Codex: компактная сборка геометрии удерживает ELF в лимите кэша модулей.
// Общие текстурные примитивы и отправка FT-команд остаются с O2.
__attribute__((optimize("Os")))
#endif
bool emit_ring(const Draw &layer){
    if(!ring_frame||tile_map_active)return false;
    const Rect roi=intersect(map_window,layer.r);if(!roi.w||!roi.h)return true;
    reset_emitter();set(0x1b000000|(uint32_t(roi.x)<<11)|uint32_t(roi.y));set(0x1c000000|(uint32_t(roi.w)<<12)|uint32_t(roi.h));
    const int tx=map_view_x-map_origin_x,ty=map_view_y-map_origin_y;
    {const uint32_t background_started=prof_now();
    const uint32_t pal=palette_address(0),shade=ring_shade_palette();
    // Codex: полосы по 16 КиБ размещаются независимо. После окна героя не
    // требуется непрерывный блок 336 КиБ; закреплены только видимые колонки.
    const unsigned bank=ring_build_bank;uint32_t background[RING_BG_COLS];
    for(int x=ring_x;x<ring_x+ring_cols;++x){const unsigned col=ring_background_column(x);int upload;
        if(!hft_cache_get(&textures,UINT64_C(0xd000000000000000)|(bank*RING_BG_COLS+col),RING_BG_BYTES,&background[col],&upload)){ring_expand();return false;}
        if(upload)std::memset(ring_background_ids[bank]+col*16,0,16*sizeof(uint64_t));
    }
    // Codex: клетки не пересекаются. Сначала их непрозрачные основы, затем
    // плоскости теней в прежнем порядке и живые хвосты. Это убирает смену
    // COLOR_RGB/COLOR_A после каждой клетки и сохраняет порядок каждого пикселя.
    for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){
        const uint32_t address=background[ring_background_column(x)];auto *p=ring_cell(x,y);
        if(!p||!p->valid){ring_background_empty(address,bank,ring_background_slot(x,y));continue;}auto &cell=*p;
        const bool was_cached=cell.frame_cached;
        cell.frame_cached=cell.usable&&ring_upload(cell,cell.frame_address,address,bank);
        if(was_cached!=cell.frame_cached)ring_live_revision=0;
        if(cell.frame_cached)++ring_hits;else {++ring_fallbacks;ring_background_empty(address,bank,ring_background_slot(x,y));}
    }
    // Codex: одна вершина на колонку вместо вершины на каждую клетку.
    // Кольцевые строки выбирает аппаратный WRAP_Y, прокрутка меняет TRANSFORM_F.
    set(0x05000000);word(0x07804000);word(0x28000001);
    word(0x08040000|(32u<<9)|unsigned(roi.h));word(0x29000000);
    set(0x2a000000|pal);set(0x1f000001);set(0x04ffffff);set(0x100000ff);set(0x15000100);set(0x17000000);
    camera_word(0x1a000000|(uint32_t(((ring_y&15)*32+roi.y-(ring_y*32+ty))&511)<<8));
    set(0x2c000000);
    for(int x=ring_x;x<ring_x+ring_cols;++x){const int px=x*32+tx;
        word(0x01000000|background[ring_background_column(x)]);
        if(motion_active){const int local=(x-ring_x)*32;
            camera_word(0x2b000000|(uint32_t((tx+ring_x*32+(local&~511))*16)&0x1ffff));
            word(0x80000000|(uint32_t(local&511)<<21)|(uint32_t(roi.y)<<12));
        }else{
            set(0x2b000000|(uint32_t((px-(px&511))*16)&0x1ffff));
            word(0x80000000|(uint32_t(px&511)<<21)|(uint32_t(roi.y)<<12));
        }
    }
    for(unsigned plane=0;plane<RING_SHADE_PLANES;++plane){bool any=false;
        for(int y=ring_y;y<ring_y+ring_rows;++y)for(int x=ring_x;x<ring_x+ring_cols;++x){
            const auto *p=ring_cell(x,y);if(!p||!p->valid||!p->frame_cached)continue;const auto &cell=*p;
            unsigned offset=0;for(unsigned k=0;k<plane;++k)offset+=((cell.layout>>(k*8))&255)?128:1024;
            if(offset>=cell.bytes-1024)continue;any=true;
            Draw covered;covered.r={x*32+tx,y*32+ty,32,32};if(!map_hidden(covered))ring_vertex(cell.frame_address+offset,shade,x,y,tx,ty,unsigned(cell.layout>>(plane*8))&255);
        }
        if(!any)break;
    }
    prof_cycles[P_BACKGROUND]+=prof_now()-background_started;
    }
    reset_emitter();set(0x100000ff);
    if(!cluster_retry&&map_covers.empty()&&ring_optimized()&&
       !(cluster_blocked_static==ring_static_revision&&cluster_blocked_screen==interface_revision&&
         cluster_blocked_x==ring_x&&cluster_blocked_y==ring_y&&cluster_blocked_cols==ring_cols&&cluster_blocked_rows==ring_rows)){
#ifdef HMM2_HOST
        const bool enabled=std::getenv("HMM2_NO_CLUSTERS")==nullptr;
#else
        constexpr bool enabled=true;
#endif
        if(enabled){
            cluster_used=true;
            if(!emit_ring_clusters(roi,tx,ty)){
                cluster_failed=true;cluster_blocked_static=ring_static_revision;cluster_blocked_screen=interface_revision;
                cluster_blocked_x=ring_x;cluster_blocked_y=ring_y;cluster_blocked_cols=ring_cols;cluster_blocked_rows=ring_rows;
            }
            reset_emitter();set(0x1b000000);set(0x1c2801e0);ring_used=true;return true;
        }
    }
    ring_clusters_clear(false);
    // Keep sprite geometry in map coordinates. Camera motion only changes the
    // final clip/translation; rebuild on actual cell changes or a new strip.
    if(ring_live_revision!=ring_contents_revision||ring_live_x!=ring_x||ring_live_y!=ring_y||ring_live_cols!=ring_cols||ring_live_rows!=ring_rows){
    const uint32_t live_started=prof_now();
    ring_sprite_plan_clear();
    ring_build_live(ring_live,ring_x,ring_y,ring_cols,ring_rows);
    ring_live_revision=ring_contents_revision;ring_live_x=ring_x;ring_live_y=ring_y;ring_live_cols=ring_cols;ring_live_rows=ring_rows;
    prof_cycles[P_LIVE]+=prof_now()-live_started;
    }
    if(map_covers.empty()){
        // Sprite positions live in an FT812 command list. Scrolling patches
        // VERTEX_TRANSLATE only; SCISSOR clips whole sprites in hardware.
        if(ring_sprite_plan.dirty){const uint32_t sprite_started=prof_now();
            ring_sprite_plan_clear();reset_emitter();map_compiling=&ring_sprite_plan;
            for(size_t i=0;i<ring_live.size();++i){const auto &source=ring_live[i];
                if(source.fill){word(0xfd000000|uint32_t(i));reset_emitter();}
                else {Draw d=source;d.r.x-=ring_x*32;d.r.y-=ring_y*32;emit(d);}
            }
            map_compiling=nullptr;ring_sprite_plan.dirty=texture_missed;reset_emitter();
            prof_cycles[P_SPRITE_BUILD]+=prof_now()-sprite_started;
        }
        cached_textures_use(ring_sprite_plan);
        {const uint32_t replay_started=prof_now();
        for(uint32_t command:ring_sprite_plan.commands){
            const unsigned op=command>>24;
            if(op==0xfd){Draw d=ring_live[command&0xffffff];d.r.x+=tx;d.r.y+=ty;d=clip(d,roi);if(d.r.w&&d.r.h)emit(d);
                reset_emitter();set(0x1b000000|(uint32_t(roi.x)<<11)|uint32_t(roi.y));set(0x1c000000|(uint32_t(roi.w)<<12)|uint32_t(roi.h));continue;}
            if(op==0x2a)command=0x2a000000|palette_address(command&0xffffff);
            else if(op==0x2b)command=0x2b000000|((command+(tx+ring_x*32)*16)&0x1ffff);
            else if(op==0x2c)command=0x2c000000|((command+(ty+ring_y*32)*16)&0x1ffff);
            if(op==0x2b||op==0x2c)camera_word(command);else word(command);
        }
        prof_cycles[P_SPRITE_REPLAY]+=prof_now()-replay_started;
        }
    }else for(const auto &source:ring_live){Draw d=source;d.r.x+=tx;d.r.y+=ty;d=clip(d,roi);if(d.r.w&&d.r.h&&!map_hidden(d))emit(d);}
    reset_emitter();set(0x1b000000);set(0x1c2801e0);ring_used=true;return true;
}
