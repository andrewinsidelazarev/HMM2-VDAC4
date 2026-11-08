"""Codex: исходный Redraw записывает только изменившиеся клетки кольца FT812."""
def patch(name,s):
    if name!='interface_gamearea.cpp':return s
    s='#include "game.h"\n#include "map_object_info.h"\n'+s
    marker='#include "hmm_map_records.inc"'
    assert s.count(marker)==1
    s=s.replace(marker,marker+'\n#include "hmm_ring_world.inc"')
    marker='    const fheroes2::Rect & tileROI = GetVisibleTileROI();'
    a=s.index('void Interface::GameArea::Redraw(');b=s.index('void Interface::GameArea::redrawOnlyFog(',a)
    head,body,tail=s[:a],s[a:b],s[b:]
    assert body.count(marker)==1
    body=body.replace(marker,marker+'''
#ifdef HMM2_PACKED
    // Codex: фаза анимации и изменившиеся входы помечают клетки, остальные
    // сохраняют исходную последовательность слоёв без повторной записи движком.
    const bool hmmRing=!isPuzzleDraw&&!_interface.isEditor()
        &&hmm_gpu_ring_start(this,flag,Game::getAdventureMapAnimationIndex());
    if(hmmRing){
        const auto *focus=(flag&LEVEL_HEROES)?GetFocusHeroes():nullptr;
        if(_animationInfo.empty()&&hmmRingTryMotion(focus))return;
        hmmRingCheckWorld(tileROI,focus,*this);
    }
#endif
''')
    # Codex: земля и три нижних прохода не выходят за свою клетку; верхние флаги
    # и tile-unfit требуют соседей. Сам map_record обрезает только грязные клетки.
    marker='                else {\n                    const Maps::Tile & tile = world.getTile( offset.x, offset.y );'
    assert body.count(marker)==1
    body=body.replace(marker,'''                else {
#ifdef HMM2_PACKED
                    if(!hmm_gpu_ring_need(offset.x,offset.y))continue;
#endif
                    const Maps::Tile & tile = world.getTile( offset.x, offset.y );''')
    marker='            const int32_t tileIndex = offset + posX;'
    assert body.count(marker)==1
    body=body.replace(marker,'''#ifdef HMM2_PACKED
            if(!hmm_gpu_ring_need(posX,posY,3))continue;
#endif
'''+marker)
    marker='            const Maps::Tile & tile = world.getTile( x + offset );'
    assert body.count(marker)==4
    body=body.replace(marker,'''#ifdef HMM2_PACKED
            if(!hmm_gpu_ring_need(x,y))continue;
#endif
'''+marker)
    marker='            const int32_t tileIndex = x + offset;'
    assert body.count(marker)==1
    body=body.replace(marker,'''#ifdef HMM2_PACKED
            if(!hmm_gpu_ring_need(x,y,2))continue;
#endif
'''+marker)
    marker='    // Draw hero\'s route. It should be drawn on top of everything.'
    body=body.replace(marker,'''#ifdef HMM2_PACKED
    // Codex: маршрут остаётся живым FT-слоем, как герой, и не запекает фон шага.
    const HmmMapKind hmm_route_kind(5);
#endif
'''+marker)
    # Codex: область действия hmm_route_kind — существующие скобки HmmMapStatic.
    s=head+body+tail
    marker='Interface::GameArea::GameArea( BaseInterface & interface )'
    assert s.count(marker)==1
    return s.replace(marker,'''#ifdef HMM2_PACKED
// Codex: снимки входов графики освобождаются до загрузки мира/экрана города.
extern "C" void hmm_ring_world_clear(){hmmRingReleaseWorld();}
#endif
'''+marker)
