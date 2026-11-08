"""Avoid per-pixel stable_sort and software-double distance work, exactly."""

def patch(name,s):
    if name!='game.cpp':return s
    marker='void Game::EnvironmentSoundMixer()'
    assert s.count(marker)==1
    s=s.replace(marker,'#ifdef HMM2_PACKED\n#include "hmm_env_audio.inc"\n#endif\n'+marker,1)
    start=s.index('    std::vector<fheroes2::Point> positions;',s.index(marker))
    end=s.index('    const double maxDistance',start)
    s=s[:start]+'#ifndef HMM2_PACKED\n'+s[start:end]+'#endif\n'+s[end:]
    marker='    for ( const fheroes2::Point & pos : positions ) {'
    assert s.count(marker)==1
    s=s.replace(marker,'''#ifdef HMM2_PACKED
    static_assert(fheroes2::tileWidthPx==32);
    for(const auto &offset:hmm_env_positions){
        const fheroes2::Point pos(offset[0],offset[1]);
        if(pos.x < -scanningOffset||pos.x > scanningOffset||pos.y < -scanningOffset||pos.y > scanningOffset||
           !Maps::isValidAbsPoint(pos.x+center.x,pos.y+center.y))continue;
#else
'''+marker+'\n#endif',1)
    start=s.index('        const double dblDistance',s.index('void Game::EnvironmentSoundMixer()'))
    end=s.index('        int16_t angle = 0;',start)
    s=s[:start]+'''#ifdef HMM2_PACKED
        const unsigned squared=unsigned(actualPosition.x*actualPosition.x+actualPosition.y*actualPosition.y);
        if(squared>=18432)continue;
        const uint8_t distance=hmm_env_distance(squared,maxDistance);
#else
'''+s[start:end]+'#endif\n\n'+s[end:]
    return s
