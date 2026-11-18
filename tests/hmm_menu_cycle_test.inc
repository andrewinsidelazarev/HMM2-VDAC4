// Native-only regression. Executes actual PAL::GetCyclingPalette, renderer and FT path.
#ifdef HMM2_PACKED
#include "render_processor.h"
static int hmm_test_menu_cycle(){
    auto &display=fheroes2::Display::instance();
    if(!Settings::Get().setGameLanguage("ru"))throw std::runtime_error("cycle Russian unavailable");
    auto &processor=fheroes2::RenderProcessor::instance();processor.stopColorCycling();
    std::array<std::array<uint8_t,768>,2> rgb;
    const auto *base=fheroes2::getGamePalette();
    FILE *maps=std::fopen("cycle_maps.bin","wb");if(!maps)throw std::runtime_error("cycle maps output");
    for(unsigned state=0;state<4;++state)for(unsigned step=0;step<20;++step){
        const auto mapping=PAL::GetCyclingPalette(step);
        if(state==0&&std::fwrite(mapping.data(),1,256,maps)!=256)throw std::runtime_error("cycle maps write");
        auto &palette=rgb[step&1];
        for(unsigned i=0;i<256;++i)for(unsigned c=0;c<3;++c)palette[i*3+c]=uint8_t(base[mapping[i]*3+c]<<2);
        display.changePalette(palette.data());
        fheroes2::drawMainMenuScreen();
        for(unsigned kind=0;kind<5;++kind){
            const auto &sprite=fheroes2::AGG::GetICN(ICN::BTNSHNGL,kind*4+state);
            fheroes2::Blit(sprite,display,sprite.x(),sprite.y());
        }
        display.render();
        std::printf("MENU_CYCLE state=%u step=%u frame=%u\n",state,step,hmm_host_frame_count());
    }
    std::fclose(maps);display.changePalette();processor.startColorCycling();
    std::puts("MENU CYCLE 20 PHASES 4 STATES COMPLETE\nSCENARIO COMPLETE");return 1;
}
#endif
