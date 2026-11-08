// Codex: списки рендера растут страницами, а не удвоением непрерывного массива.
// Малые списки имеют 1/4/16 элементов; дальше 32 крупных либо 64 малых элемента.
// Свободные страницы повторно используются до границы загрузки мира.
#ifdef _MSC_VER
#define HMM_SEQUENCE_NOINLINE __declspec(noinline)
#else
#define HMM_SEQUENCE_NOINLINE __attribute__((noinline))
#endif
template<class T> class Sequence {
    // Codex: Draw по 52 байта на Xtensa должен помещаться в оставшиеся промежутки
    // 2 КиБ. Страница из 64 Draw требовала 3336 байт у ядра и ломала длинную сессию.
    static constexpr size_t shift=sizeof(T)>32?5:6, page=1u<<shift;
    struct Free {Free *next;};
    inline static Free *free_pages[4]{};
    inline static size_t held=0,available=0,peak=0;
    std::vector<T*> blocks;
    size_t length=0,space=0;
    static unsigned bin(size_t n){return n==1?0:n==4?1:n==16?2:3;}
    HMM_SEQUENCE_NOINLINE static T *take(size_t n){
        auto &head=free_pages[bin(n)];
        if(head){auto *p=head;head=p->next;available-=n*sizeof(T);return reinterpret_cast<T*>(p);}
        auto *p=static_cast<T*>(::operator new(std::max(n*sizeof(T),sizeof(Free))));
        held+=n*sizeof(T);peak=std::max(peak,held);return p;
    }
    HMM_SEQUENCE_NOINLINE static void give(T *p,size_t n){auto &head=free_pages[bin(n)];auto *f=reinterpret_cast<Free*>(p);f->next=head;head=f;available+=n*sizeof(T);}
    T *ptr(size_t i)const{return blocks[i>>shift]+(i&(page-1));}
    HMM_SEQUENCE_NOINLINE void discard(){clear();for(auto *p:blocks)give(p,std::min(space,page));blocks.clear();space=0;}
public:
    template<bool C> struct Iter {
        using iterator_category=std::random_access_iterator_tag;
        using value_type=T;using difference_type=std::ptrdiff_t;
        using pointer=typename std::conditional<C,const T*,T*>::type;
        using reference=typename std::conditional<C,const T&,T&>::type;
        const Sequence *owner{};difference_type index{};
        reference operator*()const{return *owner->ptr(size_t(index));}
        pointer operator->()const{return &**this;}
        reference operator[](difference_type n)const{return *(*this+n);}
        Iter &operator++(){++index;return *this;}Iter operator++(int){auto p=*this;++*this;return p;}
        Iter &operator--(){--index;return *this;}Iter operator--(int){auto p=*this;--*this;return p;}
        Iter &operator+=(difference_type n){index+=n;return *this;}Iter &operator-=(difference_type n){index-=n;return *this;}
        Iter operator+(difference_type n)const{auto p=*this;return p+=n;}Iter operator-(difference_type n)const{auto p=*this;return p-=n;}
        difference_type operator-(Iter p)const{return index-p.index;}
        bool operator==(Iter p)const{return index==p.index;}bool operator!=(Iter p)const{return index!=p.index;}
        bool operator<(Iter p)const{return index<p.index;}
        friend Iter operator+(difference_type n,Iter p){return p+n;}
    };
    using iterator=Iter<false>;using const_iterator=Iter<true>;
    Sequence()=default;
    template<class I> Sequence(I a,I b){try{assign(a,b);}catch(...){discard();throw;}}
    Sequence(const Sequence &v):Sequence(v.begin(),v.end()){}
    Sequence(Sequence &&v)noexcept{swap(v);}
    ~Sequence(){discard();}
    Sequence &operator=(const Sequence &v){if(this!=&v)assign(v.begin(),v.end());return *this;}
    Sequence &operator=(Sequence &&v)noexcept{swap(v);return *this;}
    iterator begin(){return {this,0};}iterator end(){return {this,std::ptrdiff_t(length)};}
    const_iterator begin()const{return {this,0};}const_iterator end()const{return {this,std::ptrdiff_t(length)};}
    T &operator[](size_t i){return *ptr(i);}const T &operator[](size_t i)const{return *ptr(i);}
    T &back(){return (*this)[length-1];}const T &back()const{return (*this)[length-1];}
    T &front(){return (*this)[0];}const T &front()const{return (*this)[0];}
    bool empty()const{return !length;}size_t size()const{return length;}size_t capacity()const{return space;}
    void swap(Sequence &v)noexcept{blocks.swap(v.blocks);std::swap(length,v.length);std::swap(space,v.space);}
    // Codex: редкий рост вынесен из горячей проверки ёмкости: общий код не
    // размножается LTO во всех местах добавления слоёв.
    HMM_SEQUENCE_NOINLINE void grow(size_t n){
        if(space<page){
            const size_t next=n<=1?1:n<=4?4:n<=16?16:page;
            T *p=take(next);
            if(blocks.empty())try{blocks.push_back(p);}catch(...){give(p,next);throw;}
            else {for(size_t i=0;i<length;++i){new(p+i)T(std::move((*this)[i]));(*this)[i].~T();}give(blocks[0],space);blocks[0]=p;}
            space=next;
        }
        while(space<n){T *p=take(page);try{blocks.push_back(p);}catch(...){give(p,page);throw;}space+=page;}
    }
    void reserve(size_t n){if(n>space)grow(n);}
    HMM_SEQUENCE_NOINLINE void resize(size_t n){reserve(n);while(length<n){new(ptr(length))T();++length;}while(length>n)ptr(--length)->~T();}
    void clear(){resize(0);}
    HMM_SEQUENCE_NOINLINE void push_back(const T &v){reserve(length+1);new(ptr(length))T(v);++length;}
    HMM_SEQUENCE_NOINLINE void push_back(T &&v){reserve(length+1);new(ptr(length))T(std::move(v));++length;}
    HMM_SEQUENCE_NOINLINE iterator erase(iterator a,iterator b){const size_t first=size_t(a.index),last=size_t(b.index);for(size_t i=last;i<length;++i)(*this)[first+i-last]=std::move((*this)[i]);resize(length-(last-first));return {this,std::ptrdiff_t(first)};}
    iterator erase(iterator a){return erase(a,a+1);}
    template<class I> void assign(I a,I b){clear();reserve(size_t(b-a));for(;a!=b;++a)push_back(*a);}
    template<class I> iterator insert(iterator at,I a,I b){const size_t pos=size_t(at.index),n=size_t(b-a),old=length;resize(old+n);for(size_t i=old;i>pos;--i)(*this)[i+n-1]=std::move((*this)[i-1]);for(size_t i=0;i<n;++i,++a)(*this)[pos+i]=*a;return {this,std::ptrdiff_t(pos)};}
    // Codex: возвращаем лишние страницы в местный пул без копирования живых слоёв.
    HMM_SEQUENCE_NOINLINE void trim(){
        while(space>=page&&space-length>=page&&blocks.size()>1){give(blocks.back(),page);blocks.pop_back();space-=page;}
        if(space>=page&&length<=16){
            const size_t n=length<=1?1:length<=4?4:16;T *p=take(n);
            for(size_t i=0;i<length;++i){new(p+i)T(std::move((*this)[i]));(*this)[i].~T();}
            give(blocks[0],page);blocks[0]=p;space=n;
        }
    }
    HMM_SEQUENCE_NOINLINE static void release_unused(){for(unsigned b=0;b<4;++b){const size_t bytes=(b==3?page:size_t(1)<<(b*2))*sizeof(T);while(auto *p=free_pages[b]){free_pages[b]=p->next;::operator delete(p);held-=bytes;available-=bytes;}}}
    // Codex: пустая страница может обслужить другой класс общего выделителя,
    // без free/malloc ядра и без сброса всего кэша рендера.
    static void *reuse_page(size_t minimum){const size_t bytes=page*sizeof(T);auto *p=free_pages[3];if(!p||bytes<minimum)return nullptr;free_pages[3]=p->next;held-=bytes;available-=bytes;return p;}
    static size_t bytes(){return held;}static size_t free_bytes(){return available;}static size_t high(){return peak;}
};
#undef HMM_SEQUENCE_NOINLINE
