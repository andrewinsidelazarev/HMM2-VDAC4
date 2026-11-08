/* Immutable split recipes shared by every instance of a monster pose.
 * Leases survive cache eviction/low-memory trimming; per-instance alpha is
 * applied to the queue copy, never to the shared recipe. No image pixels. */
struct HmmMonsterRecipe {
    Maps::HmmMonsterParts parts;
    uint32_t first=0,second=0,stamp=0;
    int icn=0;
    unsigned bytes=0;
    uint16_t next=0;
};
std::array<HmmMonsterRecipe,128> hmm_monster_recipes;
std::array<uint16_t,256> hmm_monster_recipe_heads{};
unsigned hmmMonsterRecipeBucket(int icn,uint32_t first,uint32_t second){
    return ((first*UINT32_C(0x9e3779b9))^(second*UINT32_C(0x85ebca6b))^(uint32_t(icn)*UINT32_C(0xc2b2ae35)))>>24;
}
#ifdef HMM2_HOST
std::atomic_flag hmm_monster_recipe_lock=ATOMIC_FLAG_INIT;
#else
extern "C" int hmm_flag_take(int *);
int hmm_monster_recipe_lock;
#endif
unsigned hmm_monster_recipe_bytes,hmm_monster_recipe_clock,hmm_monster_recipe_hits,hmm_monster_recipe_builds;
struct HmmMonsterRecipeLock {
    HmmMonsterRecipeLock(){
#ifdef HMM2_HOST
        while(hmm_monster_recipe_lock.test_and_set(std::memory_order_acquire)){
        std::this_thread::yield();
#else
        // The ELF lives in PSRAM: native S32C1I is not atomic there.
        // Serialize the flag through the existing internal-RAM IO mutex.
        while(!hmm_flag_take(&hmm_monster_recipe_lock)){
        hmm_core->delay(1);
#endif
    }}
    ~HmmMonsterRecipeLock(){
#ifdef HMM2_HOST
        hmm_monster_recipe_lock.clear(std::memory_order_release);
#else
        __atomic_store_n(&hmm_monster_recipe_lock,0,__ATOMIC_RELEASE);
#endif
    }
};
Maps::HmmMonsterParts hmmMonsterRecipe(int icn,std::pair<uint32_t,uint32_t> frames){
    const unsigned bucket=hmmMonsterRecipeBucket(icn,frames.first,frames.second);
    {HmmMonsterRecipeLock lock;
        for(unsigned index=hmm_monster_recipe_heads[bucket];index;){auto &p=hmm_monster_recipes[index-1];
            if(p.icn==icn&&p.first==frames.first&&p.second==frames.second){
            p.stamp=++hmm_monster_recipe_clock;++hmm_monster_recipe_hits;return p.parts;
            }index=p.next;
        }
    }
    // Allocation stays outside the lock: malloc recovery may trim this cache.
    auto parts=std::make_shared<std::vector<fheroes2::ObjectRenderingInfo>>();
    auto append=[&](uint32_t frame){const auto &im=fheroes2::AGG::GetICN(icn,frame);
        hmmAppendSquares(*parts,{im.x()+monsterImageOffset.x,im.y()+monsterImageOffset.y},im,icn,frame,false);};
    append(frames.first);if(frames.second)append(frames.second);
    const unsigned bytes=unsigned(parts->capacity()*sizeof(fheroes2::ObjectRenderingInfo)+sizeof(*parts)+32);
    Maps::HmmMonsterParts result=parts;
    {HmmMonsterRecipeLock lock;++hmm_monster_recipe_builds;
        if(bytes>48*1024)return result;
        for(;;){
            HmmMonsterRecipe *empty=nullptr,*oldest=nullptr;
            for(auto &p:hmm_monster_recipes){if(!p.parts){if(!empty)empty=&p;}else if(!oldest||p.stamp<oldest->stamp)oldest=&p;}
            if(empty&&hmm_monster_recipe_bytes+bytes<=48*1024){
                empty->parts=result;empty->icn=icn;empty->first=frames.first;empty->second=frames.second;
                empty->stamp=++hmm_monster_recipe_clock;empty->bytes=bytes;hmm_monster_recipe_bytes+=bytes;
                empty->next=hmm_monster_recipe_heads[bucket];hmm_monster_recipe_heads[bucket]=uint16_t(empty-hmm_monster_recipes.data()+1);break;
            }
            if(!oldest)break;
            uint16_t *link=&hmm_monster_recipe_heads[hmmMonsterRecipeBucket(oldest->icn,oldest->first,oldest->second)];
            const unsigned removed=unsigned(oldest-hmm_monster_recipes.data()+1);
            while(*link&&*link!=removed)link=&hmm_monster_recipes[*link-1].next;
            if(*link)*link=oldest->next;
            hmm_monster_recipe_bytes-=oldest->bytes;*oldest=HmmMonsterRecipe{};
        }
    }
    return result;
}
