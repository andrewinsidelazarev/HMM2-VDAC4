// Codex: исходные callbacks OGG сохраняют блоки между песнями и отдают их на границе мира.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
std::unordered_map<void*,size_t> live;
unsigned allocated,freed;bool fail;
void *audio_raw_alloc(size_t n,uint32_t){if(fail)return nullptr;void *p=std::malloc(n);assert(p);live[p]=n;++allocated;return p;}
void audio_raw_free(void *p){assert(live.erase(p)==1);std::free(p);++freed;}
#include "memory_music_impl.inc"
int main(){
    auto *input=audio_alloc(64*1024,1),*arena=audio_alloc(256*1024,1);
    assert(input&&arena&&allocated==2);
    audio_memory_plan(true);
    for(unsigned i=0;i<2000;++i){
        std::memset(input,0x31,64*1024);std::memset(arena,0x72,256*1024);
        audio_free(input);audio_free(arena);assert(!freed);
        assert(audio_alloc(64*1024,1)==input&&audio_alloc(256*1024,1)==arena);
        assert(static_cast<unsigned char*>(input)[60000]==0x31&&static_cast<unsigned char*>(arena)[250000]==0x72);
    }
    assert(allocated==2);
    auto *second=audio_alloc(256*1024,1);assert(second&&second!=arena);audio_free(second);assert(freed==1);
    audio_free(input);audio_memory_plan(false);assert(freed==2&&live.count(arena));
    audio_free(arena);assert(live.empty()&&freed==allocated);
    fail=true;audio_memory_plan(true);assert(!audio_alloc(256*1024,1));
    fail=false;arena=audio_alloc(256*1024,1);assert(arena);audio_free(arena);audio_memory_plan(false);
    assert(live.empty()&&allocated==freed);
    std::puts("Music: 2000 track changes, two persistent blocks, busy ownership, world reset and allocation failure OK");
}
