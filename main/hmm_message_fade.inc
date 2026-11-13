/* Common modal animation: keep FT commands and original RAM_G textures, never
 * read or copy the framebuffer. The last shown commands survive restorers and
 * map geometry reclamation. Cursor remains a live, fully opaque final layer. */
#ifndef HMM2_HOST
#pragma GCC push_options
#pragma GCC optimize("Oz")
#define HMM_MESSAGE_FN __attribute__((optimize("Oz"),noinline,section(".text.hmm_message")))
#else
#define HMM_MESSAGE_FN
#endif
namespace {
struct MessageCommands {
    MapPart part;
    ~MessageCommands(){map_cache_release(part);}
};
struct WindowSurface {std::array<uint32_t,60> at{};bool split=false;};
struct WindowTextRow {
    MessageCommands lease;
    Sequence<Draw> glyphs;
    uint32_t address=0,hash=0;
    unsigned age=0;
};
struct MessageFade {
    std::shared_ptr<MessageCommands> background;
    MessageCommands foreground;
    unsigned revision=0;
    bool shown=false,dirty=true,popup=true;
    bool packed=false;
    std::vector<uint32_t> program,scene;
    Sequence<Draw> geometry;
    bool blit=false;
    unsigned window_mode=0,window_started=0;
    bool window_report=false,window_missing=false,chrome_rejected=false,rows_rejected=false;
    Sequence<Draw> chrome_geometry;
    std::unique_ptr<MessageCommands> chrome;
    Rect text_area;
    int text_height=0;
    unsigned text_age=0,row_builds=0,row_hits=0;
    std::vector<std::unique_ptr<WindowTextRow>> text_rows;
    Rect blit_roi;
    WindowSurface blit_color,blit_effect;
    std::vector<uint8_t> blit_pixels;
    std::array<uint32_t,256> blit_colors{};
    unsigned blit_colors_used=1;
    Sequence<Draw> blit_before;
    int mouse_x=0,mouse_y=0;
};
std::shared_ptr<MessageCommands> message_scene;
std::vector<std::unique_ptr<MessageFade>> message_fades;
MessageFade *message_transition;
unsigned message_alpha=255;
HMM_MESSAGE_FN void message_fade_reset(){message_transition=nullptr;message_fades.clear();message_scene.reset();}

HMM_MESSAGE_FN void message_scene_capture(){
    if(!message_scene||!message_scene.unique())message_scene=std::make_shared<MessageCommands>();
    auto &p=message_scene->part;map_cache_release(p);
    p.commands.assign(dl,dl+words);
    for(unsigned i=0;(i=hft_cache_next_pin(&textures,i,unsigned(textures.build_slot)))<HFT_ENTRIES;++i){auto &e=textures.entries[i];
        p.textures.push_back(i);++map_texture_refs[i];e.pins|=4u;
    }
}
HMM_MESSAGE_FN void message_fade_open(bool popup){
    auto f=std::unique_ptr<MessageFade>(new MessageFade);
    f->popup=popup;
    if(popup)f->background=message_scene;
    message_fades.push_back(std::move(f));
}

HMM_MESSAGE_FN void message_geometry_update(MessageFade &f){
    auto &c=canvas(*message_layers.back());
    size_t begin=message_fades.size()-1;unsigned revision=c.revision;
    while(begin&&message_fades[begin-1]->popup&&!message_fades[begin-1]->shown&&message_fades[begin-1]->background==f.background){
        --begin;revision=std::max(revision,canvas(*message_layers[begin]).revision);
    }
    if(f.revision==revision)return;
    f.geometry.clear();
    for(size_t i=begin;i<message_layers.size();++i)for(const auto &d:canvas(*message_layers[i]).draws)f.geometry.push_back(d);
    f.revision=revision;f.dirty=true;
    if(!f.blit)map_cache_release(f.foreground.part);
}
HMM_MESSAGE_FN void message_window_sprite(MessageFade &);
HMM_MESSAGE_FN void message_window_blits(MessageFade &);
HMM_MESSAGE_FN void message_window_hybrid(MessageFade &,bool);
HMM_MESSAGE_FN void message_foreground(MessageFade &f){
    if(!f.dirty)return;
    if(f.blit){f.window_mode=0;f.packed=false;f.window_missing=false;}
#ifdef HMM2_HOST
    if(f.blit&&(std::getenv("HMM2_FORCE_WINDOW_ROWS")||std::getenv("HMM2_FORCE_WINDOW_HYBRID"))){
        message_window_hybrid(f,std::getenv("HMM2_FORCE_WINDOW_ROWS")!=nullptr);f.dirty=false;return;
    }
#endif
    if(f.blit&&f.chrome){message_window_hybrid(f,f.chrome_rejected);f.dirty=false;return;}
    if(f.blit&&f.rows_rejected&&!f.blit_before.empty()){message_window_blits(f);f.window_mode=3;f.dirty=false;return;}
    if(f.packed){message_window_sprite(f);f.dirty=false;return;}
    auto &p=f.foreground.part;map_cache_release(p);
    struct CompileGuard {MapPart *saved=message_compiling;~CompileGuard(){message_compiling=saved;map_compiling=nullptr;reset_emitter();}} guard;
    // This command list covers the window, including the sidebar. Fill must
    // restore this viewport, rather than the adventure-map rectangle.
    map_compiling=message_compiling=&p;reset_emitter();texture_missed=false;
    // Reset the global context inherited from the underlying scene. Bitmap
    // descriptors are emitted by emit(), independently of background handles.
    const uint32_t context[]={0x04ffffff,0x05000000,0x100000ff,0x0b000014,
        0x15000100,0x16000000,0x17000000,0x18000000,0x19000100,0x1a000000,
        0x1b000000,0x1c2801e0,0x27000004,0x2b000000,0x2c000000};
    for(auto v:context)word(v);
    // Work on a separate geometry list: occlusion inside the window remains
    // valid, while the translucent window never occludes its background.
    occlude(f.geometry);join_layers(f.geometry);
    emit_sequence(f.geometry.begin(),f.geometry.size());
#ifdef HMM2_HOST
    if(const char *folder=std::getenv("HMM2_FT_WINDOW_DUMP")){
        char path[512];std::snprintf(path,sizeof(path),"%s/foreground_%u.bin",folder,serial);
        if(FILE *file=std::fopen(path,"wb")){std::fwrite(p.commands.data(),4,p.commands.size(),file);std::fclose(file);}
        std::snprintf(path,sizeof(path),"%s/background_%u.bin",folder,serial);
        if(FILE *file=std::fopen(path,"wb")){if(f.background)std::fwrite(f.background->part.commands.data(),4,f.background->part.commands.size(),file);std::fclose(file);}
        std::snprintf(path,sizeof(path),"%s/geometry_%u.csv",folder,serial);
        if(FILE *file=std::fopen(path,"w")){for(const auto &d:f.geometry)std::fprintf(file,"%u,%d,%d,%d,%d,%d,%d,%u,%u,%u\n",d.resource,d.r.x,d.r.y,d.r.w,d.r.h,d.sx,d.sy,d.mapping,unsigned(d.bitmap!=nullptr),unsigned(d.fill));std::fclose(file);}
        std::printf("WINDOW_COMMANDS serial=%u foreground=%zu background=%zu draws=%zu\n",serial,p.commands.size(),f.background?f.background->part.commands.size():0,f.geometry.size());
    }
#endif
    if(texture_missed){if(!f.blit)hmm_fail("Window fade texture missing");f.window_missing=true;}
    f.dirty=false;
#ifdef HMM2_HOST
    if(std::getenv("HMM2_FORCE_WINDOW_SPRITE")){
        map_compiling=nullptr;message_window_sprite(f);
    }
#endif
}
HMM_MESSAGE_FN void message_prepare(MessageFade &f){
    GpuBusy busy;uint32_t swap;const auto start=hmm_core->millis();
    if(f.blit&&f.dirty){f.window_started=start;f.window_report=true;}
    do{if(hmm_core->ft_read(0x302054,&swap)!=ZM_OK)hmm_fail("FT DLSWAP read");
        if(hmm_core->millis()-start>2000)hmm_fail("FT DLSWAP timeout");}while(swap);
    hft_cache_begin(&textures,displayed);message_foreground(f);
}

HMM_MESSAGE_FN void message_window_sprite(MessageFade &f){
    const auto started=hmm_core->millis();
    Rect roi{};bool first=true;
    for(const auto &d:f.geometry){
        if(d.layer)hmm_fail("Window contains map layer");
        const auto r=intersect(d.r,{0,0,640,480});if(!r.w||!r.h)continue;
        if(first){roi=r;first=false;}else{const int right=std::max(roi.x+roi.w,r.x+r.w),bottom=std::max(roi.y+roi.h,r.y+r.h);
            roi.x=std::min(roi.x,r.x);roi.y=std::min(roi.y,r.y);roi.w=right-roi.x;roi.h=bottom-roi.y;}
    }
    if(first)hmm_fail("Empty window sprite");
    auto &p=f.foreground.part;
    struct CompileGuard {~CompileGuard(){map_compiling=nullptr;reset_emitter();}} compile_guard;
    // Unused original foreground atlases may be evicted for this sprite.
    // Background commands and the displayed scanout retain their own pins.
    for(auto i:p.textures)textures.entries[i].pins&=uint8_t(~(1u<<textures.build_slot));
    map_cache_release(p);
    const unsigned bytes=(unsigned(roi.w*roi.h)+3)&~3u;uint32_t address=0;int upload;
    bool split=false;
#ifdef HMM2_HOST
    split=std::getenv("HMM2_FORCE_WINDOW_STRIPS")!=nullptr;
#endif
    split=split||!hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,bytes,&address,&upload);
    // Fragmented RAM_G can hold this window without a single large free run.
    // Each strip retains its own lease; the background is never rasterized.
    std::array<uint32_t,15> strips{};
    if(!split){map_compiling=&p;map_keep_texture();map_compiling=nullptr;}
    Canvas own;own.draws=f.geometry;
    for(auto &d:own.draws){d.r.x-=roi.x;d.r.y-=roi.y;}
    const auto *translate=hmm_resource_palette();uint8_t effects[20]{};bool used[256]={};
    for(unsigned i=0;i<256;++i)used[translate[i]]=true;
    used[0]=used[2]=used[3]=used[4]=used[5]=true;
    for(unsigned i=2;i<=5;++i)effects[i]=uint8_t(i);
    for(unsigned i=0,k=6;i<256&&k<20;++i)if(!used[i])effects[k++]=uint8_t(i);
    // Bound CPU scratch to 40 KiB even for a fullscreen dialog. The temporary
    // image contains only window pixels and its transform plane; shadow and
    // highlight masks become FT palette alpha, never pixels from the map.
    std::vector<uint8_t> pixels(size_t(roi.w)*32*2+4);
    struct View:Image {View(int w,int h){_hmmSetDimensions(w,h,false);}};
    struct RasterGuard {bool saved=materializing;RasterGuard(){materializing=true;}~RasterGuard(){materializing=saved;}} guard;
    for(int y=0;y<roi.h;y+=32){
        if(y)for(auto &d:own.draws)d.r.y-=32;
        const int rows=std::min(32,roi.h-y);const unsigned area=unsigned(roi.w*rows);
        View view(roi.w,rows);
        {HmmImagePins pins;raster(view,own,pixels.data(),{0,0,roi.w,rows},true);}
        for(unsigned i=0;i<area;++i){const unsigned mask=pixels[area+i];pixels[i]=!mask?translate[pixels[i]]:mask<20?effects[mask]:0;}
        const unsigned count=(area+3)&~3u;for(unsigned i=area;i<count;++i)pixels[i]=0;
        uint32_t target=address+unsigned(y*roi.w);
        if(split){
            if(!hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,count,&target,&upload))hmm_fail("Window sprite RAM_G capacity");
            strips[y/32]=target;map_compiling=&p;map_keep_texture();map_compiling=nullptr;
        }
        put(target,reinterpret_cast<const uint32_t*>(pixels.data()),count/4);
        hmm_audio_poll();
    }
    map_compiling=&p;reset_emitter();
    Draw sprite;sprite.r=roi;sprite.mapping=0x400000;
    const uint32_t context[]={0x04ffffff,0x05000000,0x100000ff,0x0b000014,0x15000100,0x16000000,
        0x17000000,0x18000000,0x19000100,0x1a000000,0x1b000000,0x1c2801e0,0x27000004,0x2b000000,0x2c000000};
    for(auto v:context)word(v);
    for(const auto &d:f.geometry)if(d.raw&&d.fill)emit(d);
    for(int y=0;y<roi.h;y+=split?32:roi.h){
        sprite.r.y=roi.y+y;sprite.r.h=std::min(split?32:roi.h,roi.h-y);
        const Texture texture{split?strips[y/32]:address,unsigned(roi.w),unsigned(sprite.r.h),0};
        emit_textured(sprite,texture,false);
    }
    f.packed=true;
    char log[128];std::snprintf(log,sizeof(log),"HMM2 WINDOW sprite=%ux%u bytes=%u strips=%u prepare=%u",unsigned(roi.w),unsigned(roi.h),bytes,split?unsigned((roi.h+31)/32):1u,unsigned(hmm_core->millis()-started));hmm_log(log);
}

// Fixed-Huffman zlib stream, using only distance-one runs. No compression
// workspace or dictionary; the standard FT CMD_INFLATE consumes this stream.
HMM_MESSAGE_FN unsigned message_mask_deflate(const uint8_t *in,unsigned n,uint8_t *out){
    unsigned used=0,bits=0,held=0;out[used++]=0x78;out[used++]=1;
    auto put=[&](unsigned v,unsigned count){held|=v<<bits;bits+=count;while(bits>=8){out[used++]=uint8_t(held);held>>=8;bits-=8;}};
    auto symbol=[&](unsigned v){unsigned code,count;if(v<144){code=v+48;count=8;}else if(v<256){code=v+256;count=9;}else if(v<280){code=v-256;count=7;}else{code=v-88;count=8;}
        unsigned reverse=0;for(unsigned i=0;i<count;++i){reverse=(reverse<<1)|(code&1);code>>=1;}put(reverse,count);};
    static const uint16_t lengths[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    put(3,3);
    for(unsigned i=0;i<n;){symbol(in[i++]);unsigned count=0;while(i+count<n&&count<258&&in[i+count]==in[i-1])++count;
        if(count>=3){unsigned k=0;while(k<28&&lengths[k+1]<=count)++k;symbol(257+k);put(count-lengths[k],k<8||k==28?0:(k-4)/4);put(0,5);i+=count;}}
    symbol(256);if(bits)out[used++]=uint8_t(held);
    unsigned a=1,b=0;for(unsigned i=0;i<n;++i){a+=in[i];b+=a;if(!(i&4095)){a%=65521;b%=65521;}}a%=65521;b%=65521;
    const uint32_t checksum=(b<<16)|a;for(int shift=24;shift>=0;shift-=8)out[used++]=uint8_t(checksum>>shift);
    return used;
}

// Compose only the window, never the adventure-map framebuffer. The FT
// coprocessor blits atlas runs into an unpublished surface. A palette mask
// carries shading to scanout; ESP calculates coverage, not background colors.
HMM_MESSAGE_FN void message_window_blits(MessageFade &f){
    const auto started=hmm_core->millis();auto &p=f.foreground.part;MessageCommands previous;std::swap(p,previous.part);
    Rect roi{};bool first=true;
    for(const auto &d:f.geometry){const auto r=intersect(d.r,{0,0,640,480});if(!r.w||!r.h)continue;
        if(first){roi=r;first=false;}else{const int right=std::max(roi.x+roi.w,r.x+r.w),bottom=std::max(roi.y+roi.h,r.y+r.h);
            roi.x=std::min(roi.x,r.x);roi.y=std::min(roi.y,r.y);roi.w=right-roi.x;roi.h=bottom-roi.y;}}
    if(first)hmm_fail("Empty window blit");
    using Surface=WindowSurface;
    auto allocate=[&](unsigned bytes){Surface a;int upload;
        a.split=
#ifdef HMM2_HOST
            std::getenv("HMM2_FORCE_BLIT_STRIPS")||
#endif
            !hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,bytes,&a.at[0],&upload);
        map_compiling=&p;
        if(!a.split)map_keep_texture();else for(int y=0;y<roi.h;){int rows=std::min(256,roi.h-y);uint32_t at;
            while(!hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,(unsigned(roi.w*rows)+3)&~3u,&at,&upload)){
                if(rows<=8){texture_missing(roi.w*rows,"window blit");hmm_fail("Window blit RAM_G capacity");}rows=std::max(8,(rows/2)&~7);}
            map_keep_texture();for(int k=0;k<rows;k+=8)a.at[(y+k)/8]=at+k*roi.w;y+=rows;}
        map_compiling=nullptr;return a;};
    const unsigned bytes=(unsigned(roi.w*roi.h)+3)&~3u;const auto color=allocate(bytes);
    auto address=[&](const Surface &a,int x,int y){return (a.split?a.at[y/8]+(y%8)*roi.w:a.at[0]+y*roi.w)+x;};
    const bool reuse=
#ifdef HMM2_HOST
        !std::getenv("HMM2_FULL_WINDOW_BLITS")&&
#endif
        !f.blit_before.empty()&&!std::memcmp(&roi,&f.blit_roi,sizeof(roi));Rect dirty=roi;
    auto same=[](const Draw &a,const Draw &b){return !std::memcmp(&a.r,&b.r,sizeof(Rect))&&a.sx==b.sx&&a.sy==b.sy&&a.step==b.step&&a.resource==b.resource&&a.mapping==b.mapping&&a.raw==b.raw&&a.opaque==b.opaque&&a.fill==b.fill&&a.repeat==b.repeat&&a.cpu==b.cpu&&a.layer==b.layer&&a.color==b.color&&a.mask==b.mask&&a.bitmap==b.bitmap;};
    if(reuse){size_t head=0,tail=0;const auto &old=f.blit_before;const auto &now=f.geometry;
        while(head<old.size()&&head<now.size()&&same(old[head],now[head]))++head;
        while(tail<old.size()-head&&tail<now.size()-head&&same(old[old.size()-1-tail],now[now.size()-1-tail]))++tail;
        bool empty=true;auto add=[&](const Rect &r){if(empty){dirty=r;empty=false;}else{const int right=std::max(dirty.x+dirty.w,r.x+r.w),bottom=std::max(dirty.y+dirty.h,r.y+r.h);dirty.x=std::min(dirty.x,r.x);dirty.y=std::min(dirty.y,r.y);dirty.w=right-dirty.x;dirty.h=bottom-dirty.y;}};
        for(size_t i=head;i<old.size()-tail;++i)add(old[i].r);for(size_t i=head;i<now.size()-tail;++i)add(now[i].r);
        dirty=empty?Rect{roi.x,roi.y,0,0}:intersect(dirty,roi);
    }else {f.blit_pixels.assign(unsigned(roi.w*roi.h),0);f.blit_colors.fill(0);f.blit_colors_used=1;}
    auto &effects=f.blit_pixels;auto &colors=f.blit_colors;auto &colors_used=f.blit_colors_used;std::vector<uint8_t> blend(256*16,255);
    for(int y=dirty.y;y<dirty.y+dirty.h;++y)std::memset(effects.data()+(y-roi.y)*roi.w+dirty.x-roi.x,0,dirty.w);
    auto shade=[&](uint8_t old,unsigned mask){
        auto &code=blend[unsigned(old)*16+mask];if(code!=255)return code;
        static const unsigned alpha[]={0,0,88,60,34,17};
        const uint32_t src=mask<=5?alpha[mask]<<24:lighten_argb[mask-6],dst=colors[old];
        const unsigned sa=src>>24,da=dst>>24,a=sa+da*(255-sa)/255;
        uint32_t rgba=a<<24;for(unsigned shift:{0u,8u,16u}){const unsigned g=a?((((src>>shift)&255)*sa+((dst>>shift)&255)*da*(255-sa)/255)/a):0;rgba|=g<<shift;}unsigned id=0;
        while(id<colors_used&&colors[id]!=rgba)++id;
        if(id==colors_used){if(id>=255)hmm_fail("Window shade palette capacity");colors[colors_used++]=rgba;}
        return code=uint8_t(id);};
    std::vector<uint32_t> commands(1020);unsigned count=0,runs=0;
    auto flush=[&](){if(count){copro_send(commands.data(),count);copro_wait();count=0;}};
    auto operation=[&](uint32_t op,uint32_t dst,uint32_t src,unsigned size){if(count+4>commands.size())flush();
        commands[count++]=op;commands[count++]=dst;commands[count++]=src;commands[count++]=size;++runs;};
    copro_wait();
    if(reuse)for(int y=0;y<roi.h;y+=8)operation(0xffffff1du,address(color,0,y),address(f.blit_color,0,y),unsigned(roi.w*std::min(8,roi.h-y)));
    for(int y=dirty.y;y<dirty.y+dirty.h;++y)operation(0xffffff1bu,address(color,dirty.x-roi.x,y-roi.y),0,unsigned(dirty.w));
    struct Guard{bool saved=materializing;Guard(){materializing=true;}~Guard(){materializing=saved;}} guard;
    for(const auto &item:f.geometry){const Draw d=clip(item,dirty);if(!d.r.w||!d.r.h)continue;
        if(d.layer||d.repeat||d.color||d.step!=1||(!d.fill&&d.mapping))hmm_fail("Unsupported window blit");
        HmmImagePins pins;uint32_t prior=0;const uint8_t *mask=nullptr;int stride=0;
        const HmmFtImage *ft=nullptr;bool strips=false;unsigned rows=0,loaded=~0u;Texture t{};
        if(!d.fill){
            if(d.bitmap){bitmap_materialize(*d.bitmap);stride=d.bitmap->w;if(!d.bitmap->single)mask=d.bitmap->pixels.data()+stride*d.bitmap->h;}
            else {prior=hmm_resource_pin(d.resource);const auto &im=hmm_resource_get(d.resource);stride=im.width();if(!d.raw&&!im.singleLayer())mask=im.transform();
                ft=&hmm_resource_ft(d.resource,d.raw);strips=ft->bytes>=STRIP_MIN_BYTES&&!hft_cache_ready(&textures,UINT64_C(0x1000000000000000)|ft->offset,ft->bytes);if(strips)rows=strip_rows(ft->stride);}
            if(!strips)t=texture(d);
            if(t.missing)hmm_fail("Window blit texture missing");
        }
        for(int y=0;y<d.r.h;++y){const int dy=d.r.y-roi.y+y,dx=d.r.x-roi.x;
            auto *effect=effects.data()+dy*roi.w+dx;
            if(d.fill){
                if(d.mask){if(d.mask!=1)for(int x=0;x<d.r.w;++x)effect[x]=shade(effect[x],d.mask);}
                else {operation(0xffffff1bu,address(color,dx,dy),hmm_resource_palette()[d.mapping?mappings[d.mapping-1][d.color]:d.color],unsigned(d.r.w));std::memset(effect,0,d.r.w);}continue;
            }
            if(strips&&unsigned((d.sy+y)/int(rows))!=loaded){flush();
                if(loaded!=~0u&&!(textures.entries[textures.last_entry].pins&4u))textures.entries[textures.last_entry].pins&=uint8_t(~(1u<<textures.build_slot));
                loaded=unsigned(d.sy+y)/rows;t=texture_strip(*ft,loaded,rows);if(t.missing)hmm_fail("Window blit strip missing");}
            for(int x=0;x<d.r.w;){const unsigned m=mask&&!d.raw?mask[(d.sy+y)*stride+d.sx+x]:0;
                if(m){if(m>1)effect[x]=shade(effect[x],m);++x;continue;}
                const int begin=x++;while(x<d.r.w&&(!mask||d.raw||!mask[(d.sy+y)*stride+d.sx+x]))++x;
                const unsigned sy=strips?unsigned(d.sy+y)%rows:unsigned(d.sy+y);
                operation(0xffffff1du,address(color,dx+begin,dy),t.address+(t.cell*t.height+sy)*t.stride+d.sx+begin,unsigned(x-begin));std::memset(effect+begin,0,x-begin);
            }
        }
        if(strips){flush();if(!(textures.entries[textures.last_entry].pins&4u))textures.entries[textures.last_entry].pins&=uint8_t(~(1u<<textures.build_slot));}
        if(d.bitmap)bitmap_release_pixels(*d.bitmap);else if(!d.fill)hmm_resource_restore_pin(d.resource,prior);
    }
    flush();
    // Source atlases have finished their copies. Only background and output
    // leases are needed for scanout; release temporary build pins before mask allocation.
    for(auto &entry:textures.entries)if(entry.valid&&!(entry.pins&4u))entry.pins&=uint8_t(~(1u<<textures.build_slot));
    const auto effect=allocate(bytes);std::vector<uint32_t> packed(6000);
    if(reuse)for(int y=0;y<roi.h;y+=8)operation(0xffffff1du,address(effect,0,y),address(f.blit_effect,0,y),unsigned(roi.w*std::min(8,roi.h-y)));flush();
    for(int y=dirty.y-roi.y;y<dirty.y+dirty.h-roi.y;){const int rows=std::min({effect.split?8-y%8:32,dirty.y+dirty.h-roi.y-y,roi.h-y});const unsigned size=unsigned(roi.w*rows);
        const unsigned used=message_mask_deflate(effects.data()+y*roi.w,size,reinterpret_cast<uint8_t*>(packed.data()));
        std::memset(reinterpret_cast<uint8_t*>(packed.data())+used,0,(-used)&3u);
        const uint32_t header[]={0xffffff22u,address(effect,0,y)};copro_send(header,2);
        for(unsigned at=0,n=(used+3)/4;at<n;){const unsigned k=std::min(1020u,n-at);copro_send(packed.data()+at,k);copro_wait();at+=k;}
        y+=rows;
    }
    uint32_t palette_location;int upload;
    if(!hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,1024,&palette_location,&upload))hmm_fail("Window shade RAM_G capacity");
    map_compiling=&p;map_keep_texture();put(palette_location,colors.data(),256);reset_emitter();
    const uint32_t context[]={0x04ffffff,0x05000000,0x100000ff,0x0b000014,0x15000100,0x16000000,0x17000000,0x18000000,0x19000100,0x1a000000,0x1b000000,0x1c2801e0,0x27000004,0x2b000000,0x2c000000};
    for(auto v:context)word(v);
    for(unsigned layer=0;layer<2;++layer){const auto &a=layer?effect:color;Draw sprite;sprite.r=roi;sprite.mapping=layer?0x800000u|palette_location:0;
        for(int y=0;y<roi.h;){int h=roi.h-y;if(a.split){h=std::min(8,h);while(y+h<roi.h&&a.at[(y+h)/8]==a.at[y/8]+h*roi.w)h+=std::min(8,roi.h-y-h);}
            sprite.r.y=roi.y+y;sprite.r.h=h;emit_textured(sprite,{a.split?a.at[y/8]:a.at[0],unsigned(roi.w),unsigned(h),0},false);y+=h;}}
    map_compiling=nullptr;reset_emitter();f.packed=true;
    f.blit_before=f.geometry;f.blit_roi=roi;f.blit_color=color;f.blit_effect=effect;
    char log[128];std::snprintf(log,sizeof(log),"HMM2 WINDOW blits=%u colors=%u dirty=%ux%u prepare=%u",runs,colors_used,unsigned(dirty.w),unsigned(dirty.h),unsigned(hmm_core->millis()-started));hmm_log(log);
}

HMM_MESSAGE_FN int message_text_row(const MessageFade &f,const Draw &d){
    const unsigned group=d.resource>>16;
    if((group!=ICN::FONT&&group!=ICN::YELLOW_FONT)||d.bitmap||d.fill||d.mapping||d.color||d.step!=1||f.text_height<=0)return -1;
    const auto &sprite=static_cast<const fheroes2::Sprite&>(hmm_resource_get(d.resource));
    const int origin=d.r.y-sprite.y()-d.sy;
    if(origin<f.text_area.y||origin>=f.text_area.y+f.text_area.h||d.r.x<f.text_area.x||d.r.x+d.r.w>f.text_area.x+f.text_area.w||d.r.y<origin-4||d.r.y+d.r.h>origin+f.text_height+4)return -1;
    return origin;
}
HMM_MESSAGE_FN WindowTextRow *message_cached_row(MessageFade &f,const Sequence<Draw> &glyphs){
    uint32_t hash=2166136261u;
    for(const auto &d:glyphs)for(unsigned value:{unsigned(d.resource),unsigned(d.r.x),unsigned(d.r.y),unsigned(d.r.w),unsigned(d.r.h),unsigned(d.sx),unsigned(d.sy),unsigned(d.raw)})hash=(hash^value)*16777619u;
    for(auto &entry:f.text_rows)if(entry&&entry->hash==hash&&entry->glyphs.size()==glyphs.size()){
        bool same=true;for(size_t i=0;i<glyphs.size()&&same;++i){const auto &a=entry->glyphs[i],&b=glyphs[i];same=a.resource==b.resource&&a.sx==b.sx&&a.sy==b.sy&&a.raw==b.raw&&!std::memcmp(&a.r,&b.r,sizeof(Rect));}
        if(same){entry->age=++f.text_age;++f.row_hits;return entry.get();}
    }
    for(size_t i=0;i<glyphs.size();++i)for(size_t j=0;j<i;++j){const auto r=intersect(glyphs[i].r,glyphs[j].r);if(r.w&&r.h)return nullptr;}
    size_t slot=f.text_rows.size();
    if(slot>=32){unsigned oldest=~0u;slot=0;for(size_t i=0;i<f.text_rows.size();++i){const auto &row=f.text_rows[i];bool used=false;
            if(!row){oldest=0;slot=i;break;}
            for(auto texture:row->lease.part.textures)used|=std::find(f.foreground.part.textures.begin(),f.foreground.part.textures.end(),texture)!=f.foreground.part.textures.end();
            if(!used&&row->age<oldest){oldest=row->age;slot=i;}}
        if(oldest==~0u)return nullptr;f.text_rows[slot].reset();
    }else f.text_rows.emplace_back();
    auto row=std::make_unique<WindowTextRow>();row->glyphs=glyphs;row->hash=hash;row->age=++f.text_age;
    const unsigned stride=unsigned(f.text_area.w),height=unsigned(f.text_height+8),bytes=(stride*height+3)&~3u;int upload;
    auto *const saved=map_compiling;
    if(!hft_cache_get(&textures,UINT64_C(0x3000000000000000)|bitmap_id++,bytes,&row->address,&upload))return nullptr;
    map_compiling=&row->lease.part;map_keep_texture();map_compiling=nullptr;
    std::vector<uint32_t> commands;commands.reserve(1020);
    auto flush=[&](){if(!commands.empty()){copro_send(commands.data(),unsigned(commands.size()));copro_wait();commands.clear();}};
    auto copy=[&](uint32_t op,uint32_t dst,uint32_t src,unsigned size){if(commands.size()+4>1020)flush();commands.push_back(op);commands.push_back(dst);commands.push_back(src);commands.push_back(size);};
    copy(0xffffff1bu,row->address,0,bytes);
    for(const auto &d:glyphs){const auto t=texture(d);if(t.missing)hmm_fail("Window row atlas missing");
        for(int y=0;y<d.r.h;++y)copy(0xffffff1du,row->address+unsigned(d.r.y+y)*stride+d.r.x,t.address+(t.cell*t.height+d.sy+y)*t.stride+d.sx,unsigned(d.r.w));}
    flush();map_compiling=saved;++f.row_builds;f.text_rows[slot]=std::move(row);return f.text_rows[slot].get();
}

// Retain only the window's immutable chrome. Text, selection and controls stay
// real FT bitmap vertices, so scrolling never copies thousands of glyph runs.
HMM_MESSAGE_FN void message_window_hybrid(MessageFade &f,bool rows){
    auto &p=f.foreground.part;
    for(auto i:p.textures)textures.entries[i].pins&=uint8_t(~(1u<<textures.build_slot));map_cache_release(p);
    if(!f.chrome){
        auto geometry=std::move(f.geometry);f.geometry=f.chrome_geometry;f.blit_before.clear();
        message_window_blits(f);f.geometry=std::move(geometry);
        f.chrome=std::make_unique<MessageCommands>();std::swap(f.chrome->part,p);
        f.blit_before.clear();std::vector<uint8_t>().swap(f.blit_pixels);
    }
    struct CompileGuard {MapPart *saved=message_compiling;~CompileGuard(){message_compiling=saved;map_compiling=nullptr;reset_emitter();}} guard;
    map_compiling=message_compiling=&p;reset_emitter();texture_missed=false;
    for(auto v:f.chrome->part.commands)word(v);
    for(auto i:f.chrome->part.textures){p.textures.push_back(i);++map_texture_refs[i];textures.entries[i].pins|=4u;}
    reset_emitter();
    const uint32_t context[]={0x04ffffff,0x05000000,0x100000ff,0x0b000014,0x15000100,0x16000000,0x17000000,0x18000000,0x19000100,0x1a000000,0x1b000000,0x1c2801e0,0x27000004,0x2b000000,0x2c000000};
    for(auto v:context)word(v);
    Sequence<Draw> dynamic;
    for(const auto &d:f.geometry){bool base=false;
        for(const auto &s:f.chrome_geometry)if(d.resource==s.resource&&d.bitmap==s.bitmap&&d.fill==s.fill&&d.raw==s.raw&&d.mapping==s.mapping&&d.mask==s.mask&&d.color==s.color&&d.step==s.step&&
            d.r.x-d.sx*d.step==s.r.x-s.sx*s.step&&d.r.y-d.sy==s.r.y-s.sy){base=true;break;}
        if(!base)dynamic.push_back(d);
    }
    std::vector<Sequence<Draw>> lines;std::vector<int> positions;std::vector<uint8_t> emitted;
    if(rows){
        for(const auto &d:dynamic){const int origin=message_text_row(f,d);if(origin>=0){auto at=std::find(positions.begin(),positions.end(),origin);const size_t index=size_t(at-positions.begin());
                if(at==positions.end()){positions.push_back(origin);lines.emplace_back();}auto glyph=d;glyph.r.x-=f.text_area.x;glyph.r.y-=origin-4;lines[index].push_back(std::move(glyph));}}
        emitted.resize(lines.size());
    }
    for(const auto &d:dynamic){const int origin=rows?message_text_row(f,d):-1;
        if(origin<0){emit(d);continue;}const size_t index=size_t(std::find(positions.begin(),positions.end(),origin)-positions.begin());if(emitted[index])continue;emitted[index]=1;
        auto *const row=message_cached_row(f,lines[index]);
        if(!row){for(auto glyph:lines[index]){glyph.r.x+=f.text_area.x;glyph.r.y+=origin-4;emit(glyph);}continue;}
        for(auto i:row->lease.part.textures)if(std::find(p.textures.begin(),p.textures.end(),i)==p.textures.end()){p.textures.push_back(i);++map_texture_refs[i];textures.entries[i].pins|=4u;}
        Draw sprite;sprite.r={f.text_area.x,origin-4,f.text_area.w,f.text_height+8};
        emit_textured(sprite,{row->address,unsigned(f.text_area.w),unsigned(f.text_height+8),0},false);
    }
    f.packed=false;f.window_mode=rows?2:1;f.window_missing=texture_missed;
#ifdef HMM2_HOST
    std::printf("WINDOW_HYBRID words=%zu live=%zu rows=%u builds=%u hits=%u\n",p.commands.size(),dynamic.size(),unsigned(rows),f.row_builds,f.row_hits);
#endif
}

// Keep every draw, including repeated translucent vertices. A short standard
// CALL/RETURN block stores a repeated run once. Run this after all word-removal
// passes: RAM_DL branch targets are word indices. Retained scenes stay uncompressed.
HMM_MESSAGE_FN void message_repeat_vertices(){
    for(unsigned i=0;i<words;++i){const unsigned op=dl[i]>>24;if(op==0x1d||op==0x1e||op==0x24)return;}
    unsigned out=0,anchor=0;
    for(unsigned i=0;i<words;){const uint32_t vertex=dl[i];unsigned n=1;
        if((vertex>>30)==2)while(i+n<words&&dl[i+n]==vertex)++n;
        const unsigned calls=n/4,rest=n%4,target=out+calls+rest+1,end=target+5;
        bool pinned=false;for(unsigned j=anchor;j<cursor_pending_count;++j)if(cursor_pending[j].index>=i&&cursor_pending[j].index<i+n)pinned=true;
        if(n>=12&&!pinned&&end<DL_LIMIT){
            for(unsigned j=0;j<calls;++j)dl[out++]=0x1d000000|target;
            for(unsigned j=0;j<rest;++j)dl[out++]=vertex;
            dl[out++]=0x1e000000|end;for(unsigned j=0;j<4;++j)dl[out++]=vertex;dl[out++]=0x24000000;i+=n;
        }else{if(anchor<cursor_pending_count&&cursor_pending[anchor].index==i)cursor_pending[anchor++].index=uint16_t(out);dl[out++]=dl[i++];}
    }
    words=out;
}

HMM_MESSAGE_FN void message_transition_frame(const uint8_t *rgb){
    auto &f=*message_transition;
    hmm_audio_tick();
    const bool build=f.program.empty()||!message_alpha;
    hft_cache_begin(&textures,displayed);words=0;dl_overflow=false;reset_emitter();
    std::memset(compact_valid,0,sizeof(compact_valid));std::memset(compact_mask,0,sizeof(compact_mask));compact_handle=0;
    if(build){
    word(0x02000000);word(0x26000007);word(0x1c2801e0);
    if(f.background){cached_textures_use(f.background->part);for(auto v:f.background->part.commands)word(v);}
    if(message_alpha){
        cached_textures_use(f.foreground.part);
        palette_mapping=~0u;
        for(auto v:f.foreground.part.commands){
            if((v>>24)==0x2a){
                const auto address=palette_address(v&0xffffff);v=0x2a000000|address;
                const unsigned slot=palette_entry_by_page[address/HFT_PAGE_BYTES];
                if(slot){auto &refs=f.foreground.part.textures;const unsigned index=slot-1;
                    if(std::find(refs.begin(),refs.end(),index)==refs.end()){
                        refs.push_back(index);++map_texture_refs[index];textures.entries[index].pins|=4u;
                    }
                }
            }
            else if((v>>24)==0x10)v|=0x10000;
            word(v);
        }
        word(0x100000ff);
    }
    if(words>REHANDLE_FROM&&!dl_overflow)rehandle_dl();
#ifdef HMM2_HOST
    if(std::getenv("HMM2_FT_WINDOW_DUMP"))std::printf("WINDOW_DL serial=%u words=%u packed=%d\n",serial,words,int(f.packed));
#endif
    // Retain the independent scene before cursor emission and before bitmap
    // constants can be omitted using the previous display list's state.
    if(!dl_overflow)f.scene.assign(dl,dl+words);
    // Build current cursor positions and patch anchors using the same FT path
    // as normal frames; the scene alpha never reaches the cursor.
    reset_emitter();word(0x100000ff);set(0x1b000000);set(0x1c2801e0);
    cursor_pending_count=0;cursor_overflow=false;
    int mx,my;
    if(cursor_live())hmm_loading_mouse(&mx,&my);
    else {const auto &m=LocalEvent::Get().getMouseCursorPos();mx=m.x;my=m.y;}
    f.mouse_x=mx;f.mouse_y=my;
    cursor_record=true;cursor_layer=0;
    if(cursor_image&&!cursor_image->empty())for(auto d:canvas(*cursor_image).draws){
        d.r.x+=cursor_x+mx-cursor_origin_x;d.r.y+=cursor_y+my-cursor_origin_y;
        if(d.r.w&&d.r.h)emit(d);
    }
    cursor_record=false;word(0x21000000);word(0);
    const bool compacted=!dl_overflow&&words>DL_LIMIT;
    if(compacted){if(!bitmap_prior_valid)previous_bitmap_state();compact_bitmap_constants();}
    if(!dl_overflow&&words>DL_LIMIT){bool text_window=false;for(const auto &layer:message_fades)text_window|=layer->blit;if(text_window)message_repeat_vertices();}
    if((f.window_missing||dl_overflow||words>DL_LIMIT)&&f.blit&&f.window_mode<3){
#ifdef HMM2_HOST
        std::printf("WINDOW_REJECT mode=%u words=%u missing=%u\n",f.window_mode,words,unsigned(f.window_missing));
#endif
        GpuBusy busy;
        if(!f.window_mode&&!f.chrome_rejected)message_window_hybrid(f,false);
        else if(f.window_mode<2&&!f.rows_rejected){f.chrome_rejected=true;message_window_hybrid(f,true);}
        else {
            auto &p=f.foreground.part;for(auto i:p.textures)textures.entries[i].pins&=uint8_t(~(1u<<textures.build_slot));map_cache_release(p);
            f.chrome.reset();f.text_rows.clear();f.chrome_rejected=f.rows_rejected=true;f.window_missing=false;
            message_window_blits(f);f.window_mode=3;
        }
        return message_transition_frame(rgb);
    }
    if((dl_overflow||words>DL_LIMIT)&&!f.packed){
        message_window_sprite(f);return message_transition_frame(rgb);
    }
    if(dl_overflow||words>DL_LIMIT){
#ifdef HMM2_HOST
        if(const char *path=std::getenv("HMM2_FADE_OVERFLOW")){FILE *file=std::fopen(path,"wb");if(file){std::fwrite(dl,4,words,file);std::fclose(file);}}
#endif
        hmm_fail("Window fade display list exceeds 8 KiB");
    }
    f.program.assign(dl,dl+words);
    if(f.blit&&f.window_report){
        char log[136];std::snprintf(log,sizeof(log),"HMM2 WINDOW draw=%s words=%u prepare=%u rows=%u/%u",f.window_mode==0?"direct":f.window_mode==1?"hybrid":f.window_mode==2?"rows":"blit",words,unsigned(hmm_core->millis()-f.window_started),f.row_builds,f.row_hits);hmm_log(log);f.window_report=false;
    }
    if(f.blit&&f.window_mode<3){f.blit_before.clear();std::vector<uint8_t>().swap(f.blit_pixels);}
    }
    words=unsigned(f.program.size());std::copy(f.program.begin(),f.program.end(),dl);
    for(unsigned i=0;i<words;++i)if((dl[i]&0xffff0000u)==0x10010000u)
        dl[i]=0x10000000u|(((dl[i]&255)*message_alpha+127)/255);
    if(f.background)cached_textures_use(f.background->part);
    // The alpha-zero list has no foreground commands or foreground leases.
    if(message_alpha)cached_textures_use(f.foreground.part);
    if(message_alpha==255&&(build||message_scene==f.background)){
        if(!message_scene||!message_scene.unique())message_scene=std::make_shared<MessageCommands>();
        auto &p=message_scene->part;map_cache_release(p);p.commands=f.scene;
        for(auto &v:p.commands)if((v&0xffff0000u)==0x10010000u)v=0x10000000u|(v&255);
        for(const auto *part:{f.background?&f.background->part:nullptr,&f.foreground.part})if(part)
            for(auto i:part->textures)if(std::find(p.textures.begin(),p.textures.end(),i)==p.textures.end()){
                p.textures.push_back(i);++map_texture_refs[i];textures.entries[i].pins|=4u;
            }
    }
    int mx,my;if(cursor_live())hmm_loading_mouse(&mx,&my);
    else {const auto &m=LocalEvent::Get().getMouseCursorPos();mx=m.x;my=m.y;}
    for(unsigned i=0;i<cursor_pending_count;++i){const auto &p=cursor_pending[i];const int delta=(p.base>>24)==0x2c?my-f.mouse_y:mx-f.mouse_x;
        dl[p.index]=(p.base&0xff000000u)|((p.base+delta*16)&0x1ffffu);}
    GpuBusy busy;Prof prof_publish(P_PUBLISH);
    uint32_t v;const auto start=hmm_core->millis();
    do{if(hmm_core->ft_read(0x302054,&v)!=ZM_OK)hmm_fail("FT DLSWAP read");
        if(hmm_core->millis()-start>2000)hmm_fail("FT DLSWAP timeout");}while(v);
    publish_dl();v=2;put(0x302054,&v,1);displayed^=1;
#ifdef HMM2_HOST
    if(!message_alpha&&std::getenv("HMM2_TEST_FADE_PINS")){
        unsigned only=0,pinned=0,bytes=0;
        for(auto i:f.foreground.part.textures){
            if(f.background&&std::find(f.background->part.textures.begin(),f.background->part.textures.end(),i)!=f.background->part.textures.end())continue;
            ++only;if(textures.entries[i].pins&(1u<<textures.build_slot)){++pinned;bytes+=textures.entries[i].bytes;}
        }
        std::printf("WINDOW_CLOSED_PINS foreground_only=%u falsely_displayed=%u bytes=%u\n",only,pinned,bytes);
        if(std::getenv("HMM2_EXPECT_RELEASED_FOREGROUND")&&pinned)hmm_fail("Closed foreground remains scanout-pinned");
    }
#endif
    std::memcpy(lastdl,dl,words*4);lastwords=words;bitmap_prior_valid=false;
    cursor_count=cursor_overflow?0:cursor_pending_count;
    std::copy(cursor_pending,cursor_pending+cursor_count,cursor_committed);
    cursor_anchor_x=f.mouse_x;cursor_anchor_y=f.mouse_y;status_reveal_at.store(0);
    ++serial;
    if(message_alpha!=255||!f.shown){
        char log[96];std::snprintf(log,sizeof(log),"HMM2 WINDOW fade=%u depth=%u words=%u",message_alpha,unsigned(message_fades.size()),words);hmm_log(log);
    }
    (void)rgb;
}
HMM_MESSAGE_FN void message_fade_close(){
    auto &f=*message_fades.back();
    if(f.popup&&f.shown&&hmm_gpu_restore_allowed()){
        try{
            message_prepare(f);
            f.program.clear();message_transition=&f;
            for(unsigned alpha:{170u,85u,0u}){message_alpha=alpha;hmm_video_frame(nullptr,last_palette,640,480);}
            message_scene=f.background;
        }catch(...){hmm_window_restore_failed();}
    }
    message_transition=nullptr;message_fades.pop_back();
}
}
HMM_MESSAGE_FN bool hmm_gpu_message_owned(){return !message_fades.empty()&&message_fades.back()->popup;}
HMM_MESSAGE_FN void hmm_gpu_window_blits(int x,int y,int w,int h,int height){
#ifdef HMM2_HOST
    if(std::getenv("HMM2_DISABLE_WINDOW_BLITS"))return;
#endif
    if(!message_fades.empty()&&message_fades.back()->popup){auto &f=*message_fades.back();f.blit=true;f.chrome_geometry=canvas(*message_layers.back()).draws;f.text_area={x,y,w,h};f.text_height=height;}
}
HMM_MESSAGE_FN void hmm_gpu_message_present(const uint8_t *rgb,int w,int h){
    if(message_fades.empty()){hmm_video_frame(nullptr,rgb,w,h);return;}
    if(!hmm_gpu_restore_allowed())return;
    auto &f=*message_fades.back();
    if(!f.popup){hmm_video_frame(nullptr,rgb,w,h);f.shown=true;return;}
    if(!palette_epoch||std::memcmp(rgb,last_palette,768)){
        ++palette_epoch;std::memcpy(last_palette,rgb,768);
        for(unsigned i=0;i<256;++i)palette_rgb[i]=0xff000000|(uint32_t(rgb[i*3])<<16)|(uint32_t(rgb[i*3+1])<<8)|rgb[i*3+2];
    }
    // ImageRestorer can clear and render this layer before its owner is
    // destroyed. Keep the last visible foreground until close animates it.
    if(f.shown&&canvas(*message_layers.back()).draws.empty())return;
    message_geometry_update(f);
    if(f.geometry.empty()){hmm_video_frame(nullptr,rgb,w,h);return;}
    const bool opening=!f.shown;
#ifdef HMM2_HOST
    if(f.shown&&f.dirty&&std::getenv("HMM2_POPUP_UPDATE_TRACE"))std::printf("WINDOW_UPDATE packed=%u revision=%u\n",unsigned(f.packed),f.revision);
#endif
    if(opening||f.dirty){message_scene=f.background;message_prepare(f);f.program.clear();}
    // Palette data is initialized by the ordinary renderer before the first
    // dialog; the foreground uses those same immutable original textures.
    message_transition=&f;
    try{
        if(opening)for(unsigned alpha:{85u,170u,255u}){message_alpha=alpha;hmm_video_frame(nullptr,rgb,w,h);}
        else {message_alpha=255;hmm_video_frame(nullptr,rgb,w,h);}
    }
    catch(...){message_transition=nullptr;throw;}
    message_transition=nullptr;f.shown=true;
}
extern "C" HMM_MESSAGE_FN void hmm_gpu_frame(const uint8_t *pixels,const uint8_t *rgb,int w,int h){
    if(message_transition){
        Prof prof_frame(P_FRAME);const auto start=hmm_core->millis();
        if(loader_active)loader_finish();
        message_transition_frame(rgb);
        prepare_ms=unsigned(hmm_core->millis()-start)-link_ms;
        frame_bytes=traffic;frame_link=link_ms;traffic=link_ms=gpu_upload_bytes=0;
    }
    else hmm_gpu_scene_frame(pixels,rgb,w,h);
}

#ifndef HMM2_HOST
#pragma GCC pop_options
#endif

#undef HMM_MESSAGE_FN
