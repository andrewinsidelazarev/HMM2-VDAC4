// Codex: слияние без второго массива сравнивается с прежней функцией на тех же слоях.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <new>
#include <random>
#include <vector>
#include "../main/hmm_sequence.inc"
struct Rect {int x=0,y=0,w=0,h=0;};
struct Bitmap {bool frame=false,single=true;int w=128,h=128;};
struct Draw {Rect r;int sx=0,sy=0,step=1;unsigned resource=0,mapping=0,color=0,mask=0;bool layer=false,raw=false,fill=false,repeat=false,opaque=false;std::shared_ptr<Bitmap> bitmap;};
Rect intersect(Rect a,Rect b){int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
#include "memory_join_impl.inc"
#include "memory_join_reference.inc"
static void check(const std::vector<Draw> &input){
    Sequence<Draw> expected(input.begin(),input.end()),actual(input.begin(),input.end());
    join_layers_reference(expected);
    const auto held=Sequence<Draw>::bytes();
    join_layers(actual);
    assert(Sequence<Draw>::bytes()==held);
    assert(actual.size()==expected.size());
    for(size_t i=0;i<actual.size();++i){const auto &a=actual[i],&b=expected[i];
        assert(a.r.x==b.r.x&&a.r.y==b.r.y&&a.r.w==b.r.w&&a.r.h==b.r.h&&a.sx==b.sx&&a.sy==b.sy);
        assert(a.step==b.step&&a.resource==b.resource&&a.mapping==b.mapping&&a.color==b.color&&a.mask==b.mask);
        assert(a.layer==b.layer&&a.raw==b.raw&&a.fill==b.fill&&a.repeat==b.repeat&&a.opaque==b.opaque&&a.bitmap==b.bitmap);
    }
}
int main(){
    auto bitmap=std::make_shared<Bitmap>();std::vector<Draw> input;
    Draw d;d.r={0,0,128,128};d.bitmap=bitmap;d.opaque=true;
    input.push_back(d);
    for(unsigned i=0;i<1400;++i){d.r={int(i%8)*16,int(i%7)*16,16,16};d.sx=d.r.x;d.sy=d.r.y;input.push_back(d);}
    check(input);
    std::minstd_rand random(41);std::array<Draw,24> prototypes{};
    for(auto &p:prototypes){p.bitmap=random()%2?bitmap:std::make_shared<Bitmap>();p.resource=random()%5;p.mapping=random()%3;
        p.layer=random()%9==0;p.raw=random()%2;p.fill=random()%5==0;p.repeat=random()%7==0;p.opaque=random()%2;
        p.color=random()%5;p.mask=random()%4;p.step=random()%3?1:-1;}
    for(unsigned trial=0;trial<5000;++trial){input.clear();
        for(unsigned i=0,n=random()%800;i<n;++i){
            Draw p=prototypes[random()%prototypes.size()];
            p.r={int(random()%12)*16,int(random()%10)*16,int(1+random()%3)*16,int(1+random()%3)*16};
            if(random()%5==0)p.r={0,0,128,128};p.sx=p.r.x*p.step;p.sy=p.r.y;
            input.push_back(p);
        }check(input);
    }
    input.clear();for(auto &p:prototypes)p.bitmap.reset();Sequence<Draw>::release_unused();
    assert(bitmap.use_count()==2); // Codex: остаются только bitmap и последний образец d.
    std::puts("join: 5000 overlapping/adjacent/duplicate cases exact, no new pages, ownership OK");
}
