"""Codex: компилируем всю копию движка для Xtensa; это аудит, не готовый порт SDL."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = Path(os.environ.get('HMM2_OPENHMM2_SRC', str(ROOT / 'third_party/openhmm2/src')))
OUT = Path('E:/zx/cache/tmp/hmm2_esp/xtensa')
TC = Path(os.environ.get('HMM2_XTENSA_BIN', 'E:/zx/esp/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin'))
SDK = Path(os.environ.get('HMM2_SDL_INCLUDE', 'E:/zx/OpenHMM2/VisualStudio/packages/sdl2/include'))

def run():
    OUT.mkdir(parents=True, exist_ok=True)
    headers=OUT/'headers';headers.mkdir(exist_ok=True)
    header=(SRC/'fheroes2/game/game_auto_playtest.h').read_text(encoding='utf-8').replace('std::clamp(', 'std::clamp<int32_t>(')
    (headers/'game_auto_playtest.h').write_text(header,encoding='utf-8')
    inc = [headers, ROOT/'tools/probe_include', SRC/'engine', SRC/'thirdparty/libsmacker', SDK, SDK/'SDL2']
    inc += sorted(p for p in (SRC/'fheroes2').iterdir() if p.is_dir())
    flags = ['-std=c++17', '-Os', '-DNDEBUG', '-fPIC', '-fvisibility=hidden',
             '-ffunction-sections', '-fdata-sections', '-mlongcalls', '-fno-strict-aliasing', '-fpermissive']
    flags += ['-I'+str(p) for p in inc]
    sources = sorted((SRC/'engine').glob('*.cpp')) + sorted((SRC/'fheroes2').rglob('*.cpp'))
    def compile_one(src):
        rel = src.relative_to(SRC).as_posix()
        obj = OUT/(rel.replace('/', '_')+'.o')
        s=src.read_text(encoding='utf-8')
        # Codex: явные 32-битные типы устраняют различие int и long в Xtensa, не меняя арифметику.
        if src.name in ('screen.cpp','ui_scrollbar.cpp'):s=s.replace('std::clamp(', 'std::clamp<int32_t>(').replace('std ::clamp(', 'std::clamp<int32_t>(')
        if src.name in ('editor_interface.cpp','map_random_generator_helper.cpp'):
            s=s.replace('std::max( 0, tilePos.', 'std::max<int32_t>( 0, tilePos.').replace('std::max( 1, static_cast<int32_t>( openTiles.size() )', 'std::max<int32_t>( 1, static_cast<int32_t>( openTiles.size() )')
        if src.name=='editor_interface_panel.cpp':s=s.replace('int & objectIndex','int32_t & objectIndex')
        if src.name=='maps_tiles.cpp':s=s.replace('bool & isActionObject ) {','bool & isActionObject ) -> int32_t {')
        actual=OUT/('staged_'+rel.replace('/','_'))
        actual.write_text(s,encoding='utf-8')
        cmd = [str(TC/'xtensa-esp32s3-elf-g++.exe'), *flags, '-c', str(actual), '-o', str(obj)]
        signature = hashlib.sha256((s+header+repr(cmd)).encode()).hexdigest()
        stamp = obj.with_suffix('.sha256')
        if obj.exists() and stamp.exists() and stamp.read_text() == signature:
            return {'source':rel, 'ok':True, 'object':str(obj), 'cached':True}
        p = subprocess.run(cmd, capture_output=True)
        log = p.stdout+p.stderr
        obj.with_suffix('.log').write_bytes(log)
        if not p.returncode: stamp.write_text(signature)
        return {'source':rel, 'ok':p.returncode == 0, 'object':str(obj),
                'command':cmd, 'diagnostic':log.decode(errors='replace')}
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(compile_one, sources))
    report = ROOT/'Docs/evidence/compile_probe.json'
    report.write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    failed = [r for r in results if not r['ok']]
    print(f'Всего: {len(results)}; собрано: {len(results)-len(failed)}; ошибок: {len(failed)}',flush=True)
    for r in failed:
        errors = [s for s in r['diagnostic'].splitlines() if 'error:' in s]
        print(r['source'], '\n'.join(errors[:4]),flush=True)

if __name__ == '__main__':
    run()
