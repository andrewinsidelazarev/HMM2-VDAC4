// Codex: настоящий loop_alloc/loop_release извлекаются из hmm_audio.cpp сборщиком теста.
// Проверяем содержимое активных петель после освобождения дыр и уплотнения области.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
constexpr unsigned channels_max=32;
unsigned hmm_loop_bytes;
#include "memory_loop_impl.inc"
static unsigned char value(unsigned id,unsigned at){return static_cast<unsigned char>((id*31+at*13)^(at>>8));}
int main(){
    std::vector<uint8_t> memory(240*1024);
    loop_region=memory.data();loop_capacity=unsigned(memory.size());
    unsigned bytes,generation;assert(hmm_audio_idle_region(&bytes,&generation)==memory.data()&&bytes==memory.size());
    // Codex: тесный профиль передаёт ненулевой адрес zlib, но нулевую ёмкость петель.
    loop_capacity=0;assert(!hmm_audio_idle_region(&bytes,&generation)&&bytes==0);loop_capacity=unsigned(memory.size());
    const unsigned before=generation;effects[0].whole=loop_alloc(1,1024);
    assert(!hmm_audio_idle_region(&bytes,&generation)&&generation!=before);
    const unsigned active=generation;effects[1].whole=loop_alloc(1,1024);
    assert(!hmm_audio_idle_region(&bytes,&generation)&&generation==active);
    loop_release(effects[0]);loop_release(effects[1]);
    assert(hmm_audio_idle_region(&bytes,&generation)==memory.data());
    std::memset(memory.data(),0x71,memory.size());effects[0].whole=loop_alloc(2,1024);
    assert(!hmm_audio_idle_region(&bytes,&generation)&&generation!=active);loop_release(effects[0]);
    std::minstd_rand random(82);
    for(unsigned step=0;step<20000;++step){
        auto &e=effects[random()%channels_max];
        if(e.whole)loop_release(e);
        const unsigned id=1+random()%40,n=1024+id*317;
        e.whole=loop_alloc(id,n);
        if(e.whole){auto &b=*e.whole;if(!b.filled){b.filled=n-(id%23);for(unsigned j=0;j<b.filled;++j)b.data[j]=value(id,j);}}
        unsigned total=0;
        for(auto &b:loop_buffers)if(b.users){
            total+=b.size;assert(b.data>=memory.data()&&b.data+b.size<=memory.data()+memory.size());
            unsigned references=0;for(auto &channel:effects)references+=channel.whole==&b;
            assert(references==b.users);
            for(unsigned j=0;j<b.filled;++j)assert(b.data[j]==value(b.offset,j));
            for(auto &c:loop_buffers)if(&b!=&c&&c.users)assert(b.data+b.size<=c.data||c.data+c.size<=b.data);
        }
        assert(total==loop_cache_bytes&&total==hmm_loop_bytes);
        assert(!loop_alloc(999,128*1024+1));
    }
    for(auto &e:effects)loop_release(e);
    assert(!loop_cache_bytes&&!hmm_loop_bytes);
    auto *a=loop_alloc(1,128*1024),*b=loop_alloc(2,loop_capacity-128*1024);
    assert(a&&b&&!loop_alloc(3,1));
    assert(loop_alloc(1,128*1024)==a&&a->users==2);
    std::puts("audio pool: 20000 mixed starts/stops, shared ownership, bytes and capacity OK");
}
