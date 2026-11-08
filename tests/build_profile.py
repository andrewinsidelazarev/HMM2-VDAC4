"""Codex: отдельная 32-битная сборка измерений; оригиналы и копия движка не изменяются."""
from pathlib import Path
import concurrent.futures
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT/'third_party/openhmm2/src'
OUT = Path('E:/zx/cache/tmp/hmm2_esp/host32')
VC = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
KIT = Path('C:/Program Files (x86)/Windows Kits/10')
PKG = Path('E:/zx/OpenHMM2/VisualStudio/packages/sdl2')

def replace_once(s, old, new):
    assert old in s, old
    return s.replace(old,new,1)

def main():
    ap=argparse.ArgumentParser();group=ap.add_mutually_exclusive_group();group.add_argument('--video',action='store_true');group.add_argument('--video-stream',action='store_true');args=ap.parse_args()
    video_only=args.video or args.video_stream
    stream_video=args.video_stream
    executable=OUT/('profile_smk_stream.exe' if stream_video else 'profile_smk.exe' if args.video else 'profile.exe')
    OUT.mkdir(parents=True,exist_ok=True)
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ('ucrt','shared','um','winrt')],
         SRC/'engine', SRC/'thirdparty/libsmacker', PKG/'include',PKG/'include/SDL2',ROOT/'tests']
    inc += sorted(p for p in (SRC/'fheroes2').iterdir() if p.is_dir())
    sources = sorted((SRC/'engine').glob('*.cpp')) + sorted((SRC/'fheroes2').rglob('*.cpp'))
    sources += [SRC/'thirdparty/libsmacker/smacker.c', ROOT/'tests/profile.cpp']
    compiler=VC/'bin/Hostx64/x86/cl.exe'
    flags=['/nologo','/O2','/MTd','/DNDEBUG','/D_ITERATOR_DEBUG_LEVEL=0','/DSDL_MAIN_HANDLED',
           '/D_CRT_SECURE_NO_WARNINGS','/EHsc','/std:c++17','/utf-8','/w','/D_CRT_NONSTDC_NO_WARNINGS']
    flags += ['/I'+str(i) for i in inc]
    def build(src):
        rel = src.relative_to(ROOT).as_posix()
        obj=OUT/(rel.replace('/','_')+'.obj')
        s=src.read_text(encoding='utf-8')
        modified=False
        if src.name=='fheroes2.cpp':
            s=replace_once(s,'#include <cstdint>','#include "profile.h"\n#include <cstdint>')
            s=replace_once(s,'    const bool isPortBridgeOption', '    profile_install();\n    const bool isPortBridgeOption')
            s=replace_once(s,'        if ( conf.isShowIntro() )', '        if (argc > 1 && std::string(argv[1]) == "--vdac4-profile") return profile_run(argc, argv);\n        if ( conf.isShowIntro() )')
            modified=True
        elif src.name=='agg_file.cpp':
            s='#include "profile.h"\n'+s
            s=replace_once(s,'            _stream.seek( fileOffset );','            profile_read(fileName.c_str(), fileSize);\n            _stream.seek( fileOffset );')
            modified=True
        elif src.name=='screen.cpp':
            s='#include "profile.h"\n'+s
            s=replace_once(s,'    void Display::_renderFrame( const Rect & roi ) const\n    {','    void Display::_renderFrame( const Rect & roi ) const\n    {\n        profile_frame(image(), width(), height());')
            modified=True
        elif src.name=='localevent.cpp':
            s='#include "profile.h"\n'+s
            import re
            s,n=re.subn(r'(bool LocalEvent::HandleEvents\([^\n]+\n\{)',r'\1\n    profile_events();',s,count=1)
            assert n==1
            modified=True
        elif src.name=='smk_decoder.cpp' and stream_video:
            # Codex: проверяем штатный дисковый режим libsmacker; алгоритм декодирования не меняется.
            s=replace_once(s,'SMK_MODE_MEMORY','SMK_MODE_DISK')
            modified=True
        elif src.name=='agg_image.cpp':
            s += '''
// Codex: считаем только реально выделенные пиксельные слои кэшей оригинала.
uint64_t profile_cache_bytes() {
    uint64_t total=0;
    auto add=[&](const fheroes2::Image &i){ total+=uint64_t(i.width())*i.height()*(i.singleLayer()?1:2); };
    for(const auto &v:_icnVsSprite) for(const auto &i:v) add(i);
    for(const auto &v:_tilVsImage) for(const auto &w:v) for(const auto &i:w) add(i);
    for(const auto &v:_icnVsScaledSprite) for(const auto &i:v.second) add(i);
    return total;
}
'''
            modified=True
        actual=src
        if modified:
            actual=OUT/('staged_'+src.name)
            actual.write_text(s,encoding='utf-8')
        cmd=[str(compiler),*flags,'/c',str(actual),'/Fo'+str(obj)]
        sig=hashlib.sha256((s+repr(cmd)+(ROOT/'tests/profile.h').read_text(encoding='utf-8')).encode()).hexdigest()
        stamp=obj.with_suffix('.sha256')
        if obj.exists() and stamp.exists() and stamp.read_text()==sig: return rel,obj,True,''
        p=subprocess.run(cmd,cwd=OUT,capture_output=True)
        log=(p.stdout+p.stderr).decode(errors='replace')
        obj.with_suffix('.log').write_text(log,encoding='utf-8')
        if not p.returncode: stamp.write_text(sig)
        return rel,obj,p.returncode==0,log
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results=list(pool.map(build,sources))
    bad=[r for r in results if not r[2]]
    print('Всего:',len(results),'Ошибок:',len(bad),flush=True)
    for r in bad: print(r[0],r[3][-3000:],flush=True)
    if bad: return 1
    libs=[VC/'lib/x86',KIT/'Lib/10.0.26100.0/ucrt/x86',KIT/'Lib/10.0.26100.0/um/x86',PKG/'lib/x86']
    args=['/NOLOGO','/OUT:'+str(executable),'/SUBSYSTEM:CONSOLE','/MACHINE:X86',
          *['/LIBPATH:'+str(p) for p in libs],*[str(r[1]) for r in results],
          'SDL2.lib','SDL2_mixer.lib','zlib.lib','shell32.lib','user32.lib','advapi32.lib','ole32.lib']
    rsp=OUT/'link.rsp';rsp.write_text(subprocess.list2cmdline(args),encoding='utf-8')
    p=subprocess.run([str(VC/'bin/Hostx64/x86/link.exe'),'@'+str(rsp)],cwd=OUT,capture_output=True)
    (OUT/'link.log').write_bytes(p.stdout+p.stderr)
    print((p.stdout+p.stderr).decode(errors='replace'),flush=True)
    if p.returncode:return p.returncode
    if video_only:
        # Codex: зависимости уже подготовлены основной сборкой; не трогаем файлы текущего прогона.
        print('Готово:',executable,flush=True)
        return 0
    for f in (PKG/'lib/x86').glob('*.dll'):shutil.copy2(f,OUT/f.name)
    shutil.copytree(ROOT/'third_party/openhmm2/files',OUT/'files',dirs_exist_ok=True)
    (OUT/'data').mkdir(exist_ok=True)
    for f in Path('E:/zx/HMM2/Assets/Original/DATA').glob('*.AGG'):shutil.copy2(f,OUT/'data'/f.name)
    print('Готово:',executable,flush=True)
    return 0

if __name__=='__main__': raise SystemExit(main())
