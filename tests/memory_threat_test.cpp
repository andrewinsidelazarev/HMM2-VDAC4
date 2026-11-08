// Codex: границы страниц и точность тех же double по сравнению с непрерывным массивом.
#include <cassert>
#include <cstdio>
#include <vector>
#include "../main/hmm_threat_array.inc"
int main(){
    for(size_t n:{0u,1u,63u,64u,65u,11664u,20736u}){
        HmmThreatArray pages(n);std::vector<double> reference(n,0.0);
        assert(pages.size()==reference.size());
        for(size_t i=0;i<n;++i){assert(pages[i]==reference[i]);pages[i]+=double(i)*0.125;reference[i]+=double(i)*0.125;}
        const auto moved=std::move(pages);
        for(size_t i=0;i<n;++i)assert(moved[i]==reference[i]);
    }
    std::puts("AI pages: boundaries, zero initialization, move and exact double values OK");
}
