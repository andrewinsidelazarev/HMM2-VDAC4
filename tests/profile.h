// Codex: наблюдение за памятью, ресурсами и кадрами; правила игры не меняются.
#pragma once
#include <cstddef>
#include <cstdint>
void profile_install();
void profile_phase(const char *name);
void profile_finish_phase();
void profile_read(const char *name, size_t size);
void profile_frame(const uint8_t *pixels, int width, int height);
void profile_events();
int profile_run(int argc, char **argv);
uint64_t profile_cache_bytes();
#ifdef HMM2_STAGE0B
namespace fheroes2 { class Image; }
void profile_trace_fill(fheroes2::Image &out,int x,int y,int w,int h,uint8_t color);
void profile_trace_blit(const fheroes2::Image &in,int ix,int iy,fheroes2::Image &out,int ox,int oy,int w,int h,bool flip,bool copy);
#endif
