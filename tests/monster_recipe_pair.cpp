// Actual cache implementation: old serial calls vs paired lookup, plus trim races.
#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
namespace fheroes2 {
struct ObjectRenderingInfo {std::array<uint32_t,16> data{};};
namespace AGG {struct Image {int x()const{return 0;}int y()const{return 0;}};const Image &GetICN(int,unsigned){static Image image;return image;}}
}
namespace Maps {using HmmMonsterParts=std::shared_ptr<const std::vector<fheroes2::ObjectRenderingInfo>>;}
namespace ICN {enum {MINI_MONSTER_IMAGE=7,MINI_MONSTER_SHADOW=9};}
struct Point {int x,y;} monsterImageOffset{3,4};
void hmmAppendSquares(std::vector<fheroes2::ObjectRenderingInfo>&v,Point,const fheroes2::AGG::Image&,int icn,uint32_t frame,bool){
 for(unsigned i=0;i<frame%9+1;++i){fheroes2::ObjectRenderingInfo p;for(unsigned j=0;j<p.data.size();++j)p.data[j]=unsigned(icn)*100003+frame*37+i*5+j;v.push_back(p);}
}
#include "hmm_monster_parts.inc"
void clear(bool reset=false){HmmMonsterRecipeLock lock;for(auto &p:hmm_monster_recipes)p=HmmMonsterRecipe{};hmm_monster_recipe_heads.fill(0);hmm_monster_recipe_bytes=0;if(reset)hmm_monster_recipe_clock=hmm_monster_recipe_hits=hmm_monster_recipe_builds=0;}
auto serial(std::pair<uint32_t,uint32_t> f){auto a=hmmMonsterRecipe(ICN::MINI_MONSTER_IMAGE,f);auto z=hmmMonsterRecipe(ICN::MINI_MONSTER_SHADOW,f);return std::make_pair(std::move(a),std::move(z));}
uint64_t hash(const Maps::HmmMonsterParts&p){uint64_t h=0;for(auto &r:*p)for(auto v:r.data)h=(h^v)*1099511628211ULL;return h;}
struct Result {std::vector<uint64_t> hashes;unsigned hits,built,clock,bytes;};
Result run(bool paired){
 clear(true);Result result;std::vector<Maps::HmmMonsterParts> leased;
 for(unsigned i=0;i<20000;++i){const unsigned first=(i%5?i/7:i*13)%137,second=i%3?(first+3)%137:0;
  auto p=paired?hmmMonsterRecipePair({first,second}):serial({first,second});result.hashes.push_back(hash(p.first));result.hashes.push_back(hash(p.second));
  if(i%71==0){leased.push_back(p.first);leased.push_back(p.second);}if(i%199==0){std::vector<uint64_t> before;for(auto &v:leased)before.push_back(hash(v));clear();for(unsigned n=0;n<leased.size();++n)if(hash(leased[n])!=before[n])throw std::runtime_error("trim damaged lease");leased.clear();}
 }
 result.hits=hmm_monster_recipe_hits;result.built=hmm_monster_recipe_builds;result.clock=hmm_monster_recipe_clock;result.bytes=hmm_monster_recipe_bytes;return result;
}
int main(){
 auto a=run(false),z=run(true);if(a.hashes!=z.hashes||a.hits!=z.hits||a.built!=z.built||a.clock!=z.clock||a.bytes!=z.bytes)throw std::runtime_error("paired lookup changed data or LRU semantics");
 clear(true);std::atomic<bool> failed{false};auto reader=[&](unsigned salt){try{for(unsigned i=0;i<10000;++i){auto p=hmmMonsterRecipePair({(i+salt)%137,(i*7)%53});const auto x=hash(p.first),y=hash(p.second);std::this_thread::yield();if(x!=hash(p.first)||y!=hash(p.second))throw std::runtime_error("concurrent lease");}}catch(...){failed=true;}};
 std::thread one(reader,5),two(reader,17),trim([]{for(unsigned i=0;i<5000;++i){clear();std::this_thread::yield();}});one.join();two.join();trim.join();if(failed)throw std::runtime_error("concurrent cache/trim");
 std::printf("RECIPE PAIR OK 20000 exact serial/paired calls; data/LRU/hits/builds/bytes equal; 20000 concurrent leases and 5000 trims; serialhits=%u builds=%u\n",a.hits,a.built);
}
