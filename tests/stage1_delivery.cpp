/* Codex: настоящий zm_cache.cpp с файловым хранилищем в памяти; прошивка только читается. */
#include "zifi/zm_cache.hpp"
#include "zifi/elf_image.hpp"
#include "zifi/path_table.hpp"
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
using zifi::ZmCache;
struct Storage: zifi::ZmStorage {
    std::map<std::string,std::vector<uint8_t>> files;
    bool fail=false;
    size_t capacityBytes() override {return 0x1f0000-320*1024;}
    bool read(const char*n,uint8_t*p,size_t z) override {auto i=files.find(n);if(i==files.end()||i->second.size()!=z)return false;memcpy(p,i->second.data(),z);return true;}
    bool write(const char*n,const uint8_t*p,size_t z) override {if(fail)return false;files[n]={p,p+z};return true;}
    bool remove(const char*n) override {return files.erase(n)!=0;}
};
// Codex: ветви MOD_QUERY из module_app.cpp; выделение буфера доставки проверяет QEMU.
int query(ZmCache&c,const char*n,unsigned z,unsigned stamp,const char*busy=nullptr){
    if(!z||!ZmCache::validName(n))return 3;
    if(c.has(n,z,stamp))return 0;
    if(z>2097152||!c.canFit(z,busy))return 2;
    return 1;
}
std::vector<uint8_t> read(const char*n){FILE*f=fopen(n,"rb");assert(f);fseek(f,0,SEEK_END);long nbytes=ftell(f);rewind(f);std::vector<uint8_t>b(nbytes);assert(fread(b.data(),1,b.size(),f)==b.size());fclose(f);return b;}
int main(int argc,char**argv){
    assert(argc==6);auto elf=read(argv[1]);Storage s;ZmCache c(s);c.load();
    assert(zifi::elfImageProblem(elf.data(),elf.size())==nullptr);
    char diagnostic[13];assert(zifi::PathTable::normalizeName("HMM2-E0702",10,diagnostic));
    const unsigned n=(unsigned)elf.size(),stamp=123;
    assert(query(c,"HMM2.ELF",1926956,stamp)==2);
    assert(query(c,"HMM2.ELF",0,stamp)==3);
    assert(query(c,"TOO_LONG_NAME.ELF",n,stamp)==3);
    assert(query(c,"HMM2.ELF",n,stamp)==1);
    assert(query(c,"BOUND.ELF",1691648,stamp)==1);
    assert(query(c,"BOUND.ELF",1691649,stamp)==2);
    const char*names[]={"PLAYER.ELF","MP3.ELF","MIDI.ELF","OGG.ELF"};
    for(int i=0;i<4;i++){auto b=read(argv[i+2]);assert(c.store(names[i],1,b.data(),(unsigned)b.size(),nullptr));}
    const size_t before=c.freeBytes();
    const int active=query(c,"HMM2.ELF",n,stamp,"PLAYER.ELF");
    assert(active==(c.canFit(n,"PLAYER.ELF")?1:2));
    assert(c.count()==4);
    // Codex: SPG сначала APP_STOP; после него PLAYER разрешено вытеснять.
    assert(query(c,"HMM2.ELF",n,stamp)==1);
    assert(c.store("HMM2.ELF",stamp,elf.data(),n,nullptr));
    assert(!c.find("PLAYER.ELF"));
    const size_t evicted=5-c.count(),occupied_free=c.freeBytes();
    assert(query(c,"HMM2.ELF",n,stamp)==0);
    ZmCache reboot(s);reboot.load();std::vector<uint8_t>copy(n);assert(reboot.read("HMM2.ELF",copy.data(),n)&&copy==elf);
    for(int i=1;i<4;i++){auto b=read(argv[i+2]);assert(query(reboot,names[i],(unsigned)b.size(),2,"HMM2.ELF")==1);}
    Storage broken;ZmCache bad(broken);bad.load();broken.fail=true;
    assert(!bad.store("HMM2.ELF",stamp,elf.data(),n,nullptr)&&!bad.find("HMM2.ELF"));
    Storage alone;ZmCache game(alone);game.load();assert(game.store("HMM2.ELF",stamp,elf.data(),n,nullptr));
    const size_t free=game.freeBytes();
    for(int i=1;i<4;i++){
        auto b=read(argv[i+2]);assert(game.store(names[i],2,b.data(),(unsigned)b.size(),"HMM2.ELF"));
        assert(game.has("HMM2.ELF",n,stamp));
    }
    printf("{\"capacity\":%zu,\"index_footprint\":8192,\"elf_bytes\":%u,\"elf_footprint\":%zu,\"free_after\":%zu,\"free_with_retained_decoders\":%zu,\"free_before_eviction\":%zu,\"old_rejected\":true,\"active_player_query\":%d,\"inactive_modules_evicted\":%zu,\"reload_exact\":true,\"write_failure_checked\":true,\"decoders_stored_with_game_protected\":true}\n",s.capacityBytes(),n,ZmCache::footprint(n),free,occupied_free,before,active,evicted);
}
