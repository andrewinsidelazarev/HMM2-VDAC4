/* Codex: узлы списков объектов карты сохраняют адреса и порядок std::list,
 * но делят страницы без отдельного заголовка malloc на каждый узел.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <list>
#include <memory>
#include <cstdlib>
#include <cstdint>
#include <new>
template<class T> class HmmNodeAllocator {
    struct Page {Page*next;void*available;size_t live;};
    inline static Page *pages=nullptr;
    static constexpr size_t alignment=alignof(T)>alignof(void*)?alignof(T):alignof(void*);
    static constexpr size_t stride=(sizeof(T)+alignment-1)/alignment*alignment;
    static constexpr size_t first=(sizeof(Page)+alignment-1)/alignment*alignment;
    static constexpr size_t bytes=4096;
public:
    using value_type=T;
    HmmNodeAllocator() noexcept=default;
    template<class U> HmmNodeAllocator(const HmmNodeAllocator<U>&) noexcept{}
    T *allocate(size_t n){
        if(n!=1)return std::allocator<T>{}.allocate(n);
        Page*p=pages;while(p&&!p->available)p=p->next;
        if(!p){p=static_cast<Page*>(std::malloc(bytes));if(!p)throw std::bad_alloc();p->next=pages;pages=p;p->live=0;p->available=nullptr;
            for(size_t at=first;at+stride<=bytes;at+=stride){void*v=reinterpret_cast<uint8_t*>(p)+at;*static_cast<void**>(v)=p->available;p->available=v;}}
        void*v=p->available;p->available=*static_cast<void**>(v);++p->live;return static_cast<T*>(v);
    }
    void deallocate(T*v,size_t n)noexcept{
        if(n!=1){std::allocator<T>{}.deallocate(v,n);return;}
        Page**link=&pages;uintptr_t address=reinterpret_cast<uintptr_t>(v);
        while(*link){Page*p=*link;uintptr_t begin=reinterpret_cast<uintptr_t>(p);
            if(address>=begin+first&&address<begin+bytes){*reinterpret_cast<void**>(v)=p->available;p->available=v;if(--p->live==0){*link=p->next;std::free(p);}return;}link=&p->next;}
        std::abort();
    }
    template<class U>bool operator==(const HmmNodeAllocator<U>&)const noexcept{return true;}
    template<class U>bool operator!=(const HmmNodeAllocator<U>&)const noexcept{return false;}
};
#ifdef HMM2_PACKED
template<class T>using HmmMapList=std::list<T,HmmNodeAllocator<T>>;
#else
template<class T>using HmmMapList=std::list<T>;
#endif
