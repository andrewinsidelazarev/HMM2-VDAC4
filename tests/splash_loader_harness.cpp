// Codex: окружение захвата для дословно извлечённых loader_begin/loader_tick из hmm_gpu.cpp.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "ft_sprite.h"
}
constexpr int ZM_OK=0;
uint32_t dl_address,now;
std::vector<uint8_t> ram(2*1024*1024);
std::vector<uint32_t> captured;
unsigned written_lists;
bool cursor_live(){return true;}
struct TestCore {
    uint32_t millis(){return now;}
    int ft_read(uint32_t address,uint32_t *value){*value=0;if(address<ram.size())std::memcpy(value,ram.data()+address,4);return ZM_OK;}
    int ft_write(uint32_t address,const uint32_t *data,unsigned count){
        if(address+count*4>ram.size())std::abort();
        std::memcpy(ram.data()+address,data,count*4);
        if(address==dl_address){captured.assign(data,data+count);++written_lists;}
        else if(address!=0x7fff0&&address!=0xc0000&&address!=0x302054)std::abort();
        return ZM_OK;
    }
} core;
// Codex: запись DLSWAP — регистр, не RAM_G; перехват вынесен в обёртку, видимую реальной функции.
struct CoreAdapter {
    uint32_t millis(){return core.millis();}
    int ft_read(uint32_t a,uint32_t *v){return core.ft_read(a,v);}
    int ft_write(uint32_t a,const uint32_t *p,unsigned n){return a==0x302054?ZM_OK:core.ft_write(a,p,n);}
} adapter;
CoreAdapter *hmm_core=&adapter;
bool task_append(unsigned count){return count==captured.size();}

// Codex: файл создаёт проверка из текущего исходника без изменения тела функций.
#include "loader_actual.inc"

int main(int argc,char **argv){
    if(argc!=3)return 2;
    static hft_cache cache;hft_cache_init(&cache);int upload;
    if(!hft_cache_get(&cache,UINT64_C(0x4000000000000000),8192,&dl_address,&upload)||dl_address!=0)return 3;
    for(unsigned target:{8u,20u,381u,762u}){
        unsigned before=target;
        if(target!=8){for(before=0;before<=LOADER_FILL;++before)if(before+(LOADER_FILL-before+63)/64==target)break;if(before>LOADER_FILL)return 4;}
        std::fill(ram.begin(),ram.end(),0xa5);written_lists=0;now=0;
        uint32_t mark[]={0x504c4456u,before,0x12345678u,0};std::memcpy(ram.data()+LOADER_MARK,mark,sizeof(mark));
        FILE *f=std::fopen(argv[1],"rb");if(!f)return 5;
        captured.clear();uint32_t word;while(std::fread(&word,4,1,f))captured.push_back(word);std::fclose(f);
        if(!loader_begin())return 6;
        now=target==8?0:100;
        if(!loader_tick()||loader_shown!=target||written_lists!=(target==8?0u:1u))return 7;
        for(unsigned i=LOADER_PICTURE;i<ram.size();++i){if(i>=LOADER_CHIPID&&i<LOADER_CHIPID+4)continue;if(ram[i]!=0xa5)return 8;}
        if(std::memcmp(ram.data()+LOADER_CHIPID,&mark[2],4))return 9;
        char path[1024];std::snprintf(path,sizeof(path),"%s/elf_%u.dl",argv[2],target);
        f=std::fopen(path,"wb");if(!f)return 10;std::fwrite(captured.data(),4,captured.size(),f);std::fclose(f);
        std::printf("fill=%u before=%u words=%zu inherited=%u dl_address=%u picture_preserved=1\n",target,before,captured.size(),target==8,dl_address);
    }
    return 0;
}
