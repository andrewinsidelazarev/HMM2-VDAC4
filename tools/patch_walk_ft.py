"""Codex: повтор статических команд FT812 только внутри шага героя без изменения мира."""

def patch(name, source):
    if name == 'settings.cpp':
        marker = '    SetScrollSpeed( config.IntParams( "scroll speed" ) );'
        assert source.count(marker) == 1
        return source.replace(marker, '''#ifdef HMM2_PACKED
    // Codex: старый CFG игрока содержит 0 и отключает прокрутку у края/стрелками.
    // По просьбе пользователя нормализуем только 0; Save пишет уже это значение.
    const int hmmScrollSpeed=config.IntParams("scroll speed");
    SetScrollSpeed(hmmScrollSpeed==0?SCROLL_SPEED_NORMAL:hmmScrollSpeed);
#else
'''+marker+'''
#endif''')
    if name == 'interface_gamearea.cpp':
        start = source.index('#ifdef HMM2_PACKED\n    // Codex: та же очередь')
        end = source.index('    struct TileUnfitRenderObjectInfo', start)
        source = source[:start] + '''#ifdef HMM2_PACKED
#include "hmm_map_records.inc"
#else
    using HmmTileImages=std::map<fheroes2::Point,std::deque<fheroes2::ObjectRenderingInfo>>;
#endif
''' + source[end:]
        source = source.replace('std::map<fheroes2::Point, HmmRenderQueue>', 'HmmTileImages')
        marker = '        for ( const auto & [offset, imgInfo] : images ) {\n'
        assert source.count(marker) == 1
        start = source.index(marker)
        end = source.index('\n    bool isTallTopLayerObject(', start)
        old = source[start:end]
        assert old.endswith('    }\n')
        source = source[:start] + '''#ifdef HMM2_PACKED
        // Codex: тот же порядок клеток и front/back; меняется лишь хранение записей.
        for(const auto &piece:images.ordered()){
            const auto &info=piece.info;
            if(piece.skip){if(info.alphaValue)hmm_gpu_ring_skip();continue;}
            // Codex: герой остаётся динамическим слоем поверх готового фона клетки.
            const HmmMapKind hmm_piece_kind(piece.hero?4:1);
            area.BlitOnTile(output,fheroes2::AGG::GetICN(info.icnId,info.icnIndex),info.area,
                            info.imageOffset.x,info.imageOffset.y,piece.offset,info.isFlipped,info.alphaValue);
        }
#else
''' + old[:-6] + '#endif\n    }\n' + source[end:]
        marker='        assert( hero != nullptr );'
        assert source.count(marker)==1
        source=source.replace(marker,marker+'''
#ifdef HMM2_PACKED
        // Codex: метка только для записи графики; состояние героя не изменяется.
        struct HmmHeroRecords {HmmHeroRecords(){hmm_hero_records=true;}~HmmHeroRecords(){hmm_hero_records=false;}} hmm_hero_records_scope;
#endif
''')
        return source
    if name != 'game_startgame.cpp':
        return source
    declaration = '#ifdef HMM2_PACKED\n        bool hmm_animation_only=false;\n#endif\n'
    assert source.count(declaration) == 1
    source = source.replace(declaration, '')
    marker = "        // Animation of the hero's movement\n"
    assert source.count(marker) == 1
    source = source.replace(marker, declaration + marker)
    marker = '                if ( heroAnimationFrameCount > 0 ) {\n'
    assert source.count(marker) == 1
    source = source.replace(marker, marker + '''#ifdef HMM2_PACKED
                    // Codex: здесь меняются только кадр/сдвиг героя и камера.
                    // Повторяем статические участки FT812, если другого redraw ещё нет.
                    hmm_animation_only=(getRedrawMask()&REDRAW_GAMEAREA)==0;
#ifdef HMM2_HOST
                    // Codex: оракул ПК исполняет полную исходную запись каждого шага.
                    if(std::getenv("HMM2_NO_WALK_STATIC"))hmm_animation_only=false;
#endif
#endif
''')
    marker = '                    if ( hero->isMoveEnabled() ) {\n'
    assert source.count(marker) == 1
    source = source.replace(marker, marker + '''#ifdef HMM2_PACKED
                        // Codex: Move может изменить клетку, туман, маршрут и объекты.
                        // После него обязательна исходная полная запись карты.
                        hmm_animation_only=false;
#endif
''')
    marker = '        const HmmMapAnimation hmm_animation(hmm_animation_only);'
    assert source.count(marker) == 1
    source = source.replace(marker, '''#ifdef HMM2_HOST
        // Codex: полная исходная запись также проверяет прокрутку, а не только героя.
        if(std::getenv("HMM2_NO_WALK_STATIC"))hmm_animation_only=false;
#endif
'''+marker)
    return source
