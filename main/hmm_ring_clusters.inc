/* A cached region owns commands, not a picture of monsters over terrain.
 * Cell fragments are disjoint across regions. The original per-pixel ordering
 * is retained inside each region by the normal builder and overlap rules. */
int cluster_floor(int x){return x>=0?x/8:-((-x+7)/8);}
void cluster_emitter_reset(){reset_emitter();handle_count=32;for(auto &h:handles)h.address=~0u;}
HMM_FAST_RENDER void emit_ring_hero(const HeroSlot &slot,int tx,int ty,const std::vector<uint8_t> *bindings=nullptr){
    const auto *cell=ring_cell(slot.x,slot.y);if(!cell||!cell->valid)return;
    unsigned run=0,j=cell->frame_cached?cell->tail:0;
    while(j<cell->source.size()){
        if(!ring_hero(cell->source[j])){++j;continue;}
        const unsigned first=j;do{++j;}while(j<cell->source.size()&&ring_hero(cell->source[j]));
        if(run++!=slot.run)continue;
        hero_slot_emitting=true;
        for(unsigned k=first;k<j;++k){Draw d=cell->source[k];d.r.x+=slot.x*32+tx;d.r.y+=slot.y*32+ty;if(d.r.w&&d.r.h){
            body_actor_handle=bindings&&!bindings->empty()?(*bindings)[std::min<size_t>(k-first,bindings->size()-1)]:-1;
            emit(d);++hero_slot_parts;}}
        body_actor_handle=-1;
        hero_slot_emitting=false;break;
    }
}
HMM_SIZE_RENDER bool emit_ring_clusters(const Rect &roi,int tx,int ty){
    bool skeleton=!skeleton_retry&&!(skeleton_blocked_static==ring_static_revision&&skeleton_blocked_screen==interface_revision&&
        skeleton_blocked_x==ring_x&&skeleton_blocked_y==ring_y&&skeleton_blocked_cols==ring_cols&&skeleton_blocked_rows==ring_rows);
#ifdef HMM2_HOST
    static const bool disabled=std::getenv("HMM2_NO_SKELETON")!=nullptr;if(disabled)skeleton=false;
#endif
    skeleton_used=skeleton;
    const int x0=cluster_floor(ring_x),x1=cluster_floor(ring_x+ring_cols-1);
    const int y0=cluster_floor(ring_y),y1=cluster_floor(ring_y+ring_rows-1);
    for(auto &p:map_clusters)if(p&&(p->x<x0||p->x>x1||p->y<y0||p->y>y1)){map_cache_release(p->part);p.reset();}
    try{
        if(!cluster_seeded){
            // Keep the established atlas placement on scene entry. The region
            // traversal must not fragment shared CELL banks differently.
            Sequence<Draw> warm;const uint32_t live_started=prof_now();
            ring_build_live(warm,ring_x,ring_y,ring_cols,ring_rows);
            prof_cycles[P_LIVE]+=prof_now()-live_started;
            ring_sprite_plan_clear();reset_emitter();map_compiling=&ring_sprite_plan;
            for(const auto &d:warm)if(!d.fill)emit(d);
            map_compiling=nullptr;ring_sprite_plan_clear();reset_emitter();
            if(texture_missed)return false;
            cluster_seeded=true;
        }
        for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx){
            auto &slot=map_clusters[(unsigned(cy)&3)*4+(unsigned(cx)&3)];
            if(slot&&(slot->x!=cx||slot->y!=cy)){map_cache_release(slot->part);slot.reset();}
            if(!slot){slot.reset(new MapCluster);slot->x=cx;slot->y=cy;}
            auto &c=*slot;std::array<uint64_t,64> keys{};
            const int bx=cx*8,by=cy*8;
            for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                const int wx=bx+x,wy=by+y;
                if(wx<ring_x||wx>=ring_x+ring_cols||wy<ring_y||wy>=ring_y+ring_rows)continue;
                const auto *cell=ring_cell(wx,wy);
                if(cell&&cell->valid)keys[y*8+x]=((skeleton?cell->base_id:cell->live_id)<<1)|unsigned(cell->frame_cached);
            }
            if(c.part.dirty||c.keys!=keys||c.skeleton!=skeleton){
                map_cache_release(c.part);c.fills.clear();c.heroes.clear();c.skeleton=skeleton;
                Sequence<Draw> live;
                const int sx=std::max(bx,ring_x),sy=std::max(by,ring_y);
                const uint32_t live_started=prof_now();
                ring_build_live(live,sx,sy,std::min(bx+8,ring_x+ring_cols)-sx,std::min(by+8,ring_y+ring_rows)-sy,skeleton?&c.heroes:nullptr);
                prof_cycles[P_LIVE]+=prof_now()-live_started;
                const uint32_t started=prof_now();
                cluster_emitter_reset();map_compiling=&c.part;cluster_compiling=true;
                for(auto d:live){d.r.x-=bx*32;d.r.y-=by*32;
                    if(skeleton&&d.layer&&d.raw&&d.mask==255){word(0xfd800000|d.mapping);cluster_emitter_reset();}
                    else if(d.fill){const unsigned at=unsigned(c.fills.size());c.fills.push_back(d);word(0xfd000000|at);cluster_emitter_reset();}
                    else emit(d);
                }
                map_compiling=nullptr;cluster_compiling=false;reset_emitter();
                prof_cycles[P_SPRITE_BUILD]+=prof_now()-started;
                if(texture_missed)return false;
                c.keys=keys;c.part.dirty=false;++cluster_builds;if(skeleton)++skeleton_builds;
            }else{++cluster_hits;if(skeleton)++skeleton_hits;}
            cached_textures_use(c.part);
            const uint32_t replay_started=prof_now();
            for(uint32_t command:c.part.commands){const unsigned op=command>>24;
                if(op==0xfd){
                    reset_emitter();
                    if(command&0x800000){const auto &slot=c.heroes[command&0x7fffff];
                        if(body_actor_collect)word(0xfe000000u|unsigned(body_actor_spans.size()));
                        const unsigned begin=words;
                        emit_ring_hero(slot,tx,ty);
                        if(body_actor_collect){body_actor_spans.push_back({slot,begin,words});word(0xfd000000u|unsigned(body_actor_spans.size()-1));}
                    }else{Draw d=c.fills[command&0xffffff];d.r.x+=bx*32+tx;d.r.y+=by*32+ty;d=clip(d,roi);if(d.r.w&&d.r.h)emit(d);}
                    reset_emitter();
                    set(0x1b000000|(uint32_t(roi.x)<<11)|uint32_t(roi.y));set(0x1c000000|(uint32_t(roi.w)<<12)|uint32_t(roi.h));continue;
                }
                if(op==0x2a)command=0x2a000000|palette_address(command&0xffffff);
                else if(op==0x2b)command=0x2b000000|((command+(bx*32+tx)*16)&0x1ffff);
                else if(op==0x2c)command=0x2c000000|((command+(by*32+ty)*16)&0x1ffff);
                if(op==0x2b||op==0x2c)camera_word(command);else word(command);
            }
            prof_cycles[P_SPRITE_REPLAY]+=prof_now()-replay_started;
        }
    }catch(const std::bad_alloc &){hero_slot_emitting=false;map_compiling=nullptr;cluster_compiling=false;reset_emitter();return false;}
    return true;
}
