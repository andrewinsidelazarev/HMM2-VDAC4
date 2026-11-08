"""Codex: пространственное отбрасывание клеток без изменения исходной проверки треугольников."""

def patch(name,source):
    if name!='battle_board.cpp':return source
    needle='int32_t Battle::Board::GetIndexAbsPosition( const fheroes2::Point & pt ) const\n{'
    assert source.count(needle)==1
    return source.replace(needle,needle+'''
#ifdef HMM2_PACKED
    // Codex: SetArea задаёт равные клетки по строкам. В строке точку могут
    // содержать только два соседних столбца, включая общую границу.
    // Порядок строк/столбцов и окончательный тест треугольников прежние.
    if ( size() == sizeInCells && front().GetPos().width == Cell::widthPx ) {
        for ( int row = 0; row < heightInCells; ++row ) {
            const int begin = row * widthInCells;
            const auto & rect = ( *this )[begin].GetPos();
            if ( pt.y < rect.y || pt.y > rect.y + rect.height || pt.x < rect.x )
                continue;
            const int col = ( pt.x - rect.x ) / Cell::widthPx;
            for ( int x = std::max( 0, col - 1 ); x <= std::min( widthInCells - 1, col ); ++x ) {
                const auto & cell = ( *this )[begin + x];
                if ( cell.isPositionIncludePoint( pt ) )
                    return cell.GetIndex();
            }
        }
        return -1;
    }
#endif
''',1)
