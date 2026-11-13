// Execute the production merge against a flat stable-sort reference.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>
struct Rect {int x=0,y=0,w=0,h=0;};
struct Draw {Rect r;unsigned id=0,mapping=0;bool layer=false,raw=false;unsigned mask=0;};
struct RingDraw:Draw {unsigned order=0;};
struct RingCell {std::vector<RingDraw> source;unsigned tail=0;bool frame_cached=false,valid=false;};
struct HeroSlot {int16_t x,y;uint16_t run;};
template<class T> using Sequence=std::vector<T>;
std::map<std::pair<int,int>,RingCell> cells;
const RingCell *ring_cell(int x,int y){auto p=cells.find({x,y});return p==cells.end()?nullptr:&p->second;}
bool ring_hero(const RingDraw &d){return d.mask==2;}
void hmm_fail(const char *s){throw std::runtime_error(s);}
enum {P_PARTS};struct Prof {explicit Prof(unsigned){}};
void actual(Sequence<Draw> &out,int first_x,int first_y,int cols,int rows,std::vector<HeroSlot> *heroes){
#include "hmm_ring_merge.inc"
}
static uint32_t rng=0x9ab124e3;
uint32_t next(){rng=rng*1664525+1013904223;return rng;}
auto key(const Draw &d){return std::make_tuple(d.id,d.r.x,d.r.y,d.r.w,d.r.h,d.mapping,d.layer,d.raw,d.mask);}
int main(){
    unsigned fragments=0;
    for(unsigned trial=0;trial<12000;++trial){
        cells.clear();const int x0=int(next()%31)-15,y0=int(next()%31)-15;
        const unsigned cols=1+next()%21,rows=1+next()%16;const bool actors=next()&1;
        unsigned id=0;
        for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
            if(next()%4==0)continue;
            auto &c=cells[{x0+int(x),y0+int(y)}];c.valid=next()%7!=0;c.frame_cached=next()&1;
            const unsigned n=next()%17;unsigned order=next()%4;
            for(unsigned j=0;j<n;++j){RingDraw d;d.id=++id;order+=next()%3;d.order=order;d.r={int(next()%32),int(next()%32),1+int(next()%32),1+int(next()%32)};d.mask=next()%3;c.source.push_back(d);}
            c.tail=next()%(n+1);
        }
        std::vector<Draw> got,expected;std::vector<HeroSlot> heroes,wanted;
        actual(got,x0,y0,cols,rows,actors?&heroes:nullptr);
        struct Part {unsigned order;Draw draw;};std::vector<Part> flat;
        for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
            const int wx=x0+int(x),wy=y0+int(y);const auto *c=ring_cell(wx,wy);if(!c||!c->valid)continue;
            unsigned run=0;
            for(unsigned j=c->frame_cached?c->tail:0;j<c->source.size();){
                const auto &d=c->source[j];Draw out;
                if(actors&&d.mask==2){out.r={wx*32,wy*32,32,32};out.layer=out.raw=true;out.mask=255;out.mapping=unsigned(wanted.size());wanted.push_back({int16_t(wx),int16_t(wy),uint16_t(run++)});do{++j;}while(j<c->source.size()&&c->source[j].mask==2);}
                else{out=d;out.r.x+=wx*32;out.r.y+=wy*32;++j;}
                flat.push_back({d.order,out});
            }
        }
        std::stable_sort(flat.begin(),flat.end(),[](const Part&a,const Part&b){return a.order<b.order;});
        if(got.size()!=flat.size()||heroes.size()!=wanted.size())throw std::runtime_error("merge count");
        for(unsigned i=0;i<got.size();++i)if(key(got[i])!=key(flat[i].draw))throw std::runtime_error("merge order/geometry");
        for(unsigned i=0;i<heroes.size();++i)if(std::tie(heroes[i].x,heroes[i].y,heroes[i].run)!=std::tie(wanted[i].x,wanted[i].y,wanted[i].run))throw std::runtime_error("actor slot order");
        fragments+=unsigned(got.size());
    }
    cells.clear();auto &c=cells[{0,0}];c.valid=true;c.source.resize(2);c.source[0].order=2;c.source[1].order=1;
    bool rejected=false;std::vector<Draw> out;try{actual(out,0,0,1,1,nullptr);}catch(const std::runtime_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("unordered stream guard");
    std::printf("MERGE OK 12000 stable-sort differential cases; %u fragments; ties, empty/invalid cells, cached tails, actor runs and order guard\n",fragments);
}
