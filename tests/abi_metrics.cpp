// Codex: измеряем контейнеры обеих 32-битных STL, чтобы не переносить кучу MSVC на ESP без поправок.
#include <list>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <cstdio>
#include "maps_tiles.h"
namespace {
size_t allocated=0,calls=0;
template<class T> struct CountAllocator {
    using value_type=T;
    CountAllocator()=default;
    template<class U> CountAllocator(const CountAllocator<U>&){}
    T *allocate(size_t n){allocated+=sizeof(T)*n;++calls;return std::allocator<T>{}.allocate(n);}
    void deallocate(T*p,size_t n){std::allocator<T>{}.deallocate(p,n);}
    template<class U> bool operator==(const CountAllocator<U>&)const{return true;}
    template<class U> bool operator!=(const CountAllocator<U>&)const{return false;}
};
}
extern "C" void abi_metrics(void(*log)(const char*,...)) {
    using List=std::list<Maps::ObjectPart,CountAllocator<Maps::ObjectPart>>;
    log("ABI: pointer=%u tile=%u object=%u list=%u vector=%u string=%u map=%u",
        (unsigned)sizeof(void*),(unsigned)sizeof(Maps::Tile),(unsigned)sizeof(Maps::ObjectPart),
        (unsigned)sizeof(std::list<Maps::ObjectPart>),(unsigned)sizeof(std::vector<int>),
        (unsigned)sizeof(std::string),(unsigned)sizeof(std::map<int,int>));
    allocated=calls=0;
    { List lists[2];
      log("ABI: empty_two_lists_bytes=%u calls=%u",(unsigned)allocated,(unsigned)calls);
      allocated=calls=0;lists[0].resize(3);
      log("ABI: three_nodes_bytes=%u calls=%u",(unsigned)allocated,(unsigned)calls);
    }
}
#ifdef HMM2_ABI_PC
#include <cstdarg>
static void emit(const char*f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
int main(){abi_metrics(emit);}
#endif
