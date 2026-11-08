"""Codex: дописываем только обычные клавиши/мышь стенда, не изменяя состояние игры."""
from pathlib import Path
import argparse
import os
import re
import struct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--key', action='append', default=[], help='PS/2 hex')
    ap.add_argument('--click', action='append', default=[], help='x,y[,button]')
    ap.add_argument('--delay', type=int, default=12000)
    ap.add_argument('--gap', type=int, default=12000)
    ap.add_argument('--finish', action='store_true')
    args = ap.parse_args()
    out = Path(os.environ['HMM2_STAGE1_BASE']) / 'qemu_stage1'
    log = (out / 'qemu.log').read_text(encoding='utf-8', errors='replace')
    start = int(re.search(r'input ready after first frame at=(\d+)', log)[1])
    frame, time, free = re.findall(r'кадр=(\d+) T=(\d+).*?PSRAM свободно=(\d+)', log)[-1]
    now = int(time) - start
    print(f'frame={frame} input_ms={now} free={free}')
    def write(name, data):
        target = out / name
        pending = target.with_suffix('.pending')
        pending.write_bytes(data)
        pending.replace(target)
    keys = bytearray((out / 'ui_keys.bin').read_bytes())
    times = [v[0] for v in struct.iter_unpack('<I', (out / 'ui_times.bin').read_bytes())]
    clicks = [list(v) for v in struct.iter_unpack('<4I', (out / 'ui_clicks.bin').read_bytes())]
    pending = [now + args.delay]
    if args.key and times:
        pending.append(times[-1] + args.gap)
    if args.click and clicks:
        pending.append(clicks[-1][0] + args.gap)
    at = max(pending)
    for key in args.key:
        keys.append(int(key, 16));times.append(at)
        print(f'key {key} at {at}')
        at += args.gap
    for click in args.click:
        values = list(map(int, click.split(',')))
        if len(values) == 2:
            values.append(1)
        assert len(values) == 3
        clicks.append([at, *values]);print(f'click {values} at {at}')
        at += args.gap
    assert len(keys) <= 128 and len(clicks) <= 512
    write('ui_times.bin', struct.pack('<'+'I'*len(times), *times))
    write('ui_keys.bin', keys)
    write('ui_clicks.bin', b''.join(struct.pack('<4I', *v) for v in clicks))
    if args.finish:
        write('ui_test.bin', struct.pack('<2I', 0, 0))


if __name__ == '__main__':
    main()
