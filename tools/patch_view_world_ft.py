"""Packed port: keep a viewport of one overview scale; bounded raster blocks."""

def patch(s):
    start=s.index('    struct CacheForMapWithResources\n')
    end=s.index('    void DrawWorld(',start)
    original=s[start:end]
    replacement='''    struct CacheForMapWithResources
    {
        fheroes2::Image image;
        size_t activeZoom{ totalZoomLevels };
        int32_t originX{}, originY{}, viewX{}, viewY{};
        PlayerColor playerColor{ PlayerColor::NONE };
        ViewWorldMode mode;
        Interface::GameArea & sourceArea;
        CacheForMapWithResources( const ViewWorldMode m, Interface::GameArea & area, const size_t )
            : mode( m ), sourceArea( area ) {}
        bool update( const ViewWorld::ZoomROIs & roi, const fheroes2::Rect & screen )
        {
            const size_t level = static_cast<uint8_t>( roi.getZoomLevel() );
            const int32_t scale = tileSizePerZoomLevel[level];
            const int32_t vx = scale * roi.GetROIinPixels().x / fheroes2::tileWidthPx;
            const int32_t vy = scale * roi.GetROIinPixels().y / fheroes2::tileWidthPx;
            if ( level == activeZoom && vx == viewX && vy == viewY ) return false;
            image.clear();
            activeZoom = level; viewX = vx; viewY = vy;
            originX = std::max<int32_t>( 0, vx ); originY = std::max<int32_t>( 0, vy );
            const int32_t right = std::min( world.w() * scale, vx + screen.width );
            const int32_t bottom = std::min( world.h() * scale, vy + screen.height );
            fheroes2::Image & output = image;
            output._disableTransformLayer();
            output.resize( std::max<int32_t>( 0, right - originX ), std::max<int32_t>( 0, bottom - originY ) );
            if ( output.empty() ) return true;
            // A private GameArea leaves the adventure camera untouched, including
            // when allocation/transport errors unwind this operation.
            Interface::GameArea area = sourceArea;
            constexpr int32_t block = 6, pixels = block * fheroes2::tileWidthPx;
            area.SetAreaPosition( 0, 0, pixels, pixels );
            fheroes2::Image input, reduced;
            input._disableTransformLayer(); input.resize( pixels, pixels );
            reduced._disableTransformLayer(); reduced.resize( block * scale, block * scale );
            int32_t flags = Interface::RedrawLevelType::LEVEL_ALL & ~Interface::RedrawLevelType::LEVEL_ROUTES;
            if ( mode == ViewWorldMode::ViewAll ) flags &= ~Interface::RedrawLevelType::LEVEL_FOG;
            else if ( mode == ViewWorldMode::ViewTowns ) flags |= Interface::RedrawLevelType::LEVEL_TOWNS;
            flags ^= Interface::RedrawLevelType::LEVEL_HEROES;
            for ( int32_t x = originX / scale / block * block; x * scale < right; x += block ) {
                for ( int32_t y = originY / scale / block * block; y * scale < bottom; y += block ) {
                    area.SetCenterInPixels( { x * fheroes2::tileWidthPx + pixels / 2, y * fheroes2::tileWidthPx + pixels / 2 } );
                    area.Redraw( input, flags );
                    fheroes2::Resize( input, reduced );
                    const int32_t px = x * scale - originX, py = y * scale - originY;
                    const int32_t sx = std::max<int32_t>( 0, -px ), sy = std::max<int32_t>( 0, -py );
                    const int32_t dx = std::max<int32_t>( 0, px ), dy = std::max<int32_t>( 0, py );
                    fheroes2::Copy( reduced, sx, sy, output, dx, dy,
                        std::min( reduced.width() - sx, output.width() - dx ),
                        std::min( reduced.height() - sy, output.height() - dy ) );
                }
            }
            return true;
        }
    };

    void DrawObjectsIcons( const PlayerColor, const ViewWorldMode, CacheForMapWithResources & );

'''
    s=s[:start]+'#ifdef HMM2_PACKED\n'+replacement+'#else\n'+original+'#endif\n'+s[end:]
    a=s.index('    void DrawWorld(');z=s.index('    void DrawObjectsIcons( const PlayerColor color',a)
    original=s[a:z]
    replacement='''    void DrawWorld( const ViewWorld::ZoomROIs & ROI, CacheForMapWithResources & cache, const fheroes2::Rect & roiScreen )
    {
        fheroes2::Display & display = fheroes2::Display::instance();
        // Release the previous viewport before building its replacement. The
        // first black frame is still under the existing fade-out transition.
        fheroes2::Fill( display, roiScreen.x, roiScreen.y, roiScreen.width, roiScreen.height, 0 );
        if ( cache.activeZoom == totalZoomLevels ) display.render();
        if ( cache.update( ROI, roiScreen ) && cache.playerColor != PlayerColor::NONE )
            DrawObjectsIcons( cache.playerColor, cache.mode, cache );
        const fheroes2::Image & image = cache.image;
        if ( !image.empty() ) fheroes2::Copy( image, 0, 0, display,
            roiScreen.x + cache.originX - cache.viewX, roiScreen.y + cache.originY - cache.viewY, image.width(), image.height() );
    }

'''
    s=s[:a]+'#ifdef HMM2_PACKED\n'+replacement+'#else\n'+original+'#endif\n'+s[z:]
    # The overview icons use global mini-map coordinates in the original cache.
    a=s.index('    void DrawObjectsIcons( const PlayerColor color');z=s.index('\n}',a)
    body=s[a:z]
    marker='            for ( size_t zoomLevelId = 0; zoomLevelId < cache.getZoomLevels(); ++zoomLevelId ) {\n'
    assert body.count(marker)==3
    body=body.replace(marker,'''#ifdef HMM2_PACKED
            {
                const size_t zoomLevelId = cache.activeZoom;
                fheroes2::Image & output = cache.image;
                const int32_t originX = cache.originX, originY = cache.originY;
#else
'''+marker+'''
                fheroes2::Image & output = cache.cachedImages[zoomLevelId];
                const int32_t originX = 0, originY = 0;
#endif
''')
    body=body.replace('cache.cachedImages[zoomLevelId],', 'output,')
    body=body.replace('posX * tileSize + ( tileSize - sprite.width() ) / 2;', 'posX * tileSize + ( tileSize - sprite.width() ) / 2 - originX;')
    body=body.replace('posY * tileSize + ( tileSize - sprite.height() ) / 2 + 1;', 'posY * tileSize + ( tileSize - sprite.height() ) / 2 + 1 - originY;')
    body=body.replace('posX * tileSize + tileSize / 2;', 'posX * tileSize + tileSize / 2 - originX;')
    body=body.replace('posY * tileSize + tileSize / 2;', 'posY * tileSize + tileSize / 2 - originY;')
    s=s[:a]+body+s[z:]
    marker='        DrawObjectsIcons( color, mode, cache );'
    assert s.count(marker)==1
    s=s.replace(marker,'''#ifdef HMM2_PACKED
        cache.playerColor = color;
#else
'''+marker+'\n#endif')
    return s
