/* Codex: запись частей карты для FT812 без дерева и отдельной очереди на клетку.
 * Куски записываются в страницы по 32 элемента, индексы сортируются в точности как
 * исходный map<Point, deque>: Point сравнивается по x, затем y; emplace_front
 * идёт в обратном порядке вставок, emplace_back — в прямом. Порядок слоёв прежний.
 * После Redraw страницы освобождаются, чтобы память карты не оставалась у города. */
bool hmm_hero_records=false;
bool hmm_records_fog=false;
struct HmmMapRecordFogScope {
    bool previous;
    explicit HmmMapRecordFogScope(bool enabled):previous(hmm_records_fog){hmm_records_fog=enabled;}
    ~HmmMapRecordFogScope(){hmm_records_fog=previous;}
};
class HmmTileImages {
public:
    struct Piece {fheroes2::Point offset;int order;fheroes2::ObjectRenderingInfo info;bool hero,skip;};
private:
    struct Release {~Release(){Sequence<Piece>::release_unused();}} release;
    mutable Sequence<Piece> pieces;
    void append(const fheroes2::Point &offset,const fheroes2::ObjectRenderingInfo &info,bool front){
        // A square wholly inside an unchanged cell cannot reach a dirty cell.
        // Keep unusual cross-cell pieces on the conservative original path.
        const bool skip=info.area.width>0&&info.area.height>0&&info.imageOffset.x>=0&&info.imageOffset.y>=0&&
           info.imageOffset.x+info.area.width<=32&&info.imageOffset.y+info.area.height<=32&&
           (!hmm_gpu_ring_need(offset.x,offset.y)||
            (hmm_records_fog&&offset.x>=0&&offset.y>=0&&offset.x<world.w()&&offset.y<world.h()&&
             world.getTile(offset.x,offset.y).getFogDirection()==DIRECTION_ALL));
        const int order=int(pieces.size())+1;pieces.push_back(Piece{offset,front?-order:order,info,hmm_hero_records,skip});
    }
public:
    struct Slot {
        HmmTileImages &owner;fheroes2::Point offset;
        void emplace_front(const fheroes2::ObjectRenderingInfo &info){owner.append(offset,info,true);}
        void emplace_back(const fheroes2::ObjectRenderingInfo &info){owner.append(offset,info,false);}
    };
    Slot operator[](const fheroes2::Point &offset){return {*this,offset};}
    struct Ordered {
        const Sequence<Piece> &pieces;std::vector<uint16_t> indices;
        struct Iterator {
            const Ordered &view;size_t at;
            const Piece &operator*()const{return view.pieces[view.indices.empty()?at:view.indices[at]];}
            Iterator &operator++(){++at;return *this;}
            bool operator!=(const Iterator &other)const{return at!=other.at;}
        };
        Iterator begin()const{return {*this,0};}
        Iterator end()const{return {*this,pieces.size()};}
    };
#ifndef HMM2_HOST
    __attribute__((optimize("O2"),noinline))
#endif
    Ordered ordered()const{
        auto less=[](const Piece &a,const Piece &b){
            if(a.offset<b.offset)return true;if(b.offset<a.offset)return false;return a.order<b.order;};
        Ordered result{pieces,{}};
        // Codex: при прокрутке сортируются двухбайтовые индексы, а не крупные
        // ObjectRenderingInfo в PSRAM. Тот же строгий ключ сохраняет весь порядок.
        // При необычном размере либо нехватке памяти остаётся сортировка записей;
        // индекс никогда не запрашивает непрерывный кусок >=128 КиБ.
        if(pieces.size()<=32768){
            try{result.indices.resize(pieces.size());}catch(...){result.indices.clear();}
        }
        if(!result.indices.empty()){
            bool bucket=false;
            // The game task is the only producer/consumer of these queues.
            // Reuse 8 KiB of scratch, avoiding another heap allocation during Redraw.
            static uint16_t scratch[4096];
            if(pieces.size()<=4096){
                auto minx=pieces[0].offset.x,maxx=minx,miny=pieces[0].offset.y,maxy=miny;unsigned fronts=0;
                for(const auto &p:pieces){minx=std::min(minx,p.offset.x);maxx=std::max(maxx,p.offset.x);
                    miny=std::min(miny,p.offset.y);maxy=std::max(maxy,p.offset.y);fronts+=p.order<0;}
                if(int64_t(maxx)-minx<256&&int64_t(maxy)-miny<256){
                    // First order by insertion: front entries reversed, back entries forward.
                    unsigned front=fronts,back=fronts;
                    for(unsigned i=0;i<pieces.size();++i)result.indices[pieces[i].order<0?--front:back++]=uint16_t(i);
                    uint16_t offsets[256]{};
                    for(auto i:result.indices)++offsets[pieces[i].offset.y-miny];
                    unsigned at=0;for(auto &n:offsets){const unsigned next=at+n;n=uint16_t(at);at=next;}
                    for(auto i:result.indices)scratch[offsets[pieces[i].offset.y-miny]++]=i;
                    std::fill_n(offsets,256,uint16_t(0));
                    for(unsigned j=0;j<pieces.size();++j)++offsets[pieces[scratch[j]].offset.x-minx];
                    at=0;for(auto &n:offsets){const unsigned next=at+n;n=uint16_t(at);at=next;}
                    for(unsigned j=0;j<pieces.size();++j){const auto i=scratch[j];result.indices[offsets[pieces[i].offset.x-minx]++]=i;}
                    bucket=true;
                }
            }
            if(!bucket){for(size_t i=0;i<pieces.size();++i)result.indices[i]=uint16_t(i);
                std::sort(result.indices.begin(),result.indices.end(),[&](uint16_t a,uint16_t b){return less(pieces[a],pieces[b]);});}
#ifdef HMM2_HOST
            static const bool verify=std::getenv("HMM2_QUEUE_SORT_VERIFY")!=nullptr;
            if(verify){std::vector<uint16_t> reference(pieces.size());for(unsigned i=0;i<pieces.size();++i)reference[i]=uint16_t(i);
                std::sort(reference.begin(),reference.end(),[&](uint16_t a,uint16_t b){return less(pieces[a],pieces[b]);});
                if(reference!=result.indices)throw std::runtime_error("map queue order differs from original sort");}
#endif
        }else std::sort(pieces.begin(),pieces.end(),less);
        return result;
    }
};
