/* Codex: сохраняем порядок, стабильность адресов, sort/splice/erase при переходах между страницами. */
#include "hmm_node_allocator.h"
#include <vector>
#include <cassert>
#include <cstdio>
struct Item {unsigned id,value;bool operator==(const Item&o)const{return id==o.id&&value==o.value;}};
int main(){
    for(unsigned pass=0;pass<8;pass++){
        std::list<Item,HmmNodeAllocator<Item>> actual;std::list<Item> expected;std::vector<const Item*>addresses;
        for(unsigned i=0;i<20000;i++){Item v{i,(i*1664525+pass)%173};actual.push_back(v);expected.push_back(v);addresses.push_back(&actual.back());}
        auto compare=[](const Item&a,const Item&b){return a.value<b.value;};actual.sort(compare);expected.sort(compare);
        auto a=actual.begin();auto b=expected.begin();for(;a!=actual.end();++a,++b){assert(*a==*b);assert(&*a==addresses[a->id]);}
        actual.remove_if([](const Item&i){return i.id%3==0;});expected.remove_if([](const Item&i){return i.id%3==0;});
        auto copy=actual;assert(copy==actual);auto last=actual.begin();std::advance(last,5000);copy.splice(copy.end(),actual,actual.begin(),last);
        assert(copy.size()+actual.size()==expected.size()*2);copy.clear();actual.clear();
    }
    puts("NODES OK 160000 elements; stable addresses, stable sort, erase, copy, splice, clear");return 0;
}
