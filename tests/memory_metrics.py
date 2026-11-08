"""Codex: сводка фактических кадров, памяти и чтений DAT без смешения прогонов."""
from pathlib import Path
from collections import Counter
import argparse
import json
import re
import struct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder', type=Path)
    ap.add_argument('--dat', type=Path)
    ap.add_argument('--after', type=int, default=0, help='Начало интервала по виртуальному времени QEMU, мс')
    ap.add_argument('--until', type=int, default=2**32-1, help='Конец интервала, мс')
    args = ap.parse_args()
    log = (args.folder / 'qemu.log').read_text(encoding='utf-8', errors='replace')
    frames = [tuple(map(int, m)) for m in re.findall(r'кадр=(\d+) T=(\d+) SD=(\d+)/(\d+) PSRAM свободно=(\d+) минимум=(\d+)', log)]
    selected = [f for f in frames if args.after <= f[1] < args.until]
    result = {'frames': len(frames), 'selected_frames': len(selected),
              'minimum_psram': min((f[5] for f in frames), default=None),
              'last_free': frames[-1][4] if frames else None,
              'heap_failures': log.count('HMM2 HEAP'), 'trims': log.count('HMM2 MEM trim'),
              'last_resort': log.count('HMM2 MEM last'),
              'oom_messages': log.count('QEMU: OOM'),
              'audio_allocation_failures': log.count('AUDIO decoder allocation failed'),
              'complete': 'QEMU: захват кадров завершён' in log,
              'oom': [dict(request=int(a), free=int(b), largest=int(c)) for a, b, c in re.findall(r'OOM запрос=(\d+) свободно=(\d+) блок=(\d+)', log)]}
    heaps = [tuple(map(int, row)) for row in re.findall(r'QEMU MEM frame=(\d+) free=(\d+) largest=(\d+) free_blocks=(\d+) used_blocks=(\d+)', log)]
    result['heap_samples'] = [dict(zip(('frame', 'free', 'largest', 'free_blocks', 'used_blocks'), row)) for row in heaps]
    result['stack_minimum'] = {kind: min(map(int, re.findall(pattern, log)), default=None) for kind, pattern in
                               [('game', r'STACK=(\d+)/16384'), ('audio', r'AUDIO_FREE=(\d+)/5632'),
                                ('worker', r'WORKER_FREE=(\d+)/3072')]}
    for kind in ['PERF', 'IO', 'MEMF', 'MEMR']:
        result[kind] = []
    time = 0
    for line in log.splitlines():
        stamp = re.search(r'кадр=\d+ T=(\d+)', line)
        if stamp:
            time = int(stamp[1])
        if not args.after <= time < args.until:
            continue
        for kind in ['PERF', 'IO', 'MEMF', 'MEMR']:
            if 'HMM2 ' + kind in line:
                row = dict((k, int(v) if v.isdigit() else v) for k, v in re.findall(r'(\w+)=([\d/]+)', line))
                row['time'] = time
                result[kind].append(row)
    if args.dat:
        with args.dat.open('rb') as f:
            header = f.read(16)
            # Codex: DAT карты пользователя имеет каталог FS3 в хвосте файла.
            if struct.unpack_from('<I', header, 4)[0] == 3:
                f.seek(struct.unpack_from('<I', header, 12)[0])
            entries = [f.read(72) for _ in range(struct.unpack_from('<I', header, 8)[0])]
        ranges = [(struct.unpack_from('<I', e, 64)[0], struct.unpack_from('<I', e, 68)[0], e.split(b'\0')[0].decode()) for e in entries]
        by_file, requests = Counter(), Counter()
        for line in (args.folder / 'reads.log').read_text().splitlines():
            time, kind, offset, size = line.split()
            if not args.after <= int(time) < args.until:
                continue
            offset, size = int(offset), int(size)
            name = next((name for at, n, name in ranges if at <= offset < at+n), '<catalogue/alignment>')
            by_file[name] += size
            requests[name] += 1
        result['reads'] = {name: {'bytes': n, 'requests': requests[name]} for name, n in by_file.most_common()}
    target = args.folder / ('metrics.json' if not args.after and args.until==2**32-1 else f'metrics_{args.after}_{args.until}.json')
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('PERF', 'IO', 'MEMF', 'MEMR', 'oom', 'heap_samples')}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
