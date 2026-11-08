"""Codex: проверка полного сохранения Xtensa и его чтения исходным движком x64."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import zlib


def decode_save(path):
    data = path.read_bytes()
    candidates = []
    for at in range(min(2048, len(data) - 12)):
        raw_size, packed_size, version, unused = struct.unpack_from('>IIHH', data, at)
        if at + 12 + packed_size == len(data) and not version and not unused:
            candidates.append((at, raw_size, packed_size))
    assert len(candidates) == 1, (str(path), len(data), candidates)
    at, raw_size, packed_size = candidates[0]
    raw = zlib.decompress(data[at + 12:])
    assert len(raw) == raw_size and raw[-2:] == b'\xff\x03'
    return raw, dict(save_bytes=len(data), raw_bytes=raw_size, packed_bytes=packed_size,
                     zlib_window_bytes=1 << ((data[at + 12] >> 4) + 8),
                     save_sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('save', type=Path)
    parser.add_argument('--host', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    raw, result = decode_save(args.save)
    args.out.mkdir(parents=True, exist_ok=True)
    if args.host:
        states = args.out / 'states'
        states.mkdir(exist_ok=True)
        run = args.out / 'run'
        run.mkdir(exist_ok=True)
        root = Path(__file__).resolve().parents[1]
        shutil.copy2(root / 'main/fheroes2.cfg', run / 'fheroes2.cfg')
        # Codex: упакованный стенд читает сохранения через тот же индекс, что и Xtensa.
        shutil.copy2(args.save, run / 'H2S00000.SAV')
        index = bytearray(8192)
        name = b'quicksave.savh'
        index[:len(name)] = name
        (run / 'HMM2.IDX').write_bytes(index)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   HMM2_TEST_MAP='maps/GateHell.mp2', HMM2_STATE_OUT=str(states.resolve()),
                   HMM2_TEST_LOAD_EXTERNAL='files/save/quicksave.savh')
        with (args.out / 'run.log').open('wb') as log:
            process = subprocess.run([str(args.host.resolve()), str(args.out.resolve()), '1000'],
                                     cwd=args.out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        log = (args.out / 'run.log').read_text(encoding='utf-8', errors='replace')
        assert process.returncode == 0 and 'TARGET SAVE LOAD COMPLETE' in log, log[-2000:]
        world = (states / 'target_world.be').read_bytes()
        assert raw.startswith(world)
        result.update(loaded_by_x64=True, world_bytes_exact=True, world_bytes=len(world),
                      world_sha256=hashlib.sha256(world).hexdigest(),
                      day=int(re.search(r'TARGET SAVE DAY=(\d+)', log)[1]))
    (args.out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
