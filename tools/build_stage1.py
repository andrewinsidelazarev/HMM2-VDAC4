"""Codex: общие исходники платформы для стенда x86 и ELF Xtensa, без записи в оригиналы."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,re,subprocess,sys,shutil
from compile_probe import ROOT,TC,SDK
from prepare_stage1 import prepare,BASE
from link_probe import FW
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
KIT=Path('C:/Program Files (x86)/Windows Kits/10')
RT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/pic_runtime')

# Клод: весь код собирается с -Oz ради предела ELF. Горячие файлы по трассе QEMU (поиск пути ИИ, клетки карты, рендер) идут с -O2: при -Oz короткие функции
# не встраивались, и цена вызова на регистровых окнах Xtensa была заметной долей хода ИИ.
FAST=r'(world_pathfinding|maps_tiles|maps|mp2|direction|ground|hmm_gpu|battle_pathfinding)\.'
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--host',action='store_true');ap.add_argument('--x64',action='store_true');ap.add_argument('--packed',action='store_true');ap.add_argument('--compile-only',action='store_true');args=ap.parse_args()
    arch='x64' if args.x64 else 'x86'
    # Клод: HMM2_ASAN=1 — стенд ПК с AddressSanitizer MSVC (навык cpp-pro: ошибки памяти ищутся до замеров), в отдельной папке *_asan.
    import os;asan=bool(os.environ.get('HMM2_ASAN')) and args.host
    src=prepare();out=BASE/(('host64' if args.x64 else 'host32' if args.host else 'xtensa')+('_packed' if args.packed else '')+('_asan' if asan else ''));out.mkdir(parents=True,exist_ok=True)
    omit={'audio.cpp','audio_xmi2mid.cpp','smk_decoder.cpp','thread.cpp','timing.cpp','system.cpp','dir.cpp','logging.cpp','core.cpp','battle_port_bridge.cpp','map_random_generator.cpp'}
    sources=sorted((src/'engine').glob('*.cpp'))+sorted((src/'fheroes2').rglob('*.cpp'))
    omit.add('dialog_random_map_generator.cpp')
    if args.packed:omit.add('agg_image.cpp')
    sources=[p for p in sources if p.name not in omit and 'editor' not in p.parts]
    # Codex: CPU-эталону нужны исходный декодер роликов и libsmacker; GPU-вариант использует hmm_clip.
    if args.host and not args.packed:sources += [src/'engine/smk_decoder.cpp',src/'thirdparty/libsmacker/smacker.c']
    sources += [ROOT/'main'/s for s in ['hmm_system.cpp','hmm_muted.cpp','hmm_audio.cpp','hmm_clock.cpp','hmm_logging.cpp']]
    if args.host:sources += [ROOT/'tests/stage1_host.cpp',ROOT/'tests/stage1_pack.cpp',ROOT/'tests/stage1_scene.cpp',ROOT/'tests/stage1_audio_decoder.c',ROOT/'tests/stage1_audio_test.cpp']
    if args.packed:sources += [ROOT/'main/hmm_resources.cpp',ROOT/'main/hmm_gpu.cpp',ROOT/'main/hmm_loading.cpp',ROOT/'main/hmm_clip.cpp',ROOT/'prototype/ft_sprite.c']
    if args.host:sources += [ROOT/'main/hmm_files.c',ROOT/'tests/stage1_files.cpp']
    inc=[ROOT/'main',ROOT/'prototype',ROOT/'tools/probe_include',src/'engine',src/'thirdparty/libsmacker',FW.parent/'Sound/components/stb_vorbis',SDK,SDK/'SDL2',FW/'modules/sdk/include']
    inc+=sorted(p for p in (src/'fheroes2').iterdir() if p.is_dir())
    # Клод: prototype/*.h (ft_sprite.h) тоже входит в подпись: без него hmm_gpu.cpp оставался
    # собранным со старым размером hft_cache, и новый индекс кэша писал за пределы структуры.
    # Codex: Sequence входит также через world_regions.h, поэтому его тело — общая зависимость.
    headers=list(src.rglob('*.h'))+list((ROOT/'main').glob('*.h'))+list((ROOT/'prototype').glob('*.h'))+[ROOT/'main/hmm_sequence.inc']
    hsig=hashlib.sha256(b''.join(p.read_bytes() for p in headers)).hexdigest()
    if args.host:
        inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um','winrt']],*inc]
        compiler=VC/('bin/Hostx64/'+arch+'/cl.exe')
        flags=['/nologo','/O2','/MT','/DNDEBUG','/DHMM2_HOST','/D_ITERATOR_DEBUG_LEVEL=0','/DSDL_MAIN_HANDLED','/D_CRT_SECURE_NO_WARNINGS','/EHs','/std:c++17','/utf-8','/w','/D_CRT_NONSTDC_NO_WARNINGS','/Gy','/Gw',*['/I'+str(p) for p in inc]]
    else:
        compiler=TC/'xtensa-esp32s3-elf-g++.exe'
        flags=['-std=c++17','-Oz','-DNDEBUG','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-mlongcalls','-fno-strict-aliasing','-fpermissive','-flto',*['-I'+str(p) for p in inc]]
    if args.packed:flags+=['/DHMM2_PACKED' if args.host else '-DHMM2_PACKED']
    if asan:flags+=['/fsanitize=address','/Z7']
    def build(p):
        rel=str(p.relative_to(src) if p.is_relative_to(src) else p.relative_to(ROOT)).replace('\\','_').replace('/','_')
        obj=out/(rel+('.obj' if args.host else '.o'));stamp=obj.with_suffix('.sha256')
        # Codex: зависимости арены боя и массива ИИ тоже включают main/*.inc.
        includes=sorted((ROOT/'main').glob('*.inc'));body=p.read_bytes()
        extra=b''.join(f.read_bytes() for f in includes) if p.name=='hmm_gpu.cpp' or any(('"'+f.name+'"').encode() in body for f in includes) else b''
        fast=bool(re.match(FAST,p.name))
        fileflags=[('-O2' if f=='-Oz' and fast else f) for f in flags]
        if args.packed and p.name=='hmm_gpu.cpp':fileflags+=['/GR-' if args.host else '-fno-rtti']
        sig=hashlib.sha256(p.read_bytes()+extra+hsig.encode()+repr(fileflags).encode()).hexdigest()
        if obj.exists() and stamp.exists() and stamp.read_text()==sig:return obj,True,''
        cmd=[str(compiler),*fileflags,*( ['/c',str(p),'/Fo'+str(obj)] if args.host else ['-c',str(p),'-o',str(obj)])]
        r=subprocess.run(cmd,cwd=out,capture_output=True);log=r.stdout+r.stderr;obj.with_suffix('.log').write_bytes(log)
        if not r.returncode:stamp.write_text(sig)
        return obj,not r.returncode,log.decode(errors='replace')
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:results=list(pool.map(build,sources))
    bad=[r for r in results if not r[1]];print('Файлов:',len(results),'ошибок:',len(bad),flush=True)
    for obj,ok,log in bad:print(obj.name,log[-5500:],flush=True)
    if bad:return 1
    (out/'objects.json').write_text(json.dumps([str(r[0]) for r in results]),encoding='utf-8')
    if args.compile_only:return 0
    if args.host:
        libs=[VC/('lib/'+arch),KIT/('Lib/10.0.26100.0/ucrt/'+arch),KIT/('Lib/10.0.26100.0/um/'+arch),SDK.parent/('lib/'+arch)]
        cmd=['/NOLOGO','/OUT:'+str(out/'hmm2_host.exe'),'/MAP:'+str(out/'hmm2_host.map'),'/SUBSYSTEM:CONSOLE','/MACHINE:'+arch.upper(),'/OPT:REF','/OPT:ICF',*['/LIBPATH:'+str(p) for p in libs],*[str(r[0]) for r in results],'SDL2.lib','zlib.lib','shell32.lib','user32.lib','advapi32.lib','ole32.lib']
        rsp=out/'link.rsp';rsp.write_text(subprocess.list2cmdline(cmd),encoding='utf-8');linker=VC/('bin/Hostx64/'+arch+'/link.exe')
    else:
        cmd=['-nostdlib','-nostartfiles','-shared','-fPIC','-Os','-flto','-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,--exclude-libs,ALL','-Wl,-e,hmm_game_main',*[str(r[0]) for r in results],'-Wl,--start-group',str(RT/'libstdcpp_pic.a'),str(RT/'libunwind_pic.a'),'-Wl,--end-group','-o',str(out/'HMM2.partial.elf'),'-Wl,-Map,'+str(out/'HMM2.map')]
        rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in cmd),encoding='utf-8');linker=compiler
    r=subprocess.run([str(linker),'@'+str(rsp)],cwd=out,capture_output=True);(out/'link.log').write_bytes(r.stdout+r.stderr)
    print((r.stdout+r.stderr).decode(errors='replace')[-7000:],flush=True)
    if not r.returncode and args.host:
        for dll in (SDK.parent/('lib/'+arch)).glob('*.dll'):shutil.copy2(dll,out/dll.name)
        if asan:shutil.copy2(VC/('bin/Hostx64/'+arch+'/clang_rt.asan_dynamic-x86_64.dll'),out/'clang_rt.asan_dynamic-x86_64.dll')
    if not r.returncode and not args.host:
        for tool,opts,name in [('size',['-A'],'size.txt'),('nm',['-u'],'undefined.txt')]:
            x=subprocess.check_output([str(TC/('xtensa-esp32s3-elf-'+tool+'.exe')),*opts,str(out/'HMM2.partial.elf')]);(out/name).write_bytes(x);print(x.decode(),flush=True)
    return r.returncode
if __name__=='__main__':raise SystemExit(main())
