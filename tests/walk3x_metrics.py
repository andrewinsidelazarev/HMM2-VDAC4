"""Codex: TIME и PERF одного окна без суммирования вложенных профилей.

TIME хранит миллисекунды CCOUNT за N кадров. PERF — время стенда за N кадров;
engine включает sleep. Все исходные счётчики сохраняются рядом с мс/кадр.
"""
from pathlib import Path
import argparse, hashlib, json, re


def fields(line):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)(?![\d/])', line)}


def measure(folder):
    path = folder / 'qemu.log'
    log = path.read_text(encoding='utf-8', errors='replace')
    rows, timing, opt, gpu = [], {}, {}, {}
    stamp = 0
    for line in log.splitlines():
        t = re.search(r'кадр=\d+ T=(\d+)', line)
        if t:
            stamp = int(t[1])
        if 'HMM2 GPU:' in line:
            gpu = fields(line)
        elif 'HMM2 TIME ' in line:
            timing = fields(line)
        elif 'HMM2 TIME2 ' in line:
            timing.update(fields(line))
        elif 'HMM2 OPT ' in line:
            opt = fields(line)
        elif 'HMM2 PERF ' in line:
            perf = fields(line)
            assert gpu.get('frame') == perf['frame'], (gpu, perf)
            n = perf['N']
            rows.append(dict(frame=perf['frame'], time_before_ms=stamp, TIME=timing,
                             PERF=perf, OPT=opt, GPU=gpu,
                             TIME_ms_per_frame={k: v/n for k, v in timing.items()},
                             PERF_ms_per_frame={k: perf[k]/n for k in ['wall','engine','sleep','prepare','LINK_ms','DLSWAP']},
                             busy_wall_ms_per_frame=(perf['wall']-perf['sleep'])/n,
                             busy_engine_ms_per_frame=(perf['engine']-perf['sleep'])/n))
    minima = list(map(int, re.findall(r'минимум=(\d+)', log)))
    result = dict(log_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  minimum_psram=min(minima, default=None),
                  heap_failures=log.count('HMM2 HEAP'), oom_messages=log.count('QEMU: OOM'),
                  complete='QEMU: захват кадров завершён' in log,
                  rows=rows)
    (folder/'walk3x_metrics.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(folder, 'min_psram', result['minimum_psram'], 'heap', result['heap_failures'], 'oom', result['oom_messages'], flush=True)
    for row in rows:
        if row['PERF']['sleep'] <= 2:
            print(row['frame'], row['PERF_ms_per_frame'], row['TIME_ms_per_frame'], flush=True)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('folders', nargs='+', type=Path)
    for folder in p.parse_args().folders:
        measure(folder)
