#ifdef HMM2_PACKED
#include "hmm_subtitles.h"
static int hmm_test_subtitles(){
    const char *path=std::getenv("HMM2_SUBTITLE_CASES");if(!path)throw std::runtime_error("subtitle cases missing");
    FILE *f=std::fopen(path,"rb");if(!f)throw std::runtime_error("subtitle cases open");
    auto u32=[&](){uint32_t v;if(std::fread(&v,4,1,f)!=1)throw std::runtime_error("subtitle cases header");return v;};
    if(u32()!=0x54534248u)throw std::runtime_error("subtitle cases magic");const unsigned tracks=u32();
    auto &display=fheroes2::Display::instance();display.fill(0);
    const auto target=hmm_gpu_clip_buffer(0,0,640,480);uint32_t pixels[1024];std::fill_n(pixels,1024,0x24682468u);
    for(unsigned at=0;at<640*480*2;at+=sizeof(pixels))if(hmm_core->ft_write(target+at,pixels,std::min<unsigned>(sizeof(pixels),640*480*2-at)/4)!=ZM_OK)throw std::runtime_error("subtitle fixture FT write");
    for(unsigned track=0;track<tracks;++track){
        char name[16];if(std::fread(name,1,sizeof(name),f)!=sizeof(name)||name[15])throw std::runtime_error("subtitle case name");
        const unsigned samples=u32();HmmVideoSubtitles captions({{name,Video::PLAY_CUTSCENE}});
        const bool enabled=hmm_ru_active()&&!std::getenv("HMM2_SUBTITLE_UNAVAILABLE");
        if(captions.active()!=enabled)throw std::runtime_error("subtitle optional/language selection");
        for(unsigned i=0;i<samples;++i){
            const unsigned time=u32(),present=u32();
            // Refresh the video layer every time, including identical frames and gaps.
            hmm_gpu_clip_show(display,0,0,0,target,640,480);captions.render(display,{0,0,640,480},time);
            phase("subtitle");display.render();
            std::printf("SUBTITLE SAMPLE track=%s time=%u present=%u frame=%u\n",name,time,enabled?present:0,hmm_host_frame_count());
        }
    }
    if(std::fgetc(f)!=EOF)throw std::runtime_error("subtitle cases trailing");std::fclose(f);
    display.fill(0);display.render();hmm_gpu_clip_end();std::puts("SUBTITLES SCENARIO COMPLETE\nSCENARIO COMPLETE");return 1;
}
#endif
