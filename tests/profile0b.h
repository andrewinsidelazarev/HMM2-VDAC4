// Codex: раздельный учёт пикселей и прочей кучи в 32-битном движке.
#pragma once
#include <cstddef>
#include <cstdint>
uint8_t *profile0b_pixels(size_t n);
void profile0b_hook(int kind,void *data,size_t bytes,long long live);
void profile0b_phase(const char *name,bool finish);
void profile0b_sprite(int id,unsigned index,const uint8_t*p,int w,int h,bool single);
void profile0b_frame(const char *phase,const uint8_t*p,int w,int h);
namespace fheroes2 { class Image; }
void profile0b_blit(const fheroes2::Image &in,int ix,int iy,fheroes2::Image &out,int ox,int oy,int w,int h,bool flip,bool copy);
void profile0b_fill(fheroes2::Image &out,int x,int y,int w,int h,uint8_t color);
