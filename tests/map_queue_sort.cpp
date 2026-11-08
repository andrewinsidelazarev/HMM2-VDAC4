// Exercise the actual queue class against the original map<Point,deque> contract.
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <map>
#include <deque>
#include <limits>
namespace fheroes2 {
// Xtensa's int32_t is long; keep that distinct type in the Windows fixture too.
struct Point {long x=0,y=0;bool operator<(const Point &p)const{return x==p.x?y<p.y:x<p.x;}};
struct ObjectRenderingInfo {struct Area {int width=1,height=1;} area;Point imageOffset;uint32_t icnIndex=0;};
}
template<class T> struct Sequence:std::vector<T>{static void release_unused(){}};
constexpr int DIRECTION_ALL=255;
struct World {int w()const{return 145;}int h()const{return 145;}struct Tile {int getFogDirection()const{return 0;}};Tile getTile(int,int)const{return {};}} world;
int hmm_gpu_ring_need(int,int){return 1;}
#include "hmm_map_records.inc"
static uint32_t rng=0x41672a91;
static uint32_t next(){rng=rng*1664525+1013904223;return rng;}
static void check(unsigned count,unsigned range,bool extremes=false){
    HmmTileImages queue;std::map<fheroes2::Point,std::deque<uint32_t>> reference;
    for(unsigned i=0;i<count;++i){int x=int(next()%range)-int(range/2),y=int(next()%range)-int(range/2);
        if(extremes){x=(next()&1)?std::numeric_limits<int>::min():std::numeric_limits<int>::max();y=int(next()%4);}
        fheroes2::Point p{x,y};fheroes2::ObjectRenderingInfo info;info.icnIndex=i;
        if(next()&1){queue[p].emplace_front(info);reference[p].push_front(i);}else{queue[p].emplace_back(info);reference[p].push_back(i);}
    }
    auto ordered=queue.ordered();auto p=ordered.begin();const auto end=ordered.end();
    for(const auto &cell:reference)for(auto id:cell.second){
        if(!(p!=end)||(*p).offset.x!=cell.first.x||(*p).offset.y!=cell.first.y||(*p).info.icnIndex!=id)throw std::runtime_error("map/deque contract mismatch");++p;
    }
    if(p!=end)throw std::runtime_error("extra queue entry");
}
int main(){
    _putenv_s("HMM2_QUEUE_SORT_VERIFY","1");
    for(unsigned n:{0u,1u,2u,31u,32u,4095u,4096u,4097u,32768u,32769u}){check(n,17);check(n,1001);check(n,7,true);}
    for(unsigned i=0;i<256;++i)check(next()%4097,(i&1)?245:17);
    puts("QUEUE SORT OK 286 map/deque cases; fronts/backs, duplicates, signed coordinates, extrema, 4096/32768 boundaries");
}
