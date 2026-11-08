// Codex: проверяем исходное разворачивание слоёв, порядок, обрезку и пик ёмкости.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <vector>
#include "../main/hmm_sequence.inc"
struct Rect {int x=0,y=0,w=0,h=0;};
struct Draw {Rect r;int sx=0,sy=0,step=1,id=0;bool layer=false;std::shared_ptr<int> owner;};
struct Canvas {Sequence<Draw> draws;};
Rect intersect(Rect a,Rect b){int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
bool map_static_allowed,map_plan_valid,map_plan_stable,map_active;size_t map_span_position;
std::vector<int> map_spans;int target;const int *map_target=&target;
Sequence<Draw> map_draws;std::map<const int*,Canvas> canvases;
// Codex: геометрический тест не создаёт частные клетки RAM_G; проверяем вызов их сброса.
unsigned tile_resets;
void changed(Canvas&){}void map_cache_clear(){}void map_record_end(){}void map_tiles_clear(){++tile_resets;}
#include "memory_materialize_impl.inc"
void check(const std::vector<Draw> &input,const std::vector<Draw> &layers,const Rect *expand){
    auto &v=canvases[map_target].draws;v.assign(input.begin(),input.end());map_draws.assign(layers.begin(),layers.end());
    const auto placeholders=std::count_if(input.begin(),input.end(),[](const Draw &d){return d.layer;});
    std::vector<Draw> expected;size_t first=0,count=0,ef=0,ec=0;
    for(const auto &d:input){
        if(!d.layer){expected.push_back(d);continue;}
        const Rect clip=expand&&placeholders==1?*expand:d.r;ef=expected.size();
        for(const auto &m:layers){auto r=intersect(m.r,clip);if(!r.w||!r.h)continue;
            auto e=m;e.sx+=(r.x-m.r.x)*m.step;e.sy+=r.y-m.r.y;e.r=r;expected.push_back(e);
        }ec=expected.size()-ef;
    }
    const unsigned resets=tile_resets;map_materialize(expand,&first,&count);assert(tile_resets==resets+1);
    assert(v.size()==expected.size()&&map_draws.empty());
    if(expand&&placeholders==1)assert(first==ef&&count==ec);else assert(!first&&!count);
    for(size_t i=0;i<expected.size();++i){const auto &a=v[i],&b=expected[i];
        assert(a.id==b.id&&a.owner==b.owner&&a.sx==b.sx&&a.sy==b.sy&&a.step==b.step&&a.layer==b.layer);
        assert(a.r.x==b.r.x&&a.r.y==b.r.y&&a.r.w==b.r.w&&a.r.h==b.r.h);
    }
}
int main(){
    auto owner=std::make_shared<int>(82);std::vector<Draw> many(1400);
    for(size_t i=0;i<many.size();++i)many[i]={{0,0,100,100},0,0,1,int(i),false,owner};
    check({{{0,0,640,480},0,0,1,-1,true,owner}},many,nullptr);
    // Codex: прежняя реализация одновременно держала три массива по 1408 слоёв.
    assert(Sequence<Draw>::high()<=(2*1408+80)*sizeof(Draw));
    std::minstd_rand random(82);
    for(unsigned test=0;test<5000;++test){std::vector<Draw> layers,input;
        for(unsigned i=0,n=random()%180;i<n;++i)layers.push_back({{int(random()%600)-30,int(random()%450)-30,int(random()%90),int(random()%90)},7,9,random()%2?1:-1,int(i),false,owner});
        for(unsigned i=0,n=1+random()%12;i<n;++i)input.push_back({{int(random()%500),int(random()%350),int(random()%180),int(random()%180)},4,8,1,-int(i)-1,random()%3==0,owner});
        Rect expanded{-20,-30,600,450};check(input,layers,random()%2?&expanded:nullptr);
    }
    std::puts("materialize: 5000 clipped/multiple/expanded cases, ownership and two-list peak OK");
}
