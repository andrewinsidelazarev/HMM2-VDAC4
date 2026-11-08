"""Codex: проверки новых контейнеров и настоящего выделителя петель без запуска игрового стенда."""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / 'temp/memory_unit'
VC = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
KIT = Path('C:/Program Files (x86)/Windows Kits/10')
SDK = Path('E:/zx/OpenHMM2/VisualStudio/packages/sdl2')


def main():
    names = ['memory_sequence_test', 'memory_audio_test', 'memory_decode_test', 'memory_threat_test', 'memory_heap_test', 'memory_scratch_test', 'memory_music_test', 'memory_materialize_test', 'memory_join_test']
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', nargs='+', choices=names)
    selected = parser.parse_args().only or names
    OUT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(TEMP=str(OUT), TMP=str(OUT), PYTHONDONTWRITEBYTECODE='1')
    source = (ROOT/'main/hmm_canvas.inc').read_text(encoding='utf-8')
    begin = source.index('bool same_layer_source(')
    end = source.index('struct CullRect ', begin)
    (OUT/'memory_join_impl.inc').write_text(source[begin:end], encoding='utf-8')
    begin = source.index('void map_materialize(')
    end = source.index('// Клод: mapping_hashes', begin)
    (OUT/'memory_materialize_impl.inc').write_text(source[begin:end], encoding='utf-8')
    source = (ROOT/'main/hmm_audio.cpp').read_text(encoding='utf-8')
    music_begin = source.index('struct AudioMemory ')
    music_end = source.index('struct AudioServiceScope ', music_begin)
    (OUT/'memory_music_impl.inc').write_text(source[music_begin:music_end], encoding='utf-8')
    descriptor = next(line for line in source.splitlines() if line.startswith('struct LoopBuffer '))
    begin = source.index('constexpr uint32_t loop_cache_max=')
    end = source.index('float sample(Effect &e)', begin)
    # Codex: пул готовых звуков 91 проверяет игровой стенд; здесь нужны только петли и их освобождение.
    pool_begin = source.index('/* Клод: звуки целиком', begin)
    release_begin = source.index('void loop_release(Effect &e)', pool_begin)
    # Codex: подменяется только окружение каналов; функции менеджмента и их константы исходные.
    prefix = descriptor+'\nstruct AudioLock {};\nstruct Effect {LoopBuffer *whole=nullptr;};\nEffect effects[channels_max];\n'
    (OUT/'memory_loop_impl.inc').write_text(prefix+source[begin:pool_begin]+source[release_begin:end], encoding='utf-8')
    source = (ROOT/'main/hmm_resources.cpp').read_text(encoding='utf-8')
    begin = source.index('constexpr unsigned decode_window=')
    end = source.index('static uint8_t used[', begin)
    (OUT/'memory_decode_impl.inc').write_text(source[begin:end], encoding='utf-8')
    a = source.index('static size_t free_run(')
    b = source.index('// Codex: большой фон', a)
    refresh_begin = source.index('static bool refresh_background()')
    compact_begin = source.index('static void compact_cached_pages(')
    refresh_end = source.index('// Codex: полный CPU-снимок', refresh_begin)
    c = source.index('extern "C" void *hmm_scratch_take(')
    d = source.index('static void *extra_alloc', c)
    (OUT/'memory_scratch_impl.inc').write_text(source[a:b]+source[compact_begin:refresh_end]+source[c:d], encoding='utf-8')
    source = (ROOT/'main/hmm_heap.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include .+$', '', source, flags=re.M)
    source = re.sub(r'\b(malloc|calloc|realloc|free)\b', r'test_\1', source)
    source = source.replace('__attribute__((noinline))', '')
    (OUT/'memory_heap_impl.inc').write_text(source, encoding='utf-8')
    package = ROOT.parent/'stage/data/HMM2.DAT'
    with package.open('rb') as data:
        header = data.read(16)
        entries = [data.read(72) for _ in range(struct.unpack_from('<I', header, 8)[0])]
        files = {e.split(b'\0')[0].decode(): struct.unpack_from('<II', e, 64) for e in entries}
        ftg = files['hmm2.ftg'][0]
        data.seek(ftg)
        header = struct.unpack('<4I', data.read(16))
        assert header[1] == 3
        data.seek(ftg+272)
        groups = [struct.unpack('<2I', data.read(8)) for _ in range(header[2]+header[3]*4)]
        with (OUT/'decode_cases.bin').open('wb') as index:
            for offset, count in groups:
                data.seek(ftg+offset)
                for _ in range(count):
                    record = struct.unpack('<25I', data.read(100))
                    at, compressed, w, h = record[:4]
                    if w and h:
                        index.write(struct.pack('<3I', files['hmm2.gfx'][0]+at, compressed, w*h*(1 if record[6] else 2)))
    for dll in (SDK/'lib/x64').glob('*.dll'):
        shutil.copy2(dll, OUT/dll.name)
    results = []
    for name in selected:
        is_c = name == 'memory_heap_test'
        command = [str(VC/'bin/Hostx64/x64/cl.exe'), '/nologo', '/O2', '/MT', '/EHsc', '/std:c11' if is_c else '/std:c++17', '/utf-8',
                   '/I'+str(VC/'include'), '/I'+str(KIT/'Include/10.0.26100.0/ucrt'), '/I'+str(OUT), '/I'+str(SDK/'include'),
                   str(ROOT/'tests'/f'{name}{".c" if is_c else ".cpp"}'), '/Fo'+str(OUT/f'{name}.obj'), '/Fe'+str(OUT/f'{name}.exe'),
                   '/link', '/LIBPATH:'+str(VC/'lib/x64'), '/LIBPATH:'+str(KIT/'Lib/10.0.26100.0/ucrt/x64'),
                   '/LIBPATH:'+str(KIT/'Lib/10.0.26100.0/um/x64'), '/LIBPATH:'+str(SDK/'lib/x64'), 'zlib.lib']
        built = subprocess.run(command, cwd=OUT, env=env, capture_output=True)
        (OUT/f'{name}.build.log').write_bytes(built.stdout+built.stderr)
        built.check_returncode()
        result = subprocess.run([str(OUT/f'{name}.exe'), str(package), str(OUT/'decode_cases.bin')], cwd=OUT, env=env, capture_output=True)
        (OUT/f'{name}.log').write_bytes(result.stdout+result.stderr)
        result.check_returncode()
        results.append(dict(test=name, result=result.stdout.decode().strip(), returncode=result.returncode))
    (OUT/'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
