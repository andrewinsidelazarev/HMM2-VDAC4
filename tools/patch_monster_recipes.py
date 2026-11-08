"""Share immutable per-pose monster metadata, preserving per-cell queue order."""

def patch(name,s):
    if name=='maps_tiles_render.cpp':
        s='#ifdef HMM2_PACKED\n#include "hmm_monster_parts.h"\n#endif\n'+s
        marker='    bool doesHeroImageNeedToBeReflected( const int directionFrom )'
        assert s.count(marker)==1
        s=s.replace(marker,'#ifdef HMM2_PACKED\n#include "hmm_monster_parts.inc"\n#endif\n\n'+marker,1)
        marker='    std::vector<fheroes2::ObjectRenderingInfo> getMonsterSpritesPerTile('
        assert s.count(marker)==1
        at=s.index(marker)
        s=s[:at]+'''#ifdef HMM2_PACKED
    std::pair<HmmMonsterParts,HmmMonsterParts> hmmMonsterPartsPair(const Tile &tile,bool editor){
        const auto frames=GetMonsterSpriteIndices(tile,getMonsterFromTile(tile).GetSpriteIndex(),editor);
        auto body=hmmMonsterRecipe(ICN::MINI_MONSTER_IMAGE,frames);
        auto shadow=hmmMonsterRecipe(ICN::MINI_MONSTER_SHADOW,frames);
#ifdef HMM2_HOST
        static const bool verify=std::getenv("HMM2_RECIPE_VERIFY")!=nullptr;
        if(verify){
            auto equal=[](const auto &a,const auto &z){if(a.size()!=z.size())return false;
                for(size_t i=0;i<a.size();++i){const auto &p=a[i],&q=z[i];
                    if(p.tileOffset!=q.tileOffset||p.imageOffset!=q.imageOffset||p.area!=q.area||p.icnId!=q.icnId||
                       p.icnIndex!=q.icnIndex||p.isFlipped!=q.isFlipped||p.alphaValue!=q.alphaValue)return false;
                }return true;};
            if(!equal(*body,getMonsterSpritesPerTile(tile,editor))||!equal(*shadow,getMonsterShadowSpritesPerTile(tile,editor)))
                throw std::runtime_error("monster recipe differs from original splitter");
        }
#endif
        return {body,shadow};
    }
#endif
'''+s[at:]
        s+='''
#ifdef HMM2_PACKED
extern "C" void hmm_monster_parts_clear(){
    HmmMonsterRecipeLock lock;
    for(auto &p:hmm_monster_recipes)p=HmmMonsterRecipe{};
    hmm_monster_recipe_heads.fill(0);
    hmm_monster_recipe_bytes=0;
}
extern "C" void hmm_monster_parts_stats(unsigned *hit,unsigned *built,unsigned *bytes,int reset){
    HmmMonsterRecipeLock lock;*hit=hmm_monster_recipe_hits;*built=hmm_monster_recipe_builds;*bytes=hmm_monster_recipe_bytes;
    if(reset)hmm_monster_recipe_hits=hmm_monster_recipe_builds=0;
}
#endif
'''
    elif name=='interface_gamearea.cpp':
        s='#ifdef HMM2_PACKED\n#include "hmm_monster_parts.h"\n#endif\n'+s
        start=s.index('    void populateStaticTileUnfitObjectInfo(')
        end=s.index('    void populateStaticTileUnfitBackgroundObjectInfo(',start)
        body=s[start:end].replace('std::vector<fheroes2::ObjectRenderingInfo> & imageInfo','const std::vector<fheroes2::ObjectRenderingInfo> & imageInfo').replace('std::vector<fheroes2::ObjectRenderingInfo> & shadowInfo','const std::vector<fheroes2::ObjectRenderingInfo> & shadowInfo').replace('auto & objectInfo :','auto objectInfo :')
        s=s[:start]+body+s[end:]
        a='''                auto spriteInfo = getMonsterSpritesPerTile( tile, isEditor );
                auto spriteShadowInfo = getMonsterShadowSpritesPerTile( tile, isEditor );

                populateStaticTileUnfitObjectInfo( tileUnfit, spriteInfo, spriteShadowInfo, { posX, posY }, alphaValue, fogDirection );'''
        assert s.count(a)==1
        s=s.replace(a,'''#ifdef HMM2_PACKED
                const auto parts=Maps::hmmMonsterPartsPair(tile,isEditor);
                populateStaticTileUnfitObjectInfo(tileUnfit,*parts.first,*parts.second,{posX,posY},alphaValue,fogDirection);
#else
'''+a+'\n#endif',1)
    return s
