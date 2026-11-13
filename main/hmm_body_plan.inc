/* Cache completed FT command ranges. Actor spans restore the exact context
 * expected by the following immutable range; no terrain pixels are copied. */
bool body_motion_reusable(){
    return body_motion&&(body_dynamic?body_base_revision==ring_base_revision:body_contents_revision==ring_contents_revision)&&body_x==ring_x&&body_y==ring_y&&
        body_cols==ring_cols&&body_rows==ring_rows&&map_covers.empty()&&
        body_window.x==map_window.x&&body_window.y==map_window.y&&body_window.w==map_window.w&&body_window.h==map_window.h;
}
HMM_SIZE_RENDER bool body_motion_capture(){
    bool okay=true;
    for(unsigned i=0;i<words;++i){auto &w=dl[i];const unsigned op=w>>24;unsigned kind=0;
        if((op==0x2b||op==0x2c)&&(w&0x800000)){kind=op==0x2b?1:2;w&=~0x800000u;}
        else if(op==0x1a&&(w&0xfe0000u)==0x600000u){kind=3;w&=0xff01ffffu;}
        if(kind&&okay)try{motion_pending.push_back({uint16_t(i),uint16_t(kind)});}catch(...){motion_pending.clear();okay=false;}
    }
    return okay;
}
void body_actor_capture(){
    // Marker removal never allocates. Rebase camera patches after the ordinary
    // handle allocator, which preserves vertex and non-bitmap command order.
    unsigned out=0,patch=0;
    for(unsigned read=0;read<words;++read){const uint32_t w=dl[read];const unsigned op=w>>24;
        if(op==0xfe||op==0xfd){const unsigned i=w&0xffffff;
            if(i>=body_actor_spans.size())hmm_fail("actor span marker invalid");
            (op==0xfe?body_actor_spans[i].begin:body_actor_spans[i].end)=out;continue;
        }
        while(patch<motion_pending.size()&&motion_pending[patch].index==read)motion_pending[patch++].index=uint16_t(out);
        dl[out++]=w;
    }
    words=out;
}
uint32_t body_symbolic_palette(uint32_t command){
    if((command>>24)!=0x2a)return command;
    const unsigned address=command&0xffffff,page=address/HFT_PAGE_BYTES;
    const unsigned slot=page<HFT_PAGES?palette_entry_by_page[page]:0;
    if(slot){const auto &e=textures.entries[slot-1];
        if(e.valid&&e.page*HFT_PAGE_BYTES==address&&(e.key>>60)==2)
            return 0x2a800000|unsigned((e.key&UINT64_C(0x0fffffffffffffff))>>1);
    }
    return command;
}
uint32_t body_resolve_state(uint32_t w,unsigned kind,int dx,int dy){
    if(kind<=2&&kind)return (w&0xff000000u)|((w+(kind==1?dx:dy)*16)&0x1ffffu);
    if(kind==3)return 0x1a000000u|((w-dy*256)&0x1ff00u);
    if((w&0xff800000u)==0x2a800000u)return 0x2a000000u|palette_address(w&0x7fffffu);
    return w;
}
void body_apply_state(const BodyTerminal &t,int dx,int dy,bool restore){
    if(restore){
        for(unsigned h=0;h<32;++h)if(body_actor_touched&(uint32_t(1)<<h))for(unsigned f=0;f<5;++f)if(t.mask[h]&(1u<<f)){
            if(!(compact_mask[h]&(1u<<f))||compact_bitmap[h][f]!=t.bitmap[h][f]){word(0x05000000u|h);word(t.bitmap[h][f]);}
        }
        for(unsigned op=0;op<64;++op)if(op!=5&&t.valid[op]&&command_kind[op]==6){
            const uint32_t w=body_resolve_state(t.state[op],t.camera[op],dx,dy);
            if(!compact_valid[op]||compact_state[op]!=w)word(w);
        }
        if(compact_handle!=t.handle)word(0x05000000u|t.handle);
    }
    std::memcpy(compact_state,t.state,sizeof(t.state));std::memcpy(compact_valid,t.valid,sizeof(t.valid));
    std::memcpy(compact_bitmap,t.bitmap,sizeof(t.bitmap));std::memcpy(compact_mask,t.mask,sizeof(t.mask));compact_handle=t.handle;
    for(unsigned op=0;op<64;++op)if(t.valid[op]&&(t.camera[op]||op==0x2a))compact_state[op]=body_resolve_state(t.state[op],t.camera[op],dx,dy);
}
BodyActorGlobals body_actor_globals(const BodyTerminal &t){
    BodyActorGlobals g;
    for(unsigned i=0;i<12;++i){const unsigned op=BODY_ACTOR_OPS[i];g.value[i]=t.state[op];g.camera|=uint32_t(t.camera[op])<<(i*2);if(t.valid[op])g.known|=uint16_t(1u<<i);}
    return g;
}
HMM_FAST_RENDER void body_actor_apply(const BodyActorState &s,int dx,int dy,bool restore){
    const auto &g=restore?s.after:s.before;
    if(restore){
        for(const auto &b:s.bindings)if(body_actor_touched&(uint32_t(1)<<b.handle))for(unsigned f=0;f<5;++f)if(b.after_known&(1u<<f)){
            if(!(compact_mask[b.handle]&(1u<<f))||compact_bitmap[b.handle][f]!=b.after[f]){word(0x05000000u|b.handle);word(b.after[f]);}
        }
        for(unsigned i=1;i<12;++i)if(g.known&(1u<<i)){const unsigned op=BODY_ACTOR_OPS[i];const uint32_t w=body_resolve_state(g.value[i],(g.camera>>(i*2))&3,dx,dy);
            if(!compact_valid[op]||compact_state[op]!=w)word(w);
        }
        if(compact_handle!=(g.value[0]&31))word(g.value[0]);
    }
    std::memset(compact_valid,0,sizeof(compact_valid));std::memset(compact_mask,0,sizeof(compact_mask));
    for(unsigned i=0;i<12;++i)if(g.known&(1u<<i)){const unsigned op=BODY_ACTOR_OPS[i];compact_valid[op]=1;compact_state[op]=body_resolve_state(g.value[i],(g.camera>>(i*2))&3,dx,dy);}
    compact_handle=g.value[0]&31;
    for(const auto &b:s.bindings){compact_mask[b.handle]=restore?b.after_known:b.before_known;std::memcpy(compact_bitmap[b.handle],restore?b.after:b.before,20);}
}
HMM_SIZE_RENDER void body_actor_states_build(){
    body_actor_states.resize(body_spans.size());
    BodyTerminal t{};auto before=std::make_unique<BodyTerminal>();std::fill_n(t.last,64,uint16_t(0xffff));
    const uint32_t defaults[]={0x02000000,0x04ffffff,0x05000000,0x0b000014,0x0e000010,0x100000ff,0x15000100,0x16000000,0x17000000,0x18000000,0x1a000000,0x1b000000,0x27000004,0x2b000000,0x2c000000};
    for(auto w:defaults){t.state[w>>24]=w;t.valid[w>>24]=1;}
    unsigned actor=0,patch=0;
    for(unsigned i=0;i<=body_plan.commands.size();++i){
        while(actor<body_spans.size()){
            if(i==body_spans[actor].begin)*before=t;
            if(i!=body_spans[actor].end)break;
            auto &a=body_actor_states[actor];a.before=body_actor_globals(*before);a.after=body_actor_globals(t);
            uint32_t seen=0;
            for(auto h:a.handles)if(!(seen&(uint32_t(1)<<h))){seen|=uint32_t(1)<<h;BodyActorBinding b{};b.handle=h;b.before_known=before->mask[h];b.after_known=t.mask[h];
                std::memcpy(b.before,before->bitmap[h],20);std::memcpy(b.after,t.bitmap[h],20);a.bindings.push_back(b);}
            ++actor;
        }
        if(i==body_plan.commands.size())break;
        const uint32_t w=body_plan.commands[i];const unsigned op=w>>24;
        if((w>>30)==2&&actor<body_spans.size()&&i>=body_spans[actor].begin&&i<body_spans[actor].end)
            body_actor_states[actor].handles.push_back(uint8_t((w>>7)&31));
        unsigned camera=0;while(patch<body_patches.size()&&body_patches[patch].index<i)++patch;
        if(patch<body_patches.size()&&body_patches[patch].index==i&&body_patches[patch].kind<=3)camera=body_patches[patch].kind;
        if(op<64){const unsigned kind=command_kind[op];
            if(kind&&kind<6){t.bitmap[t.handle][kind-1]=w;t.mask[t.handle]|=uint8_t(1u<<(kind-1));}
            else if(kind==6){t.state[op]=w;t.valid[op]=1;t.last[op]=uint16_t(i);t.camera[op]=uint8_t(camera);if(op==5)t.handle=w&31;}
        }
    }
    if(actor!=body_spans.size())throw std::bad_alloc();
    *body_terminal=t;
}
HMM_SIZE_RENDER void body_plan_store(unsigned revision){
    const uint32_t body_started=prof_now();body_plan_clear();
    try{
        body_plan.commands.assign(dl,dl+words);
        for(auto &w:body_plan.commands)w=body_symbolic_palette(w);
        for(unsigned i=0;(i=hft_cache_next_pin(&textures,i,unsigned(textures.build_slot)))<HFT_ENTRIES;++i){auto &e=textures.entries[i];
            if((e.key>>60)!=2){body_plan.textures.push_back(i);++map_texture_refs[i];e.pins|=4u;}
        }
        body_terminal.reset(new BodyTerminal{});auto &t=*body_terminal;
        std::memcpy(t.state,compact_state,sizeof(t.state));std::memcpy(t.valid,compact_valid,sizeof(t.valid));
        std::memcpy(t.bitmap,compact_bitmap,sizeof(t.bitmap));std::memcpy(t.mask,compact_mask,sizeof(t.mask));t.handle=compact_handle;
        std::fill_n(t.last,64,uint16_t(0xffff));body_patches=motion_pending;
        for(unsigned i=0;i<words;++i){const uint32_t w=dl[i];const unsigned op=w>>24;
            if(op<64&&command_kind[op]==6){t.state[op]=w;t.valid[op]=1;t.last[op]=uint16_t(i);}
            if((body_plan.commands[i]&0xff800000u)==0x2a800000u)body_patches.push_back({uint16_t(i),4});
        }
        for(const auto &p:motion_pending){const unsigned op=dl[p.index]>>24;if(t.last[op]==p.index)t.camera[op]=uint8_t(p.kind);}
        // Keep 100 words before the existing 2000-word replay guard. A larger
        // actor pose still retries the ordinary exact builder before publish.
        body_dynamic=!body_actor_spans.empty()&&words<=1900;
        if(body_dynamic){
            body_spans=body_actor_spans;
            std::sort(body_patches.begin(),body_patches.end(),[](const auto &a,const auto &c){return a.index<c.index;});
            for(const auto &s:body_actor_spans)body_actors.push_back(s.slot);
            body_actor_states_build();
        }
        body_base_revision=ring_base_revision;body_motion=motion_candidate;body_contents_revision=ring_contents_revision;
        body_handle_fast=rehandle_fast_used;
        body_tx=map_view_x-map_origin_x;body_ty=map_view_y-map_origin_y;
        body_x=ring_x;body_y=ring_y;body_cols=ring_cols;body_rows=ring_rows;body_window=map_window;
        body_screen_revision=revision;body_ring_revision=ring_revision;body_bank=ring_build_bank;++body_builds;
    }catch(...){body_plan_clear();}
    prof_cycles[P_BODY_STORE]+=prof_now()-body_started;
}
void body_plan_emit(){
    rehandle_fast_used|=body_handle_fast;
    cached_textures_use(body_plan);
    const int dx=map_view_x-map_origin_x-body_tx,dy=map_view_y-map_origin_y-body_ty;
    if(body_dynamic){
        unsigned patch=0;
        auto append=[&](unsigned first,unsigned end){const unsigned n=end-first,base=words;
            if(words+n>DL_CAPACITY){dl_overflow=true;return false;}
            std::memcpy(dl+words,body_plan.commands.data()+first,n*4);words+=n;
            while(patch<body_patches.size()&&body_patches[patch].index<first)++patch;
            while(patch<body_patches.size()&&body_patches[patch].index<end){const auto &p=body_patches[patch++];
                dl[base+p.index-first]=body_resolve_state(body_plan.commands[p.index],p.kind,dx,dy);
            }
            return true;
        };
        try{unsigned first=0;
            for(unsigned i=0;i<body_actors.size();++i){const auto &span=body_spans[i];const auto &s=body_actor_states[i];
                if(!append(first,span.begin))break;
                body_actor_apply(s,dx,dy,false);cluster_emitter_reset();
                body_actor_touched=0;
                emit_ring_hero(body_actors[i],map_view_x-map_origin_x,map_view_y-map_origin_y,&s.handles);
                body_actor_apply(s,dx,dy,true);first=span.end;
            }
            if(!dl_overflow)append(first,unsigned(body_plan.commands.size()));
            body_apply_state(*body_terminal,dx,dy,false);
        }catch(const std::bad_alloc&){body_actor_handle=-1;hero_slot_emitting=false;texture_missed=true;}
        ++body_dynamic_hits;
    }else{
#ifdef HMM2_HOST
        if(std::getenv("HMM2_NO_MOTION_PLAN")){
            for(uint32_t w:body_plan.commands){if((w&0xff800000u)==0x2a800000u)w=0x2a000000u|palette_address(w&0x7fffffu);word(w);}
            ring_build_bank=body_bank;ring_used=true;++body_hits;return;
        }
#endif
        const auto &t=*body_terminal;words=unsigned(body_plan.commands.size());std::memcpy(dl,body_plan.commands.data(),words*4);
        body_apply_state(t,dx,dy,false);
        for(const auto &p:body_patches){uint32_t &w=dl[p.index];
            if(p.kind<=2)w=(w&0xff000000u)|((w+(p.kind==1?dx:dy)*16)&0x1ffffu);
            else if(p.kind==3)w=0x1a000000u|((w-dy*256)&0x1ff00u);
            else w=0x2a000000u|palette_address(w&0x7fffffu);
            const unsigned op=w>>24;if(t.last[op]==p.index)compact_state[op]=w;
        }
    }
    if(body_motion&&(dx||dy))++body_move_hits;
    ring_build_bank=body_bank;ring_used=true;++body_hits;
}
