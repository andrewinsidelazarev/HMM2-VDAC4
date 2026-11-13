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
struct ObjectRenderingInfo {struct Area {int width=1,height=1;} area;Point imageOffset;uint32_t icnIndex=0;uint8_t alphaValue=255;};
}
template<class T> struct Sequence:std::vector<T>{static void release_unused(){}};
constexpr int DIRECTION_ALL=255;
struct World {int w()const{return 145;}int h()const{return 145;}struct Tile {int getFogDirection()const{return 0;}};Tile getTile(int,int)const{return {};}} world;
static bool check_skipped=false;
int hmm_gpu_ring_need(int x,int){return !check_skipped || x%3==0;}
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
static void skipped(){
    check_skipped=true;
    struct Ref{uint32_t id;uint8_t alpha;bool hero,skip;};
    HmmTileImages queue;std::map<fheroes2::Point,std::deque<Ref>> reference;
    for(unsigned i=0;i<8192;++i){
        fheroes2::Point p{long(int(next()%33)-16),long(int(next()%33)-16)};
        fheroes2::ObjectRenderingInfo info;info.icnIndex=i;info.alphaValue=i%3==0?0:i%3==1?37:255;
        hmm_hero_records=(i&4)!=0;Ref r{i,info.alphaValue,hmm_hero_records,p.x%3!=0};
        if(next()&1){queue[p].emplace_front(info);reference[p].push_front(r);}else{queue[p].emplace_back(info);reference[p].push_back(r);}
    }
    auto ordered=queue.ordered();auto at=ordered.begin();unsigned skips=0,callbacks=0;
    for(const auto &cell:reference)for(const auto &r:cell.second){
        if(!(at!=ordered.end()))throw std::runtime_error("missing record");const auto v=*at;++at;
        if(v.offset.x!=cell.first.x||v.offset.y!=cell.first.y||v.hero!=r.hero||v.skip!=r.skip)throw std::runtime_error("skip order/flags");
        if(r.skip){++skips;callbacks+=v.info.alphaValue!=0;if(bool(v.info.alphaValue)!=bool(r.alpha))throw std::runtime_error("skip alpha predicate");}
        else if(v.info.icnIndex!=r.id||v.info.alphaValue!=r.alpha)throw std::runtime_error("visible payload");
    }
    if(at!=ordered.end()||skips<1000||callbacks<1000)throw std::runtime_error("skip coverage");
    std::printf("SKIP QUEUE OK8192 records; metadata=%zu bytes skips=%u callbacks=%u\n",sizeof(HmmTileImages::Piece),skips,callbacks);
    check_skipped=false;hmm_hero_records=false;
}
static void alpha_overrides(){
    for(unsigned mode=0;mode<2;++mode){
        check_skipped=mode!=0;HmmTileImages queue;
        struct Ref {uint32_t id;uint8_t alpha;bool hero,skip;};std::map<fheroes2::Point,std::deque<Ref>> expected;
        for(unsigned i=0;i<4096;++i){const fheroes2::Point p{long(int(next()%33)-16),long(int(next()%33)-16)};
            fheroes2::ObjectRenderingInfo info;info.icnIndex=i;info.alphaValue=uint8_t(next());const auto original=info.alphaValue;
            const uint8_t alpha=i%3==0?0:i%3==1?37:255;hmm_hero_records=(i&8)!=0;
            Ref r{i,alpha,hmm_hero_records,check_skipped&&p.x%3!=0};
            if(next()&1){queue[p].emplace_front_alpha(info,alpha);expected[p].push_front(r);}
            else{queue[p].emplace_back_alpha(info,alpha);expected[p].push_back(r);}
            if(info.alphaValue!=original)throw std::runtime_error("shared recipe modified");
        }
        const auto sorted=queue.ordered();auto at=sorted.begin();
        for(const auto &cell:expected)for(const auto &r:cell.second){if(!(at!=sorted.end()))throw std::runtime_error("override missing entry");const auto actual=*at;++at;
            if(actual.offset.x!=cell.first.x||actual.offset.y!=cell.first.y||actual.hero!=r.hero||actual.skip!=r.skip)throw std::runtime_error("override order/flags");
            if(r.skip?bool(actual.info.alphaValue)!=bool(r.alpha):actual.info.alphaValue!=r.alpha||actual.info.icnIndex!=r.id)throw std::runtime_error("override alpha/payload");
        }
        if(at!=sorted.end())throw std::runtime_error("override extra entry");
    }
    check_skipped=hmm_hero_records=false;puts("ALPHA OVERRIDE OK8192 records, visible/skip, front/back, source immutable");
}
int main(){
    _putenv_s("HMM2_QUEUE_SORT_VERIFY","1");
    for(unsigned n:{0u,1u,2u,31u,32u,4095u,4096u,4097u,32768u,32769u}){check(n,17);check(n,1001);check(n,7,true);}
    for(unsigned i=0;i<256;++i)check(next()%4097,(i&1)?245:17);
    skipped();alpha_overrides();
    puts("QUEUE SORT OK 286 map/deque cases; fronts/backs, duplicates, signed coordinates, extrema, 4096/32768 boundaries");
}
