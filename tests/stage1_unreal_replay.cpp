/* Codex: графический поток HMM2 исполняется неизменной моделью VDAC4 Unreal-ng.
 * Это replay FT812; игровой ELF и Z80 здесь не исполняются. */
#include "emulator/platforms/tsconf/vdac4device.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <string>

static uint32_t word(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
int main(int argc,char **argv){try {
    if(argc<4)return 2;
    std::ifstream input(argv[1],std::ios::binary);if(!input)throw std::runtime_error("FT trace open");
    const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
    uint64_t ticks=0;Vdac4Device device([&]{return ticks;},argv[3]);auto *spi=device.GraphicsSpi();
    auto advance=[&](unsigned ms){ticks+=uint64_t(ms)*3500;device.Synchronize(ticks);};
    auto read=[&](uint32_t a){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(device.Peek(a+i))<<(i*8);return value;};
    auto host=[&](uint8_t c,uint8_t p=0){spi->select(true);spi->exchange(c);spi->exchange(p);spi->exchange(0);spi->select(false);};
    auto write=[&](uint32_t a,const uint8_t *p,size_t n){spi->select(true);spi->exchange(uint8_t(0x80|(a>>16)));spi->exchange(uint8_t(a>>8));spi->exchange(uint8_t(a));for(size_t i=0;i<n;++i)spi->exchange(p[i]);spi->select(false);};
    auto put=[&](uint32_t a,uint32_t value){uint8_t p[4];for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(i*8));write(a,p,4);};
    host(0x68);host(0x44);host(0x42);host(0x61,0xc8);host(0);advance(20);
    for(unsigned i=0;i<100&&read(0x302000)!=0x7c;++i)advance(1);
    if(read(0x302000)!=0x7c)throw std::runtime_error("FT812 ID not ready");
    const uint32_t mode[][2]={{0x302070,0},{0x302038,24},{0x30203c,160},{0x302030,320},{0x302034,1024},{0x30202c,1344},{0x30204c,2},{0x302050,8},{0x302044,37},{0x302048,768},{0x302040,806},{0x30206c,0},{0x302068,0},{0x302070,1}};
    for(const auto &r:mode)put(r[0],r[1]);
    std::ofstream hashes(out/"frames.csv");hashes<<"frame,hash,video_frames\n";
    uint32_t frame=0,swaps=0;const unsigned limit=argc>4?unsigned(std::strtoul(argv[4],nullptr,10)):0;
    uint8_t header[8];
    while(input.read(reinterpret_cast<char*>(header),8)){
        uint32_t a=word(header),n=word(header+4);if(n>0x100000)throw std::runtime_error("FT record length");
        std::vector<uint8_t> data(size_t(n)*4);if(!input.read(reinterpret_cast<char*>(data.data()),data.size()))throw std::runtime_error("FT record truncated");
        if(a==0xffffffff){
            if(n!=1)throw std::runtime_error("FT frame marker");frame=word(data.data());advance(20);
            for(unsigned i=0;i<100&&read(0x302054);++i)advance(20);
            if(read(0x302054)||device.CoprocessorFault())throw std::runtime_error("FT swap or coprocessor fault");
            uint64_t hash=UINT64_C(14695981039346656037);for(uint32_t pixel:device.Picture())hash=(hash^pixel)*UINT64_C(1099511628211);
            hashes<<frame<<','<<std::hex<<hash<<std::dec<<','<<device.Frames()<<'\n';
            if(frame==1||frame==59||frame==100||frame==200||frame==370||frame==500)device.SavePicture((out/("frame_"+std::to_string(frame)+".ppm")).string());
            if(frame%50==0){std::printf("UNREAL REPLAY frame=%u swaps=%u fault=%u\n",frame,swaps,device.CoprocessorFault());std::fflush(stdout);}
            if(limit&&frame>=limit)break;
            continue;
        }
        a&=0x7fffffff;write(a,data.data(),data.size());
        if(a==0x3020fc){const uint32_t target=word(data.data());for(unsigned i=0;i<1000&&read(0x3020f8)!=target;++i)advance(1);if(read(0x3020f8)!=target)throw std::runtime_error("CMD_APPEND timeout");}
        if(a==0x302054)++swaps;
    }
    device.SaveTrace((out/"device.log").string());
    if(device.AppRunning())throw std::runtime_error("Replay unexpectedly started a native app");
    std::printf("UNREAL REPLAY complete frames=%u swaps=%u fault=%u; graphics only\n",frame,swaps,device.CoprocessorFault());
    return frame?0:3;
}catch(const std::exception &e){std::fprintf(stderr,"UNREAL REPLAY failure: %s\n",e.what());return 1;}}
