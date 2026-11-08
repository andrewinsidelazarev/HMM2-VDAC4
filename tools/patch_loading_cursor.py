"""Codex: точки обслуживания загрузки в подготовленной копии оригинала."""
def patch(name,s):
    if name in ('game_scenarioinfo.cpp','game_io.cpp','world_loadmap.cpp','world_regions.cpp','world.cpp','game_startgame.cpp','battle_main.cpp','battle_interface.cpp','castle_dialog.cpp'):
        s='#ifdef HMM2_PACKED\n#include "hmm_loading.h"\n#endif\n'+s
    signatures={'game_scenarioinfo.cpp':'    fheroes2::GameMode LoadNewMap()\n    {',
                'game_io.cpp':'fheroes2::GameMode Game::Load( const std::string & filePath )\n{'}
    if name in signatures:
        a=signatures[name];assert a in s
        s=s.replace(a,a+'\n#ifdef HMM2_PACKED\n    HmmLoadingScope hmmLoading("Map loading...");\n#endif',1)
    # Codex: обслуживание длинных проходов мира, включая метаданные и проходимость.
    if name in ('world_loadmap.cpp','world_regions.cpp','world.cpp'):
        import re
        s=re.sub(r'(for \( [^\n]+ \) \{)',
                 r'\1\n#ifdef HMM2_PACKED\n    {static unsigned hmmTicks;if(!(++hmmTicks&15))hmm_loading_tick();}\n#endif',s)
    if name=='world_regions.cpp':
        a='    void CheckAdjacentTiles( std::vector<MapRegionNode> & rawData, MapRegion & region, uint32_t rawDataWidth, const std::vector<int> & offsets )\n    {'
        assert a in s
        s=s.replace(a,a+'\n#ifdef HMM2_PACKED\n        {static unsigned ticks;if(!(++ticks&15))hmm_loading_tick();}\n#endif',1)
    if name=='game_startgame.cpp':
        a='fheroes2::GameMode Interface::AdventureMap::StartGame()\n{'
        assert a in s
        # Клод: «Map loading...» — сразу после чтения мира: StartGame рисует интерфейс с пустой картой задолго до проявления
        # (плата, сборка 95, 04.11: «а где map loading?»); снимается при проявлении карты в HumanTurn (hmm_status_hold_arm).
        s=s.replace(a,a+'\n#ifdef HMM2_PACKED\n    auto hmmLoading=std::make_unique<HmmLoadingScope>();hmm_status_hold("Map loading...");\n#endif',1)
        a='    while ( res == fheroes2::GameMode::END_TURN ) {'
        assert a in s
        s=s.replace(a,'#ifdef HMM2_PACKED\n    hmmLoading.reset();\n#endif\n'+a,1)
        a='fheroes2::GameMode Interface::AdventureMap::HumanTurn( const bool isLoadedFromSave )\n{'
        assert a in s
        s=s.replace(a,a+'\n#ifdef HMM2_PACKED\n    auto hmmLoading=std::make_unique<HmmLoadingScope>();\n#endif',1)
        a='    validateFadeInAndRender();'
        # Codex: это единственный такой вызов в HumanTurn; снять WAIT после первого готового окна.
        at=s.index(a,s.index('fheroes2::GameMode Interface::AdventureMap::HumanTurn'))
        # Клод: «Map loading...» остаётся поверх карты, пока её кадры не станут лёгкими (пользователь, 03.11: убирать табличку,
        # только когда реально можно играть); только первый ход после загрузки мира.
        hold='#ifdef HMM2_PACKED\n    hmm_status_hold("Map loading...");\n#endif\n'
        s=s[:at]+hold+s[at:];at+=len(hold)+len(a)
        s=s[:at]+'\n#ifdef HMM2_PACKED\n    hmmLoading.reset();hmm_status_hold_arm();\n#endif'+s[at:]
    if name in ('world.cpp','world_loadmap.cpp'):
        import re
        # Codex: те же конструкторы тайлов в том же порядке, с обслуживанием между порциями.
        s=re.sub(r'(?m)^( *)(vec_tiles|w\.vec_tiles)\.resize\( ([^;]+) \);$',
            lambda m:'#ifdef HMM2_PACKED\n'+m[1]+'{const size_t target='+m[3]+';auto &tiles='+m[2]+';tiles.reserve(target);\n'+m[1]+'if(tiles.size()>target)tiles.resize(target);\n'+m[1]+'while(tiles.size()<target){tiles.resize(std::min(target,tiles.size()+128));hmm_loading_tick();}}\n#else\n'+m[0]+'\n#endif',s)
    if name=='heroes.cpp':
        s='#ifdef HMM2_PACKED\n#include "hmm_loading.h"\n#endif\n'+s
        a='            _heroes.emplace_back( std::make_unique<Heroes>( hid, race ) );'
        assert a in s
        s=s.replace(a,a+'\n#ifdef HMM2_PACKED\n            hmm_loading_tick();\n#endif',1)
    # Клод: подпись на долгих паузах, чтобы было понятно, из-за чего игра стоит.
    def status(s,a,before,after=''):
        assert s.count(a)==1,a
        return s.replace(a,'#ifdef HMM2_PACKED\n'+before+'\n#endif\n'+a+('\n#ifdef HMM2_PACKED\n'+after+'\n#endif' if after else ''),1)
    if name=='game_startgame.cpp':
        # Клод: при смене дня и ходе врагов подписей нет: «Enemy turn» и «Next day...» закрывали движения врагов, а новый день
        # теперь наступает быстро (пользователь, 02.11: «табличку Next Day не нужно»). Перед ходом игрока снимается оставшаяся подпись.
        s=status(s,'                    res = HumanTurn( isLoadedFromSave );','                    if(!hmm_status_holding())hmm_status_clear();')
    if name=='battle_main.cpp':
        # Клод: бой выделяет крупные блоки, а фоновые звуки карты в нём молчат — их буферы отдаются до Arena (сборка 74: bad_alloc на входе в бой).
        s=status(s,'        Arena arena( attackingArmy, defendingArmy, tileIndex, showBattle, randomGenerator );','        hmm_audio_drop_loops();if(showBattle)hmm_status_show("Battle loading...",false);')
    if name=='battle_interface.cpp':
        s=status(s,'    fheroes2::fadeInDisplay( _background->activeArea(), !isDefaultScreenSize );\n}\n\nvoid Battle::Interface::Redraw()','    hmm_status_clear();')
    if name=='castle_dialog.cpp':
        s=status(s,'    fheroes2::Display & display = fheroes2::Display::instance();\n\n    fheroes2::Rect dialogRoi;','    if(fade)hmm_status_show("Entering castle...",true);')
    if name=='localevent.h':
        a='    const fheroes2::Point & getMouseCursorPos() const'
        assert a in s
        s=s.replace(a,'#ifdef HMM2_PACKED\n    // Codex: только положение мыши во время загрузки, без игровых действий.\n    void hmmLoadingMouse(int x,int y){onMouseMotionEvent({x,y});}\n#endif\n'+a,1)
    return s
