"""Codex: отдельная инструментированная сборка; базовые результаты этапа 0 не перезаписываем."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'tests/build_profile.py').read_text(encoding='utf-8')
text=text.replace("OUT = Path('E:/zx/cache/tmp/hmm2_esp/host32')","OUT = Path('E:/zx/cache/tmp/hmm2_esp/stage0b/host32')")
text=text.replace("ROOT/'tests/profile.cpp']","ROOT/'tests/profile.cpp',ROOT/'tests/profile0b.cpp']")
text=text.replace("'/DNDEBUG'","'/DNDEBUG','/DHMM2_STAGE0B'")
marker="        elif src.name=='agg_image.cpp':"
injection='''        elif src.name=='image.cpp':
            import re
            s='#include "profile0b.h"\\n#include "profile.h"\\n'+s
            s,n=re.subn(r'new uint8_t\\[([^\\]]+)\\]',r'profile0b_pixels( \\1 )',s)
            assert n==4
            start=s.index('    void Blit( const Image & in, int32_t inX')
            at=s.index('        const int32_t widthIn = in.width();',start)
            s=s[:at]+'        profile_trace_blit(in,inX,inY,out,outX,outY,width,height,flip,false);\\n'+s[at:]
            start=s.index('    void Copy( const Image & in, int32_t inX')
            at=s.index('        const int32_t widthIn = in.width();',start)
            s=s[:at]+'        profile_trace_blit(in,inX,inY,out,outX,outY,width,height,false,true);\\n'+s[at:]
            start=s.index('    void Fill( Image & image, int32_t x')
            at=s.index('        if ( image.width() == width',start)
            s=s[:at]+'        profile_trace_fill(image,x,y,width,height,colorId);\\n'+s[at:]
            modified=True
'''
text=text.replace(marker,injection+marker+'''
            s='#include "profile0b.h"\\n'+s
            s=replace_once(s,'            return GetScaledICN( icnId, index );','            const auto &v=GetScaledICN(icnId,index); profile0b_sprite(icnId,index,v.image(),v.width(),v.height(),v.singleLayer()); return v;')
            s=replace_once(s,'        return _icnVsSprite[icnId][index];','        const auto &v=_icnVsSprite[icnId][index]; profile0b_sprite(icnId,index,v.image(),v.width(),v.height(),v.singleLayer());\\n        return v;')
            s=replace_once(s,'        return _tilVsImage[tilId][shapeId][index];','        const auto &v=_tilVsImage[tilId][shapeId][index]; profile0b_sprite(-tilId-1,index,v.image(),v.width(),v.height(),v.singleLayer());\\n        return v;')
''')
exec(compile(text,str(ROOT/'tests/build_profile.py'),'exec'),{'__file__':str(ROOT/'tests/build_profile.py'),'__name__':'__main__'})
