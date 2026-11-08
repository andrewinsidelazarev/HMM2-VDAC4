/* Codex: быстрый независимый повтор RGB для длинных попиксельных A/B.
 * Семантика check_fix5_ft.py и ft_rd_pix.sv: каждое произведение делится на 255
 * отдельно. Этот файл используется только ПК-проверкой и не входит в ELF. */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace {
int signed_bits(uint32_t v,int bits){return int(v^(1u<<(bits-1)))-int(1u<<(bits-1));}
int div_floor(int v,int d){return v>=0?v/d:-((-v+d-1)/d);}
int factor(int kind,int sa,int da){switch(kind){case 0:return 0;case 1:return 255;case 2:return sa;case 3:return da;case 4:return 255-sa;case 5:return 255-da;default:return -10000;}}
struct Handle {uint32_t source,stride,bh,bw,oh,format,wrapx,wrapy;};
}
extern "C" __declspec(dllexport) int ft_rgb(const uint32_t *words,unsigned count,const uint8_t *ram,Handle *handles,uint8_t *rgb){
    const bool small=std::find(words,words+count,0x19000100u)!=words+count;
    const int width=small?640:1024,height=small?480:768;
    std::vector<uint8_t> alpha(width*height,0);std::memset(rgb,0,width*height*3);
    int mask=15,bs=2,bd=4,ca=255,clearA=0,color[3]={255,255,255},clear[3]={0,0,0};
    int handle=0,cell=0,palette=0,a=256,e=256,tx=0,ty=0,vx=0,vy=0,fmt=4,primitive=1;
    int sx0=0,sy0=0,sw=width,sh=height,cx=0,cy=0;bool corner=false,gray=false;
    for(unsigned k=0;k<count;++k)gray|=(words[k]>>24)==0x20&&(words[k]&15)!=15;
    auto paint=[&](int x,int y,int r,int g,int b,int sa){
        const int p=y*width+x,da=alpha[p],fs=factor(bs,sa,da),fd=factor(bd,sa,da);const int src[3]={r,g,b};
        for(int c=0;c<3;++c)if(mask&(8>>c))rgb[p*3+c]=uint8_t(std::min(255,src[c]*fs/255+int(rgb[p*3+c])*fd/255));
        if(mask&1)alpha[p]=uint8_t(std::min(255,sa*fs/255+da*fd/255));
    };
    unsigned calls[4],depth=0,steps=0;
    for(unsigned k=0;k<count;++k){if(++steps>1000000)return -35;const uint32_t v=words[k],op=v>>24;Handle *d=handles+handle;
        if(op==0x1d){if(depth==4)return -36;calls[depth++]=k+1;k=(v&2047)-1;continue;}
        if(op==0x1e){k=(v&2047)-1;continue;}
        if(op==0x24){if(!depth)break;k=calls[--depth]-1;continue;}
        if(op==0x20)mask=v&15;
        else if(op==0xf)clearA=v&255;
        else if(op==0x10)ca=v&255;
        else if(op==2){clear[0]=v>>16&255;clear[1]=v>>8&255;clear[2]=v&255;}
        else if(op==0x26){if(v&4)for(int p=0;p<width*height;++p){for(int c=0;c<3;++c)if(mask&(8>>c))rgb[p*3+c]=uint8_t(clear[c]);if(mask&1)alpha[p]=uint8_t(clearA);}}
        else if(op==5)handle=v&31;
        else if(op==6)cell=v&127;
        else if(op==1)d->source=v&0x3fffff;
        else if(op==0x2a)palette=v&0x3fffff;
        else if(op==7){d->format=v>>19&31;if(d->format!=1&&d->format!=7&&d->format!=16)return -7;d->stride=(d->stride&3072)|(v>>9&1023);d->bh=(d->bh&1536)|(v&511);}
        else if(op==0x28){d->stride=(d->stride&1023)|(v>>2&3)<<10;d->bh=(d->bh&511)|(v&3)<<9;}
        else if(op==8){if(v&0x100000)return -8;d->wrapx=!!(v&0x80000);d->wrapy=!!(v&0x40000);d->bw=(d->bw&1536)|(v>>9&511);d->oh=(d->oh&1536)|(v&511);}
        else if(op==0x29){d->bw=(d->bw&511)|(v>>2&3)<<9;d->oh=(d->oh&511)|(v&3)<<9;}
        else if(op==0x15)a=signed_bits(v&0x1ffff,17);
        else if(op==0x19)e=signed_bits(v&0x1ffff,17);
        else if(op==0x17)tx=signed_bits(v&0xffffff,24);
        else if(op==0x1a)ty=signed_bits(v&0xffffff,24);
        else if(op==0x27)fmt=v&7;
        else if(op==0x2b)vx=div_floor(signed_bits(v&0x1ffff,17),16);
        else if(op==0x2c)vy=div_floor(signed_bits(v&0x1ffff,17),16);
        else if(op==0x1b){sx0=v>>11&2047;sy0=v&2047;}
        else if(op==0x1c){sw=v>>12&4095;sh=v&4095;}
        else if(op==0x1f){primitive=v&15;corner=false;}
        else if(op==4){color[0]=v>>16&255;color[1]=v>>8&255;color[2]=v&255;}
        else if(op==0xb){bs=v>>3&7;bd=v&7;if(bs>5||bd>5)return -11;}
        else if((v>>30)==1||(v>>30)==2){
            int x,y,fx,fy,dc;
            if((v>>30)==2){x=int(v>>21&511)+vx;y=int(v>>12&511)+vy;d=handles+(v>>7&31);dc=v&127;fx=x*16;fy=y*16;}
            else{fx=div_floor(signed_bits(v>>15&32767,15)*16,1<<fmt)+vx*16;fy=div_floor(signed_bits(v&32767,15)*16,1<<fmt)+vy*16;x=div_floor(fx+15,16);y=div_floor(fy+15,16);dc=cell;}
            if(primitive==9){
                if(!corner){cx=x;cy=y;corner=true;}
                else{for(int yy=std::max({0,cy,sy0});yy<std::min({height,y,sy0+sh});++yy)for(int xx=std::max({0,cx,sx0});xx<std::min({width,x,sx0+sw});++xx)paint(xx,yy,color[0],color[1],color[2],ca);corner=false;}
                continue;
            }
            if(primitive!=1)return -31;
            const int right=std::min({x+int(d->bw),width,sx0+sw}),bottom=std::min({y+int(d->oh),height,sy0+sh});x=std::max({x,0,sx0});y=std::max({y,0,sy0});
            if(right<=x||bottom<=y)continue;
            const int bytes=d->format==7?2:1,pw=d->format==1?d->stride*8:d->stride/bytes,bh=d->bh;if(!pw||!bh)return -32;
            const uint32_t address=d->source+d->stride*d->bh*dc;
            for(int yy=y;yy<bottom;++yy){int sy=div_floor(div_floor((yy*16-fy)*e,16)+ty,256);if(d->wrapy)sy=(sy%bh+bh)%bh;
                for(int xx=x;xx<right;++xx){int sx=div_floor(div_floor((xx*16-fx)*a,16)+tx,256);if(d->wrapx)sx=(sx%pw+pw)%pw;
                    const bool border=sx<0||sx>=pw||sy<0||sy>=bh;const int ix=std::clamp(sx,0,pw-1);const uint32_t p=address+std::clamp(sy,0,bh-1)*d->stride+(d->format==1?ix/8:ix*bytes);
                    if(p+bytes>0x300000)return -33;
                    int r,g,b,sa;
                    // Codex: L1 в Tang задаёт белый тексель с альфой 0/255;
                    // цвет и исходную альфу тени применяет контекст FT812.
                    if(d->format==1){r=g=b=255;sa=!border&&(ram[p]&(128u>>(ix&7)))?ca:0;}
                    else if(bytes==2){const unsigned c=ram[p]|(unsigned(ram[p+1])<<8);r=(c>>11)*255/31;g=(c>>5&63)*255/63;b=(c&31)*255/31;sa=border?0:ca;}
                    else {const unsigned pp=palette+unsigned(ram[p])*4;if(pp+4>0x300000)return -34;
                        r=border?0:ram[pp+(gray?0:2)];g=border?0:ram[pp+(gray?0:1)];b=border?0:ram[pp];sa=border?0:ram[pp+(gray?0:3)]*ca/255;}
                    paint(xx,yy,r*color[0]/255,g*color[1]/255,b*color[2]/255,sa);
                }
            }
        }
    }
    return width*height*3;
}
