"""Codex: исходное разбиение спрайта сразу заполняет конечный список частей."""
import re

HELPER='''
    // Codex: те же клетки DivideImageBySquares без двух временных векторов
    // и последующего копирования геометрии в ObjectRenderingInfo.
    void hmmAppendSquares( std::vector<fheroes2::ObjectRenderingInfo> & output, const fheroes2::Point & spriteOffset,
                           const fheroes2::Image & original, int icnId, uint32_t icnIndex, bool reflect )
    {
        if ( original.empty() )
            return;
        constexpr int32_t squareSize = fheroes2::tileWidthPx;
        fheroes2::Point offset{ spriteOffset.x / squareSize, spriteOffset.y / squareSize };
        if ( spriteOffset.x < 0 && offset.x * squareSize != spriteOffset.x )
            --offset.x;
        if ( spriteOffset.y < 0 && offset.y * squareSize != spriteOffset.y )
            --offset.y;
        const fheroes2::Point relative{ spriteOffset.x - offset.x * squareSize, spriteOffset.y - offset.y * squareSize };
        const fheroes2::Point steps{ ( original.width() + relative.x + squareSize - 1 ) / squareSize,
                                    ( original.height() + relative.y + squareSize - 1 ) / squareSize };
        const fheroes2::Rect area( relative.x, relative.y, original.width(), original.height() );
        output.reserve( output.size() + steps.x * steps.y );
        for ( int32_t y = 0; y < steps.y; ++y ) {
            for ( int32_t x = 0; x < steps.x; ++x ) {
                const fheroes2::Rect cell( x * squareSize, y * squareSize, squareSize, squareSize );
                const fheroes2::Rect hit = area ^ cell;
                output.emplace_back( offset + fheroes2::Point( x, y ), fheroes2::Point( hit.x - cell.x, hit.y - cell.y ),
                                     fheroes2::Rect( hit.x - relative.x, hit.y - relative.y, hit.width, hit.height ),
                                     icnId, icnIndex, reflect, static_cast<uint8_t>( 255 ) );
            }
        }
    }
'''

def patch(name,source):
    if name!='maps_tiles_render.cpp':return source
    pattern=re.compile(r'fheroes2::DivideImageBySquares\( (.*?), (\w+), fheroes2::tileWidthPx, outputSquareInfo, outputImageInfo \);\s*'
                       r'(?:assert\( outputSquareInfo.size\(\) == outputImageInfo.size\(\) \);\s*)?'
                       r'(std::vector<fheroes2::ObjectRenderingInfo> objectInfo;\s*)?'
                       r'for \( size_t i = 0; i < outputSquareInfo.size\(\); \+\+i \) \{\s*'
                       r'objectInfo.emplace_back\( outputSquareInfo\[i\], outputImageInfo\[i\].first, outputImageInfo\[i\].second, icnId, ([^,]+), ([^,]+),\s*'
                       r'static_cast<uint8_t>\( 255 \) \);\s*\}',re.S)
    def replace(m):
        return (m[3] or '')+f'hmmAppendSquares( objectInfo, {m[1]}, {m[2]}, icnId, {m[4]}, {m[5]} );'
    source,count=pattern.subn(replace,source);assert count==12,count
    source=re.sub(r'^[ \t]*std::vector<fheroes2::Point> outputSquareInfo;\n','',source,flags=re.M)
    source=re.sub(r'^[ \t]*std::vector<std::pair<fheroes2::Point, fheroes2::Rect>> outputImageInfo;\n','',source,flags=re.M)
    source=re.sub(r'^[ \t]*(?:outputSquareInfo|outputImageInfo).clear\(\);\n','',source,flags=re.M)
    assert 'outputSquareInfo' not in source and 'outputImageInfo' not in source
    needle='namespace\n{';assert source.count(needle)==1
    return source.replace(needle,needle+HELPER,1)
