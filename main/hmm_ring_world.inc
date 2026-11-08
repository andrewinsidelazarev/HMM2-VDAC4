/* Codex: точный снимок входов графики, а не хэш. Небольшие списки частей
 * сравниваются один раз на Redraw; неизменная клетка не вызывает AGG/Blit. */
struct HmmRingWorldTile {int x=0,y=0;bool animated=false;const Heroes *hero=nullptr;std::vector<uint8_t> key;};
std::array<std::unique_ptr<HmmRingWorldTile>,1024> hmm_ring_world;
std::vector<uint32_t> hmm_ring_route;
std::vector<fheroes2::Point> hmm_ring_route_tiles;
unsigned hmm_ring_world_generation;
unsigned hmm_ring_world_phase;
using HmmHeroKey=std::array<int32_t,12>;
std::vector<const Heroes*> hmm_ring_visible_heroes;
std::array<HmmHeroKey,16> hmm_ring_hero_keys;
const Heroes *hmm_ring_visual_focus;
bool hmm_ring_visual_valid,hmm_ring_focus_parts_valid;
std::vector<fheroes2::ObjectRenderingInfo> hmm_ring_focus_body,hmm_ring_focus_shadow;
HmmHeroKey hmmRingHeroKey(const Heroes &h){const auto p=h.getCurrentPixelOffset();return {
    h.GetIndex(),h.GetDirection(),h.GetSpriteIndex(),p.x,p.y,int(h.isMoveEnabled()),int(h.isShipMaster()),int(h.isInDeepOcean()),
    h.getAlphaValue(),h.GetAttackedMonsterTileIndex(),int(h.GetColor()),h.GetPath().empty()?int(Direction::UNKNOWN):h.GetPath().GetFrontDirection()};}
bool hmmRingSubjectParts(const std::vector<fheroes2::ObjectRenderingInfo> &body,const std::vector<fheroes2::ObjectRenderingInfo> &shadow,
                         const Heroes &hero,std::vector<HmmSubjectPart> &out){
    const auto center=hero.GetCenter();out.reserve(body.size()+shadow.size());
    auto append=[&](const auto &list){for(const auto &p:list){const auto &im=fheroes2::AGG::GetICN(p.icnId,p.icnIndex);uint32_t key;
        if(!hmm_resource_id(im,key)||(hmm_resource_flags(im)&2))return false;
        out.push_back({key,(center.x+p.tileOffset.x)*32+p.imageOffset.x,(center.y+p.tileOffset.y)*32+p.imageOffset.y,
            p.area.width,p.area.height,p.isFlipped?im.width()-p.area.x-1:p.area.x,p.area.y,p.isFlipped?-1:1,im.singleLayer()});
    }return true;};
    return append(body)&&append(shadow);
}
bool hmmRingTryMotion(const Heroes *focus){
#ifdef HMM2_HOST
    if(std::getenv("HMM2_NO_SUBJECT_MOTION"))return false;
#endif
    if(!hmm_gpu_map_animation_reuse()||!hmm_ring_visual_valid||focus!=hmm_ring_visual_focus||
       hmm_ring_world_generation!=hmm_gpu_ring_generation()||hmm_ring_world_phase!=Game::getAdventureMapAnimationIndex())return false;
    std::array<HmmHeroKey,16> keys;int changed=-1;
    for(unsigned i=0;i<hmm_ring_visible_heroes.size();++i){const auto *h=hmm_ring_visible_heroes[i];keys[i]=hmmRingHeroKey(*h);
        if(keys[i]==hmm_ring_hero_keys[i])continue;
        if(h!=focus||changed!=-1||!hmm_ring_focus_parts_valid||keys[i][8]!=255)return false;
        for(unsigned j=0;j<12;++j)if(j!=2&&j!=3&&j!=4&&keys[i][j]!=hmm_ring_hero_keys[i][j])return false;
        changed=int(i);
    }
    if(changed<0)return hmm_gpu_ring_subject(nullptr,0,nullptr,0);
    auto body=Maps::getHeroSpritesPerTile(*focus),shadow=Maps::getHeroShadowSpritesPerTile(*focus);
    std::vector<HmmSubjectPart> before,after;
    if(!hmmRingSubjectParts(hmm_ring_focus_body,hmm_ring_focus_shadow,*focus,before)||!hmmRingSubjectParts(body,shadow,*focus,after))return false;
    if(!hmm_gpu_ring_subject(before.data(),unsigned(before.size()),after.data(),unsigned(after.size())))return false;
    hmm_ring_focus_body=std::move(body);hmm_ring_focus_shadow=std::move(shadow);hmm_ring_hero_keys[unsigned(changed)]=keys[unsigned(changed)];return true;
}
void hmmRingReleaseWorld(){
    hmm_monster_parts_clear();
    for(auto &p:hmm_ring_world)p.reset();std::vector<uint32_t>().swap(hmm_ring_route);
    std::vector<fheroes2::Point>().swap(hmm_ring_route_tiles);hmm_ring_world_generation=0;
    std::vector<const Heroes*>().swap(hmm_ring_visible_heroes);hmm_ring_visual_valid=hmm_ring_focus_parts_valid=false;
    std::vector<fheroes2::ObjectRenderingInfo>().swap(hmm_ring_focus_body);std::vector<fheroes2::ObjectRenderingInfo>().swap(hmm_ring_focus_shadow);
}
void hmmRingCheckWorld(const fheroes2::Rect &roi,const Heroes *hero,const Interface::GameArea &area){
    hmm_ring_visible_heroes.clear();hmm_ring_focus_parts_valid=false;
    const unsigned generation=hmm_gpu_ring_generation();
    const unsigned phase=Game::getAdventureMapAnimationIndex();const bool phaseChanged=phase!=hmm_ring_world_phase;hmm_ring_world_phase=phase;
    const bool reuse=hmm_gpu_map_animation_reuse();
    if(generation!=hmm_ring_world_generation){
        for(auto &p:hmm_ring_world)p.reset();hmm_ring_route.clear();hmm_ring_route_tiles.clear();hmm_ring_world_generation=generation;
    }
    std::vector<uint8_t> key;
    auto append=[&](uint32_t v){do{key.push_back(uint8_t(v&127)|(v>=128?128:0));v>>=7;}while(v);};
    // Codex: снимки входов за границами окна с ореолом не нужны. Последний
    // готовый тайл остаётся в кольце; при возвращении ключ проверится заново.
    static fheroes2::Rect previous;
    if(previous.x!=roi.x||previous.y!=roi.y||previous.width!=roi.width||previous.height!=roi.height){
        previous=roi;
        for(auto &p:hmm_ring_world)if(p&&(p->x<roi.x-3||p->x>=roi.x+roi.width+3||p->y<roi.y-3||p->y>=roi.y+roi.height+3))p.reset();
    }
    for(int y=std::max<int>(0,roi.y-3);y<std::min<int>(world.h(),roi.y+roi.height+3);++y)
        for(int x=std::max<int>(0,roi.x-3);x<std::min<int>(world.w(),roi.x+roi.width+3);++x){
            auto &prior=hmm_ring_world[(unsigned(y)&31)*32+(unsigned(x)&31)];
            // A camera/subpixel animation cannot change world inputs. New strips
            // and every redraw after Move/actions still take the complete key.
            if(!reuse||!prior||prior->x!=x||prior->y!=y){
            const auto &tile=world.getTile(x,y);key.clear();
            append(tile.getTerrainImageIndex()|(uint32_t(tile.getTerrainFlags())<<16));
            append(tile.getFogDirection());append(uint32_t(tile.getMainObjectType()));
            append(uint32_t(tile.getMainObjectType(false)));append(uint32_t(tile.getBoatOwnerColor()));
            // Codex: затухание после подбора/боя меняет графику без изменения SAV.
            // Его альфа входит в точный ключ, а смешивание выполняет FT812.
            append(area.getObjectAlphaValue(y*world.w()+x,tile.getMainObjectType(false)));
            for(auto v:tile.metadata())append(v);
            auto part=[&](const Maps::ObjectPart &p){append(p._uid);append(uint32_t(p.layerType)|(uint32_t(p.icnType)<<8)|(uint32_t(p.icnIndex)<<24));append(area.getObjectAlphaValue(p._uid));};
            part(tile.getMainObjectPart());append(uint32_t(tile.getGroundObjectParts().size()));
            for(const auto &p:tile.getGroundObjectParts())part(p);
            for(const auto &p:tile.getTopObjectParts())part(p);
            if(tile.getMainObjectType()==MP2::OBJ_HERO)append(uint32_t(tile.getHero()->GetAttackedMonsterTileIndex()));
            if(!prior)prior.reset(new HmmRingWorldTile);
            prior->hero=tile.getMainObjectType()==MP2::OBJ_HERO?tile.getHero():nullptr;
            if(prior->x!=x||prior->y!=y||prior->key!=key){
                hmm_gpu_ring_dirty(x,y,3);prior->x=x;prior->y=y;prior->key=key;
                const auto type=tile.getMainObjectType(false);
                bool animated=type==MP2::OBJ_MONSTER||type==MP2::OBJ_ABANDONED_MINE||type==MP2::OBJ_MINE;
                auto animation=[&](const Maps::ObjectPart &p){
                    if(p.icnType==MP2::OBJ_ICN_TYPE_UNKNOWN||p.icnIndex==255)return;
                    const auto *info=Maps::getObjectPartByIcn(p.icnType,p.icnIndex);if(info&&info->animationFrames)animated=true;
                };
                animation(tile.getMainObjectPart());for(const auto &p:tile.getGroundObjectParts())animation(p);for(const auto &p:tile.getTopObjectParts())animation(p);
                prior->animated=animated;
            }
            }
            // Codex: новая фаза может выйти за прежний прямоугольник спрайта.
            // Помечается область источника, включая ещё пустые соседние клетки.
            if(phaseChanged&&prior->animated)hmm_gpu_ring_dirty(x,y,3);
            // Codex: прежний след героя помечен в кольце, новый задают исходные
            // функции нарезки OpenHMM2. Не пересобираем все 49 соседних клеток.
            if(const auto *h=prior->hero){const auto &center=h->GetCenter();
                auto mark=[&](const auto &parts){for(const auto &p:parts)hmm_gpu_ring_dirty(center.x+p.tileOffset.x,center.y+p.tileOffset.y,0);};
                auto body=Maps::getHeroSpritesPerTile(*h),shadow=Maps::getHeroShadowSpritesPerTile(*h);mark(body);mark(shadow);
                hmm_ring_visible_heroes.push_back(h);
                if(h==hero){hmm_ring_focus_body=std::move(body);hmm_ring_focus_shadow=std::move(shadow);hmm_ring_focus_parts_valid=true;}
            }
        }
    hmm_ring_visual_focus=hero;hmm_ring_visual_valid=hmm_ring_visible_heroes.size()<=16;
    if(hmm_ring_visual_valid)for(unsigned i=0;i<hmm_ring_visible_heroes.size();++i)hmm_ring_hero_keys[i]=hmmRingHeroKey(*hmm_ring_visible_heroes[i]);
    if(reuse)return; // Camera/subpixel-only redraw cannot change the route.
    std::vector<uint32_t> routeKey;std::vector<fheroes2::Point> points;
    if(hero){
        const auto &path=hero->GetPath();routeKey={uint32_t(hero->GetIndex()),uint32_t(hero->GetDirection()),uint32_t(hero->isMoveEnabled()),
            uint32_t(path.isShow()),uint32_t(path.GetAllowedSteps()),uint32_t(hero->GetLevelSkill(Skill::Secondary::PATHFINDING))};
        for(const auto &step:path){routeKey.push_back(uint32_t(step.GetIndex()));routeKey.push_back(uint32_t(step.GetDirection()));points.push_back(Maps::GetPoint(step.GetIndex()));}
    }
    if(routeKey!=hmm_ring_route){
        for(const auto &p:hmm_ring_route_tiles)hmm_gpu_ring_dirty(p.x,p.y,1);
        for(const auto &p:points)hmm_gpu_ring_dirty(p.x,p.y,1);
        hmm_ring_route.swap(routeKey);hmm_ring_route_tiles.swap(points);
    }
}
