"""Codex: неизменённые исходники канала ядра + настоящая игра в DLL стенда Z80."""
from pathlib import Path
import argparse,json,subprocess,shutil,hashlib
from build_stage1 import ROOT,BASE,FW,VC,KIT,SDK
OUT=BASE/'joint';OUT.mkdir(exist_ok=True)

def main():
    # Codex: для правок загрузчика сохраняем проверенную DLL игры и пересобираем только ESP-стенд.
    ap=argparse.ArgumentParser();ap.add_argument('--transport-only',action='store_true');args=ap.parse_args()
    previous=json.loads((OUT/'build.json').read_text()) if args.transport_only else None
    (OUT/'Arduino.h').write_text('#pragma once\n#include <stdint.h>\nuint32_t millis();\nvoid delay(uint32_t);\nvoid yield();\n',encoding='ascii')
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um','winrt']],OUT,ROOT/'main',SDK,SDK/'SDL2',FW/'include',FW/'modules/sdk/include']
    inc+=[BASE/'src/engine',*[p for p in (BASE/'src/fheroes2').iterdir() if p.is_dir()]]
    objects=[Path(p) for p in json.loads((BASE/'host64_packed/objects.json').read_text()) if not Path(p).name.startswith('tests_')]
    objects.append(BASE/'host64_packed/tests_stage1_scene.cpp.obj')
    objects.append(BASE/'host64_packed/tests_stage1_audio_decoder.c.obj')
    app_sources=[ROOT/'tests/stage1_joint_app.cpp',ROOT/'main/hmm_session.c',ROOT/'main/hmm_failure.cpp',ROOT/'main/hmm_io.c',ROOT/'main/hmm_input.c',ROOT/'main/hmm_video.c']
    sources=[ROOT/'tests/stage1_joint.cpp',*[FW/'src'/s for s in ['spi_link_core.cpp','spsc_ring.cpp','protocol.cpp','z80_transport.cpp']],*app_sources]
    if args.transport_only:sources=[p for p in sources if p not in app_sources]
    # Codex: имя стенда зависит также от объектов движка/GPU, не только оболочки SPI.
    object_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}
    headers=[ROOT/'tests/stage1_joint_app.h',ROOT/'tests/stage1_atomic.h',ROOT/'main/hmm_core.h',ROOT/'patches/slot0_autounload/after/include/zifi/ft_detach_guard.hpp']
    signature=hashlib.sha256(b''.join(p.read_bytes() for p in sources+headers)+json.dumps(object_hashes,sort_keys=True).encode()).hexdigest()
    dllname='joint-'+signature[:12]+'.dll'
    appname='game-'+signature[:12]+'.dll';transport_objects=[]
    if previous:
        appname=previous['app']
        assert hashlib.sha256((OUT/appname).read_bytes()).hexdigest()==previous['app_sha256']
    for source in sources:
        obj=OUT/(source.name+'.obj')
        flags=['/TP','/std:c++17','/EHs'] if source.suffix=='.cpp' else ['/TC','/std:c11']
        if source.suffix=='.c':flags+=['/FI'+str(ROOT/'tests/stage1_atomic.h')]
        if source.name=='hmm_video.c':flags+=['/Dhmm_video_frame=joint_video_frame_actual']
        if source.name=='hmm_input.c':flags+=['/Dhmm_poll_event=joint_poll_event_actual']
        cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DSDL_MAIN_HANDLED',*flags,*['/I'+str(p) for p in inc],'/c',str(source),'/Fo'+str(obj)]
        r=subprocess.run(cmd,capture_output=True,cwd=OUT);obj.with_suffix('.log').write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
        (objects if source in app_sources else transport_objects).append(obj)
    libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']],SDK.parent/'lib/x64']
    for name,objs in ([(dllname,transport_objects)] if previous else [(dllname,transport_objects),(appname,objects)]):
        args=['/NOLOGO','/DLL','/OUT:'+str(OUT/name),'/OPT:REF','/OPT:ICF',*map(str,objs),*['/LIBPATH:'+str(p) for p in libs],'SDL2.lib','zlib.lib','shell32.lib','user32.lib','advapi32.lib','ole32.lib']
        rsp=OUT/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(args),encoding='utf-8')
        r=subprocess.run([str(VC/'bin/Hostx64/x64/link.exe'),'@'+str(rsp)],capture_output=True,cwd=OUT)
        (OUT/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
    for dll in (SDK.parent/'lib/x64').glob('*.dll'):
        target=OUT/dll.name
        if not target.exists() or target.read_bytes()!=dll.read_bytes():shutil.copy2(dll,target)
    (OUT/'build.json').write_text(json.dumps(dict(dll=dllname,app=appname,sha256=hashlib.sha256((OUT/dllname).read_bytes()).hexdigest(),app_sha256=hashlib.sha256((OUT/appname).read_bytes()).hexdigest(),objects=object_hashes,sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+headers},app_reused=bool(previous)),indent=2),encoding='utf-8')
    print(OUT/dllname)
if __name__=='__main__':main()
