"""Codex: ускорение неизменяемых справочников и временных очередей без изменения правил рендера."""
def patch(name,s):
    # Клод: архив теперь читается один раз, без предварительной проверки, поэтому в повреждённом файле мусор может дойти до чтения мира и запросить огромный вектор.
    # Это та же ошибка файла: сообщение и возврат в меню, как и раньше. Проверено на сохранении с порчей посередине.
    if name=='game_io.cpp':
        marker='    dataStream >> World::Get() >> conf >> GameOver::Result::Get();'
        assert s.count(marker)==1
        s=s.replace(marker,'#ifdef HMM2_PACKED\n    try {\n'+marker+'\n    }\n    catch ( const std::bad_alloc & ) {\n        showGenericErrorMessage();\n        return fheroes2::GameMode::CANCEL;\n    }\n#else\n'+marker+'\n#endif')
        s='#include <new>\n'+s
    # Клод: пока открыт экран города, большие фоны держатся раскодированными (hmm_resource_town_cache): окно найма
    # рисует край узором и ~20 раз растеризует полосы экрана, а каждый раз строки фонов читались с карты (8 МиБ на открытие).
    if name=='castle_dialog.cpp':
        marker='                                                    const bool renderBackgroundDialog )\n{\n'
        assert s.count(marker)==1 and 'Castle::OpenDialog' in s
        s=s.replace(marker,marker+'#ifdef HMM2_PACKED\n    const HmmTownCache hmmTownCache;\n#endif\n')
        s='#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif\n'+s
    # Клод: в HMM2.DAT только английский: разбор .mo и окно выбора языка не исполняются, а занимают ELF и PSRAM. Место нужно под ускорение горячего кода.
    if name=='settings.cpp':
        marker="    // First, let's see if the translation for the requested language is already cached\n"
        assert s.count(marker)==1
        s=s.replace(marker,'#ifdef HMM2_PACKED\n    Translation::reset();\n    return true;\n#endif\n'+marker)
    if name=='dialog_language_selection.cpp':
        marker="    SupportedLanguage selectLanguage( const std::vector<SupportedLanguage> & languages, const SupportedLanguage currentLanguage, const bool isGameLanguage )\n    {\n"
        assert s.count(marker)==1
        s=s.replace(marker,marker+'#ifdef HMM2_PACKED\n        return currentLanguage;\n#endif\n')
    # Клод: очистка таблицы поиска пути: при -Oz присваивание node = {} вызывало memcpy на каждую клетку,
    # 12 % всех команд трассы хода ИИ (карта 108×108, 21 проход). Нулевые байты дают те же пустые optional; _from остаётся -1.
    if name=='world_pathfinding.cpp':
        marker='    for ( WorldNode & node : _cache ) {\n        node = {};\n    }\n'
        assert s.count(marker)==2
        s=s.replace(marker,'#ifdef HMM2_PACKED\n    std::memset( static_cast<void *>( _cache.data() ), 0, _cache.size() * sizeof( WorldNode ) );\n    for ( WorldNode & node : _cache ) {\n        node._from = -1;\n    }\n#else\n'+marker+'#endif\n')
        s='#include <cstring>\n'+s
    # Клод: пустой слот (UNKNOWN) бонусов не даёт и пропускается сразу: у героя заняты обычно единицы из 14 мест. Сумма бонусов строила std::set на 14 мест: узел кучи на каждый вызов атаки, защиты, морали и удачи героя (ход ИИ считает их сотни раз за Army::GetStrength).
    if name=='artifact.cpp':
        assert s.count('std::set<int> usedArtifactIds;')==6 and s.count('usedArtifactIds.insert( artifactId ).second')==6
        s=s.replace('std::set<int> usedArtifactIds;','HmmIdSet usedArtifactIds;').replace('!usedArtifactIds.insert( artifactId ).second','artifactId == Artifact::UNKNOWN || !usedArtifactIds.insert( artifactId )')
        marker='BagArtifacts::BagArtifacts()'
        assert s.count(marker)==1
        s=s.replace(marker,'namespace\n{\n    struct HmmIdSet\n    {\n        int ids[16];\n        unsigned count = 0;\n        bool insert( const int id )\n        {\n            for ( unsigned i = 0; i < count; ++i ) {\n                if ( ids[i] == id ) {\n                    return false;\n                }\n            }\n            if ( count < 16 ) {\n                ids[count++] = id;\n            }\n            return true;\n        }\n    };\n}\n\n'+marker)
    # Клод: тень движения в бою спрашивает путь для всех 99 клеток, и каждый запрос заново сравнивает проходимость 99 клеток: около 10 000 проверок на кадр (8 % ядра игры).
    # Внутри этого цикла доска не меняется (идут только Blit), поэтому проверка делается один раз на цикл. Флаг снимается и при исключении.
    if name=='battle_pathfinding.cpp':
        marker='    void BattlePathfinder::reEvaluateIfNeeded( const Unit & unit )\n    {\n'
        assert s.count(marker)==1
        s=s.replace(marker,'    bool hmmPathFreeze = false;\n    const Unit * hmmPathUnit = nullptr;\n\n'+marker+'        if ( hmmPathFreeze ) {\n            if ( hmmPathUnit == &unit ) {\n                return;\n            }\n            hmmPathUnit = &unit;\n        }\n\n')
    if name=='battle_interface.cpp':
        marker="""        for ( int32_t i = 0; i < w / 2; i += 2 ) {
            for ( int32_t x = 0; x < rt.width; ++x ) {
                for ( int32_t y = 0; y < rt.height; ++y ) {
                    fheroes2::SetTransformPixel( sf, rt.x + x, rt.y + y, alphaValue );
                }
            }
            --rt.y;"""
        assert s.count(marker)==1
        s=s.replace(marker,"""        fheroes2::Rect hmmRects[32];
        size_t hmmRectCount = 0;
        for ( int32_t i = 0; i < w / 2; i += 2 ) {
            hmmRects[hmmRectCount++] = rt;
            --rt.y;""")
        marker="""            rt.width -= ( i == 0 ) ? 2 : 4;
        }

        return sf;"""
        assert s.count(marker)==1
        s=s.replace(marker,"""            rt.width -= ( i == 0 ) ? 2 : 4;
        }
        fheroes2::SetTransformRects( sf, hmmRects, hmmRectCount, alphaValue );

        return sf;""")
        marker='namespace\n{\n'
        assert s.count(marker)>=1
        s=s.replace(marker,'namespace Battle\n{\n    extern bool hmmPathFreeze;\n    extern const Unit * hmmPathUnit;\n}\n\nnamespace\n{\n    struct HmmPathFreezeScope\n    {\n        HmmPathFreezeScope()\n        {\n            Battle::hmmPathFreeze = true;\n            Battle::hmmPathUnit = nullptr;\n        }\n        ~HmmPathFreezeScope()\n        {\n            Battle::hmmPathFreeze = false;\n        }\n    };\n}\n\n'+marker,1)
        marker='    const fheroes2::Image & shadowImage = conf.BattleShowGrid() ? _hexagonGridShadow : _hexagonShadow;\n'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'    const HmmPathFreezeScope hmmPathFreezeScope;\n')
        # Клод: контур отряда (выделенного и того, чей ход) — слоями FT812 (hmm_gpu_contour, hmm_canvas.inc) вместо CreateContour на CPU
        # каждый кадр. Когда нарисован не сам спрайт ресурса (камень, зеркальный образ, своя альфа, ров замка) — прежним путём.
        marker='namespace Battle\n{\n    extern bool hmmPathFreeze;'
        assert s.count(marker)==1
        s=s.replace(marker,'#ifdef HMM2_PACKED\nbool hmm_gpu_contour( const fheroes2::Image &, fheroes2::Image &, int, int, bool, uint8_t );\n#endif\n\n'+marker,1)
        for color in ['GetArmyColorFromPlayerColor( unit.GetArmyColor() )','_contourColor']:
            marker=f'''        const fheroes2::Sprite & monsterContour = fheroes2::CreateContour( monsterSprite, {color} );
        fheroes2::Blit( monsterContour, _mainSurface, drawnPosition.x, drawnPosition.y, unit.isReflect() );
'''
            assert s.count(marker)==1
            s=s.replace(marker,f'''#ifdef HMM2_PACKED
        if ( unit.Modes( SP_STONE | CAP_MIRRORIMAGE ) || unit.GetCustomAlpha() != 255 || Arena::GetCastle() != nullptr
             || !hmm_gpu_contour( monsterSprite, _mainSurface, drawnPosition.x, drawnPosition.y, unit.isReflect(), {color} ) )
#endif
        {{
            const fheroes2::Sprite & monsterContour = fheroes2::CreateContour( monsterSprite, {color} );
            fheroes2::Blit( monsterContour, _mainSurface, drawnPosition.x, drawnPosition.y, unit.isReflect() );
        }}
''')
    # Клод: поля, которые поиск пути и карта читают постоянно, — подряд в начале Tile: одно чтение строки кэша PSRAM (32 байта) вместо двух-трёх.
    # Промах кэша — основное время хода ИИ на плате. Формат сохранений не меняется: потоковые операторы пишут поля по именам.
    if name=='maps_tiles.h':
        import re
        begin=s.index('        ObjectPart _mainObjectPart;\n');end='        uint32_t _region{ REGION_NODE_BLOCKED };\n';stop=s.index(end)+len(end)
        lists=''.join(re.findall(r'[^\n]*_(?:ground|top)ObjectPart;\n',s[begin:stop]));assert lists.count('\n')==2
        s=s[:begin]+'''        uint16_t _tilePassabilityDirections{ DIRECTION_ALL };
        uint16_t _fogDirection{ DIRECTION_ALL };
        uint8_t _terrainFlags{ 0 };
        bool _isTileMarkedAsRoad{ false };
        uint8_t _occupantHeroId{ Heroes::UNKNOWN };
        PlayerColorsSet _fogColors{ Color::allPlayerColors() };
        PlayerColor _boatOwnerColor{ PlayerColor::NONE };
        MP2::MapObjectType _mainObjectType{ MP2::OBJ_NONE };
        uint16_t _terrainImageIndex{ 0 };
        int32_t _index{ 0 };
        uint32_t _region{ REGION_NODE_BLOCKED };
        ObjectPart _mainObjectPart;
        std::array<uint32_t, 3> _metadata{ 0 };
'''+lists+s[stop:]
    # Клод: тень клетки боя строилась точками: 25 тысяч команд на точку (растеризация, снимок, cover, выделение памяти на каждую), пять картинок по тысячам точек при каждом входе в бой.
    # По трассе 91 % команд входа в бой; это счёт на ядре игры без чтений и кадров, как 30-секундное зависание боя на плате. Теперь все прямоугольники пишутся одной операцией, результат те же точки.
    if name=='image.cpp':
        marker='    void SetTransformPixel( Image & image, const int32_t x, const int32_t y, const uint8_t value )\n    {\n'
        assert s.count(marker)==1
        s=s.replace(marker,"""    void SetTransformRects( Image & image, const Rect * rects, const size_t count, const uint8_t value )
    {
        const HmmImagePins hmm_pins;
#ifdef HMM2_PACKED
        hmm_gpu_touch(image,0,0,image.width(),image.height());
#endif
        if ( image.empty() || image.singleLayer() ) {
            return;
        }
        uint8_t * pixels = image.image();
        uint8_t * transform = image.transform();
        const int32_t width = image.width();
        const int32_t height = image.height();
        for ( size_t i = 0; i < count; ++i ) {
            const int32_t left = std::max<int32_t>( rects[i].x, 0 );
            const int32_t right = std::min<int32_t>( rects[i].x + rects[i].width, width );
            const int32_t top = std::max<int32_t>( rects[i].y, 0 );
            const int32_t bottom = std::min<int32_t>( rects[i].y + rects[i].height, height );
            if ( left >= right ) {
                continue;
            }
            for ( int32_t y = top; y < bottom; ++y ) {
                std::memset( pixels + y * width + left, 0, right - left );
                std::memset( transform + y * width + left, value, right - left );
            }
        }
    }

"""+marker)
    if name=='image.h':
        marker='    void SetTransformPixel( Image & image, const int32_t x, const int32_t y, const uint8_t value );\n'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'    void SetTransformRects( Image & image, const Rect * rects, const size_t count, const uint8_t value );\n')
    # Клод: та же тень в окне настроек боя, строилась по точкам при каждом открытии окна.
    if name=='battle_interface_settings.cpp':
        marker="""        for ( int32_t i = 0; i < w / 2; i += 2 ) {
            for ( int32_t x = 0; x < rt.width; ++x ) {
                for ( int32_t y = 0; y < rt.height; ++y ) {
                    fheroes2::SetTransformPixel( sf, rt.x + x, rt.y + y, 2 );
                }
            }
            --rt.y;"""
        assert s.count(marker)==1
        s=s.replace(marker,"""        fheroes2::Rect hmmRects[32];
        size_t hmmRectCount = 0;
        for ( int32_t i = 0; i < w / 2; i += 2 ) {
            hmmRects[hmmRectCount++] = rt;
            --rt.y;""")
        marker="""            rt.width -= ( i == 0 ) ? 2 : 4;
        }

        return sf;"""
        assert s.count(marker)==1
        s=s.replace(marker,"""            rt.width -= ( i == 0 ) ? 2 : 4;
        }
        fheroes2::SetTransformRects( sf, hmmRects, hmmRectCount, 2 );

        return sf;""")
    if name=='battle_cell.cpp':
        marker="    const fheroes2::Point pt( infl * dst.x, infl * dst.y );\n"
        assert s.count(marker)==1
        s=s.replace(marker,marker+'''#ifdef HMM2_PACKED
    // Клод: поиск клетки под курсором перебирает всю доску по 8 треугольников на клетку: 48,6 % команд ядра игры в бою (трасса сборки 67).
    // Все треугольники лежат в описанном прямоугольнике шестиугольника, вне него исходный код тоже вернёт UNKNOWN.
    if ( pt.y < _coord[2].y || pt.y > _coord[5].y || pt.x < _coord[1].x || pt.x > _coord[3].x ) {
        return AttackDirection::UNKNOWN;
    }
#endif
''')
    if name=='map_object_info.cpp':
        marker='        auto iter = objectInfoByIcn.find( std::make_pair( icnType, icnIndex ) );'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
        // Codex: populateObjectData заполняет неизменяемый справочник один раз.
        // Полный ключ проверяется: коллизия вызывает исходный поиск, чужого результата нет.
        struct CachedPart { uint32_t type=0,index=0; const ObjectPartInfo *value=nullptr; };
        static CachedPart cache[256];
        auto &cached=cache[((uint32_t(icnType)*2654435761u)^(icnIndex*2246822519u))>>24];
        if(cached.value&&cached.type==uint32_t(icnType)&&cached.index==icnIndex)return cached.value;
#endif
'''+marker)
        marker='''        if ( iter != objectInfoByIcn.end() ) {
            return iter->second;'''
        assert s.count(marker)==1
        s=s.replace(marker,'''        if ( iter != objectInfoByIcn.end() ) {
#ifdef HMM2_PACKED
            cached={uint32_t(icnType),icnIndex,iter->second};
#endif
            return iter->second;''')
    if name=='image.cpp':
        marker='        const Rect relativeROI( spriteRelativeOffset.x, spriteRelativeOffset.y, original.width(), original.height() );'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
        // Codex: число фрагментов уже вычислено исходным алгоритмом; одна аллокация вместо роста1/2/4/8.
        const size_t hmm_parts=static_cast<size_t>(stepPerDirection.x)*static_cast<size_t>(stepPerDirection.y);
        outputSquareId.reserve(outputSquareId.size()+hmm_parts);
        outputImageInfo.reserve(outputImageInfo.size()+hmm_parts);
#endif
'''+marker,1)
        # Клод: узор края окна берёт пиксели рамки (WINLOSE 319×456, два слоя — 290 КиБ). in.image() раскодировал её целиком в один блок,
        # которого в раздробленной PSRAM города нет: отказ malloc отдавал все кэши, и окно найма каждый раз читало фоны с карты.
        # Теперь та же полоса источника (width×height от inX,inY) — через hmm_resource_region, и на ней исходный алгоритм без изменений.
        a=s.index('    void CreateDitheringTransition( const Image & in, int32_t inX, int32_t inY, Image & out,')
        marker='        const int32_t widthIn = in.width();\n'
        b=s.index(marker,a)
        s=s[:b]+'''#ifdef HMM2_PACKED
        {
            uint32_t hmmKey;Image hmmPart;
            if(hmm_resource_id(in,hmmKey)&&hmm_resource_region(hmmKey,inX,inY,width,height,hmmPart)){
                CreateDitheringTransition(hmmPart,0,0,out,outX,outY,width,height,isVertical,isReverse);
                return;
            }
        }
#endif
'''+s[b:]
    if name=='interface_radar.h':
        marker='        bool _hide{ true };'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'''\n#ifdef HMM2_PACKED
        uint32_t _hmm_stamp=0;bool _hmm_map_shown=false;
#endif''',1)
    if name=='interface_radar.cpp':
        s='#ifdef HMM2_PACKED\n#include "hmm_resources.h"\n#endif\n'+s
        for marker in ['void Interface::Radar::Build()\n{','void Interface::Radar::RedrawObjects( const PlayerColorsSet playerColor, const ViewWorldMode flags )\n{']:
            assert s.count(marker)==1
            s=s.replace(marker,marker+'''\n#ifdef HMM2_PACKED
    _hmm_map_shown=false;
#endif''',1)
        begin=s.index('void Interface::Radar::_redraw(');end=s.index('\nvoid Interface::Radar::RedrawForViewWorld(',begin);part=s[begin:end]
        part=part.replace('            return;', '''#ifdef HMM2_PACKED
            _hmm_map_shown=false;
#endif
            return;''',1)
        marker='    const fheroes2::Rect & rect = GetArea();'
        assert part.count(marker)==1
        part=part.replace(marker,marker+'''
#ifdef HMM2_PACKED
    // Codex: только рамка меняется; запись в область радара отменяет этот быстрый путь.
    const auto hmm_stamp=hmm_gpu_region_begin(this,display,rect.x,rect.y,rect.width,rect.height);
    if(!redrawMapObjects&&!conf.isHideInterfaceEnabled()&&!_hide&&_hmm_map_shown&&_hmm_stamp==hmm_stamp){
        RedrawCursor();_hmm_stamp=hmm_gpu_region_end(true);return;
    }
#endif''',1)
        part=part.replace('    if ( _hide ) {','''    if ( _hide ) {
#ifdef HMM2_PACKED
        _hmm_map_shown=false;
#endif''',1)
        marker='        _cursorArea.show();\n        RedrawCursor();'
        assert part.count(marker)==1
        part=part.replace(marker,marker+'''
#ifdef HMM2_PACKED
        _hmm_map_shown=true;_hmm_stamp=hmm_gpu_region_end(false);
#endif''',1)
        s=s[:begin]+part+s[end:]
        marker='        if ( _cursorArea.width() != cursorSize.width || _cursorArea.height() != cursorSize.height ) {'
        assert s.count(marker)==1
        s=s.replace(marker,"""#ifdef HMM2_PACKED
        // Codex: при прежних размере и позиции рамка уже показана; ImageRestorer не нужен.
        const bool hmm_resized=_cursorArea.width()!=cursorSize.width||_cursorArea.height()!=cursorSize.height;
        if(hmm_resized){
#else
"""+marker+"\n#endif",1)
        marker='        _cursorArea.setPosition( totalRenderingArea.x + ( radarWorldArea.x * actualRenderingArea.width ) / worldSize.width,'
        assert s.count(marker)==1
        s=s.replace(marker,"""#ifdef HMM2_PACKED
        const int32_t hmm_x=totalRenderingArea.x+(radarWorldArea.x*actualRenderingArea.width)/worldSize.width;
        const int32_t hmm_y=totalRenderingArea.y+(radarWorldArea.y*actualRenderingArea.height)/worldSize.height;
        if(hmm_resized||_cursorArea.x()!=hmm_x||_cursorArea.y()!=hmm_y)
#endif
"""+marker,1)
    if name=='interface_gamearea.cpp':
        s=s.replace('std::deque<fheroes2::ObjectRenderingInfo>','HmmRenderQueue')
        marker='    struct TileUnfitRenderObjectInfo'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
    // Codex: та же очередь с теми же emplace_front/back и порядком обхода.
    // Два обычных элемента хранятся в узле; длинная очередь растёт без ограничения.
    class HmmRenderQueue {
        using Info=fheroes2::ObjectRenderingInfo;
        Info local[2];std::vector<Info> overflow;unsigned count=0;
        void append(const Info &info,bool front){
            if(count<2){if(front&&count)local[1]=local[0];local[front?0:count]=info;}
            else {if(count==2)overflow.assign(local,local+2);
                if(front)overflow.insert(overflow.begin(),info);else overflow.push_back(info);}
            ++count;
        }
    public:
        void emplace_front(const Info &info){append(info,true);}
        void emplace_back(const Info &info){append(info,false);}
        const Info *begin()const{return count>2?overflow.data():local;}
        const Info *end()const{return begin()+count;}
    };
#else
    using HmmRenderQueue=std::deque<fheroes2::ObjectRenderingInfo>;
#endif
'''+marker)
    return patch_static(name,s)

# Codex: оборачивается исходный участок; вычисление кадров анимации остаётся снаружи.
def static_block(s,start,end,after=''):
    a=s.index(start,s.index(after) if after else 0);b=s.index(end,a)
    return s[:a]+"#ifdef HMM2_PACKED\n    {const HmmMapStatic hmm_static;if(!hmm_static.reused()){\n#endif\n"+s[a:b]+"#ifdef HMM2_PACKED\n    }}\n#endif\n"+s[b:]

# Codex: неизменная строка пропускается целиком; любой динамический потомок помечает её flags.
def static_row(s,after,before):
    a=s.index(after);b=s.index(before,a);a=s.index('    for ( int32_t y =',a,b);a=s.index('\n',s.index('{',a,b))+1
    return s[:a]+"#ifdef HMM2_PACKED\n        const HmmMapStatic hmm_row;\n        if(hmm_row.reused())continue;\n#endif\n"+s[a:]

def patch_static(name,s):
    if name=='interface_gamearea.cpp':
        s=s.replace('_topLeftTileOffset.y );\n#endif','_topLeftTileOffset.y, this, flag, _animationInfo.empty(), _getStartTileId().x, _getStartTileId().y );\n#endif',1)
        s=s.replace('src,overlappedRoi.x-imageRoi.x,overlappedRoi.y-imageRoi.y,dst,overlappedRoi.x,overlappedRoi.y,overlappedRoi.width,overlappedRoi.height',
                    'src,0,0,dst,imageRoi.x,imageRoi.y,src.width(),src.height()')
        s=s.replace('src,srcRoi.x+overlappedRoi.x-imageRoi.x,srcRoi.y+overlappedRoi.y-imageRoi.y,dst,overlappedRoi.x,overlappedRoi.y,overlappedRoi.width,overlappedRoi.height',
                    'src,srcRoi.x,srcRoi.y,dst,imageRoi.x,imageRoi.y,srcRoi.width,srcRoi.height')
        s=static_block(s,'    // Render terrain.','    const int32_t minX =')
        # Codex: целый статический проход сохраняется; при наличии анимаций исполняется
        # исходный цикл, а его статические дочерние участки остаются независимыми.
        s=static_block(s,'    // Render all terrain and background layer object.','    // Draw the lower part of tile-unfit')
        s=static_block(s,'    for ( int32_t y = minY; y < maxY; ++y ) {','    // Draw all shadows from tile-unfit objects.',
                       'renderImagesOnTiles( dst, tileUnfit.bottomBackgroundImages, *this );')
        s=static_block(s,'    for ( int32_t y = minY; y < maxY; ++y ) {','    // Draw middle part of tile-unfit sprites.',
                       'renderImagesOnTiles( dst, tileUnfit.lowPriorityBottomImages, *this );')
        s=static_block(s,'    // Draw hero\'s route.','    bool drawPassabilities =')
        s=static_block(s,'        const bool drawTowns =','    }\n\n    updateObjectAnimationInfo();')
        s=static_row(s,'    // Render all terrain and background layer object.','    // Draw the lower part of tile-unfit')
        s=static_row(s,'renderImagesOnTiles( dst, tileUnfit.bottomBackgroundImages, *this );','    // Draw all shadows from tile-unfit objects.')
        s=static_row(s,'renderImagesOnTiles( dst, tileUnfit.lowPriorityBottomImages, *this );','    // Draw middle part of tile-unfit sprites.')
    if name=='maps_tiles_render.cpp':
        s=static_block(s,'        const fheroes2::Sprite & sprite =','        const auto * objectInfo =')
        s=static_block(s,'        const fheroes2::Sprite & mainObjectSprite =','        const auto * objectInfo =')
        # Клод: при нехватке RAM_G — анимация только рядом с героем и вода (как в порте HMM2 для VDAC2, просьба пользователя 02.11).
        # Плотную карту (Alteris: десятки монстров) GPU собирает из клеток 32×32, растеризованных процессором, и у клетки с
        # анимированным объектом свой вариант на каждый кадр анимации: варианты не помещались в RAM_G, клетки растеризовались
        # заново каждый тик со спрайтами с SD, и карта стояла даже в простое. Тогда (hmm_gpu_animation_limited) объекты дальше
        # трёх клеток от героя (или замка) в фокусе стоят в позе первого кадра; вода анимируется вся. Иначе — как в оригинале.
        # Клод: 03.11 — и в простое тоже (пользователь: «после загрузки игра снова тормозит даже в простое»): на плате (сборка 86, без
        # ограничения) плотная карта перепекала ~47 клеток за кадр, сборка кадра 250–335 мс. Центр — герой в фокусе, иначе замок в фокусе.
        marker='#include "world.h"\n'
        assert s.count(marker)==1
        s=s.replace(marker,marker+'#ifdef HMM2_PACKED\n#include "castle.h"\n#include "game_interface.h"\n#endif\n')
        marker='    void renderObjectPart( fheroes2::Image & output, const Interface::GameArea & area, const fheroes2::Point & offset, const Maps::ObjectPart & part )\n'
        assert s.count(marker)==1
        s=s.replace(marker,'''#ifdef HMM2_PACKED
    // Клод: индекс анимации карты для клетки mp и ICN объекта (-1 — монстр); см. patch_fast_frame.py.
    uint32_t hmmAnimationIndex( const fheroes2::Point & mp, const int icn )
    {
        const uint32_t index = Game::getAdventureMapAnimationIndex();
        if ( !hmm_gpu_animation_limited() || icn == ICN::OBJNWATR || icn == ICN::OBJNWAT2 ) {
            return index;
        }
        fheroes2::Point center;
        if ( const Heroes * hero = Interface::GetFocusHeroes() ) {
            center = hero->GetCenter();
        }
        else if ( const Castle * castle = Interface::GetFocusCastle() ) {
            center = castle->GetCenter();
        }
        else {
            return 0;
        }
        return std::abs( mp.x - center.x ) <= 3 && std::abs( mp.y - center.y ) <= 3 ? index : 0;
    }
#define HMM2_MAP_ANIMATION_INDEX( mp, icn ) hmmAnimationIndex( mp, icn )
#else
#define HMM2_MAP_ANIMATION_INDEX( mp, icn ) Game::getAdventureMapAnimationIndex()
#endif

'''+marker)
        for old_expr,new_expr in [('const uint32_t secondaryFrameIndex = part.icnIndex + ( Game::getAdventureMapAnimationIndex() % objectInfo->animationFrames ) + 1;',
                                   'const uint32_t secondaryFrameIndex = part.icnIndex + ( HMM2_MAP_ANIMATION_INDEX( offset, icn ) % objectInfo->animationFrames ) + 1;'),
                                  ('uint32_t secondaryFrameIndex = part.icnIndex + ( Game::getAdventureMapAnimationIndex() % objectInfo->animationFrames ) + 1;',
                                   'uint32_t secondaryFrameIndex = part.icnIndex + ( HMM2_MAP_ANIMATION_INDEX( Maps::GetPoint( tile.GetIndex() ), mainObjectIcn ) % objectInfo->animationFrames ) + 1;'),
                                  ('monsterAnimationSequence[( Game::getAdventureMapAnimationIndex() + mp.x * mp.y ) % monsterAnimationSequence.size()]',
                                   'monsterAnimationSequence[( HMM2_MAP_ANIMATION_INDEX( mp, -1 ) + mp.x * mp.y ) % monsterAnimationSequence.size()]')]:
            assert s.count(old_expr)==1,old_expr
            s=s.replace(old_expr,new_expr)
    if name=='game_startgame.cpp':
        marker='        // Scrolling the game area\n'
        assert s.count(marker)==1
        s=s.replace(marker,"""#ifdef HMM2_PACKED
        bool hmm_animation_only=false;
#endif
"""+marker)
        marker="""        if ( Game::validateAnimationDelay( Game::DelayType::MAPS_DELAY ) ) {
            Game::updateAdventureMapAnimationIndex();

            _gameArea.SetRedraw();
        }
"""
        assert s.count(marker)==1
        s=s.replace(marker,marker.replace('            Game::updateAdventureMapAnimationIndex();',"""#ifdef HMM2_PACKED
            hmm_animation_only=(getRedrawMask()&REDRAW_GAMEAREA)==0;
#endif
            Game::updateAdventureMapAnimationIndex();""")+"""#ifdef HMM2_PACKED
        const HmmMapAnimation hmm_animation(hmm_animation_only);
#endif
""")
        for marker in ['                _gameArea.Scroll();','            else if ( _gameArea.needDragScrollRedraw() || _gameArea.updateInertia() ) {']:
            assert s.count(marker)==1
            code="\n#ifdef HMM2_PACKED\n                hmm_animation_only=(getRedrawMask()&REDRAW_GAMEAREA)==0;\n#endif\n"
            if marker.startswith('            else'):s=s.replace(marker,marker+code)
            else:s=s.replace(marker,code+marker)
        s=s.replace('hmm_animation_only=(getRedrawMask()&REDRAW_GAMEAREA)==0;\n#endif\n            Game::updateAdventureMapAnimationIndex();',
                    'hmm_animation_only=hmm_animation_only||(getRedrawMask()&REDRAW_GAMEAREA)==0;\n#endif\n            Game::updateAdventureMapAnimationIndex();')
    return s
