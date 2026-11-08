"""Codex: все DLSWAP двух трасс FT812 сравниваются по всем RGB-байтам кадра.

Повторяется лишь пиксельный расчёт с теми же командами, входным состоянием
и байтами всех читаемых текстур. Команды состояния исполняются каждый раз.
"""
from pathlib import Path
import argparse, hashlib, itertools, json, struct
from check_fix5_ft import render
from ft_command_fifo import CommandFifo


def frames(path):
    ram, dl = bytearray(0x300000), bytearray(8192)
    fifo = CommandFifo()
    handles = [dict(source=0, stride=0, bh=0, bw=0, oh=0) for _ in range(32)]
    memo = {}
    with path.open('rb') as f:
        while header := f.read(8):
            address, count = struct.unpack('<II', header)
            raw = f.read(count * 4)
            assert len(raw) == count * 4
            address &= 0x7fffffff
            if address < 0x300000:
                assert address + len(raw) <= len(ram)
                ram[address:address+len(raw)] = raw
            elif address < 0x302000:
                offset = address - 0x300000
                assert offset + len(raw) <= len(dl)
                dl[offset:offset+len(raw)] = raw
            elif address == 0x302054:
                words = []
                for w, in struct.iter_unpack('<I', dl):
                    words.append(w)
                    if not w:
                        break
                assert words[-1] == 0 and len(words) <= 2048
                before = [dict(v) for v in handles]
                protected = []
                render(words, ram, protected, handle_state=handles, dry_run=True)
                regions = []
                for begin, end in sorted(set(protected)):
                    # Codex: BITMAP_SOURCE обрезанного атласа может быть смещён;
                    # консервативный stride*height выходит за RAM_G, хотя видимые
                    # строки остаются внутри. В таком случае кэшируем по всей RAM_G.
                    if not 0 <= begin <= end <= len(ram):
                        regions = [[0, len(ram)]]
                        break
                    if regions and begin <= regions[-1][1]:
                        regions[-1][1] = max(end, regions[-1][1])
                    else:
                        regions.append([begin, end])
                h = hashlib.sha256(dl[:len(words)*4])
                h.update(repr(before).encode())
                for begin, end in regions:
                    h.update(struct.pack('<II', begin, end))
                    h.update(memoryview(ram)[begin:end])
                key = h.digest()
                if key not in memo:
                    rgb, _ = render(words, ram, handle_state=before)
                    assert before == handles
                    memo[key] = hashlib.sha256(rgb.tobytes()).hexdigest()
                yield memo[key]
            else:
                fifo.write(address, raw, ram, dl)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('a', type=Path)
    p.add_argument('b', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    result = dict(a=str(a.a), b=str(a.b), frames=0, mismatch_frames=[], pixel_hashes=[])
    for i, pair in enumerate(itertools.zip_longest(frames(a.a), frames(a.b)), 1):
        assert None not in pair, ('different frame counts', i)
        result['frames'] = i
        result['pixel_hashes'].append(pair)
        if pair[0] != pair[1]:
            result['mismatch_frames'].append(i)
        if i % 100 == 0:
            print(i, 'mismatches', len(result['mismatch_frames']), flush=True)
    a.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print({k: v for k, v in result.items() if k != 'pixel_hashes'}, flush=True)
    return bool(result['mismatch_frames'])


if __name__ == '__main__':
    raise SystemExit(main())
