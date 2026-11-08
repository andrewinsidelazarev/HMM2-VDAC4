// Codex: проверка страниц на границах, порядка слоёв и владения снимками.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <random>
#include <vector>
#include <cstdio>
#include <array>
#include <cstdint>
#include "../main/hmm_sequence.inc"
#include "../main/hmm_palette_pages.inc"
struct Value {int n=0;std::shared_ptr<int> p;};
struct Large {uint32_t data[13]{};};
int main(){
    std::weak_ptr<int> lifetime;
    {
        Sequence<Value> a;std::vector<Value> b;std::minstd_rand random(82);
        auto owner=std::make_shared<int>(42);lifetime=owner;
        auto check=[&]{assert(a.size()==b.size());for(size_t i=0;i<b.size();++i){assert(a[i].n==b[i].n);assert(a[i].p==b[i].p);}};
        for(unsigned n:{0,1,4,5,16,17,63,64,65,127,128,129,1932}){a.resize(n);b.resize(n);check();}
        for(unsigned step=0;step<20000;++step){
            switch(random()%7){
            case 0:{Value v{int(step),owner};a.push_back(v);b.push_back(v);break;}
            case 1:if(!b.empty()){size_t first=random()%b.size(),last=first+random()%(b.size()-first+1);a.erase(a.begin()+first,a.begin()+last);b.erase(b.begin()+first,b.begin()+last);}break;
            case 2:{size_t n=random()%500;a.resize(n);b.resize(n);break;}
            case 3:{Sequence<Value> c(a);a=std::move(c);break;}
            case 4:{std::vector<Value> v(3,Value{int(step),owner});size_t at=random()%(b.size()+1);a.insert(a.begin()+at,v.begin(),v.end());b.insert(b.begin()+at,v.begin(),v.end());break;}
            case 5:a.trim();break;
            case 6:if(b.size()>1){std::rotate(a.begin(),a.begin()+1,a.end());std::rotate(b.begin(),b.begin()+1,b.end());}break;
            }check();
        }
        a.resize(64);auto *first=&a[0];a.reserve(1932);assert(first==&a[0]);
        a.clear();b.clear();owner.reset();assert(lifetime.expired());
    }
    assert(Sequence<Value>::bytes()==Sequence<Value>::free_bytes());
    // Codex: передача свободной страницы исключает её из обоих счётчиков и списка.
    const auto held=Sequence<Value>::bytes();void *page=Sequence<Value>::reuse_page(64*sizeof(Value));assert(page);
    assert(Sequence<Value>::bytes()==held-64*sizeof(Value));::operator delete(page);
    Sequence<Value>::release_unused();assert(!Sequence<Value>::bytes());
    // Codex: размер Draw на Xtensa — 52 байта. Все новые страницы вмещаются
    // в промежуток 2 КиБ; прежние элементы переживают рост и сжатие списка.
    {Sequence<Large> a;
        for(unsigned i=0;i<2000;++i){Large v;v.data[0]=i;a.push_back(v);}
        for(unsigned i=0;i<2000;++i)assert(a[i].data[0]==i);
        a.resize(45);a.trim();assert(a.capacity()==64);a.resize(3);a.trim();assert(a.capacity()==4);
    }
    assert(Sequence<Large>::bytes()==Sequence<Large>::free_bytes());
    assert(!Sequence<Large>::reuse_page(2048));
    void *small_page=Sequence<Large>::reuse_page(32*sizeof(Large));assert(small_page);::operator delete(small_page);
    Sequence<Large>::release_unused();assert(!Sequence<Large>::bytes());
    // Codex: рост таблицы страниц палитр сохраняет все прежние значения.
    {PaletteTables palettes;
        for(unsigned n=0;n<500;++n){std::array<unsigned char,256> row;for(unsigned i=0;i<256;++i)row[i]=static_cast<unsigned char>(n*7+i);
            palettes.push_back(row);assert(palettes.size()==n+1);
            for(unsigned k=0;k<=n;++k)for(unsigned i=0;i<256;++i)assert(palettes[k][i]==static_cast<unsigned char>(k*7+i));
        }
    }
    std::puts("sequence: boundaries, 20000 mixed operations, ownership OK");
}
