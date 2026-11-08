"""Codex: независимое исполнение каждого DL сохранённого прогона и контроль RAM_G."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from ft_command_fifo import CommandFifo
from check_fix5_ft import render
from test_ft_trace import png


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder', type=Path)
    ap.add_argument('--shots', type=int, nargs='*', default=[])
    ap.add_argument('--state-only', action='store_true', help='Codex: исполнить состояние и проверить RAM_G всех DL; пиксели считать только для --shots')
    args = ap.parse_args()
    data = (args.folder / 'menu.ft').read_bytes()
    ram, dl = bytearray(3*1024*1024), bytearray(8192)
    fifo = CommandFifo()
    handles = [dict(source=0, stride=0, bh=0, bw=0, oh=0) for _ in range(32)]
    protected, hashes = [], []
    memo = {}
    at = largest = 0
    while at < len(data):
        addr, count = struct.unpack_from('<II', data, at)
        addr &= 0x7fffffff
        at += 8
        raw = data[at:at+count*4]
        assert len(raw) == count*4
        at += len(raw)
        if addr < 0x300000:
            assert addr+len(raw) <= len(ram)
            assert not any(addr < end and addr+len(raw) > begin for begin, end in protected)
            ram[addr:addr+len(raw)] = raw
        elif 0x300000 <= addr < 0x302000:
            offset = addr-0x300000
            assert offset+len(raw) <= len(dl)
            dl[offset:offset+len(raw)] = raw
        elif addr == 0x302054:
            words = []
            for value, in struct.iter_unpack('<I', dl):
                words.append(value)
                if not value:
                    break
            assert words[-1] == 0 and len(words) <= 2048
            largest = max(largest, len(words))
            before = [dict(v) for v in handles]
            protected = []
            render(words, ram, protected, handle_state=handles, dry_run=True)
            # Codex: неиспользуемая текстура не влияет на результат текущего DL.
            # Состояние исполняется каждый раз; кэшируется только результат пикселей.
            regions = []
            for begin, end in sorted(set(protected)):
                assert 0 <= begin <= end <= len(ram)
                if regions and begin <= regions[-1][1]:
                    regions[-1][1] = max(regions[-1][1], end)
                else:
                    regions.append([begin, end])
            serial = len(hashes)+1
            if args.state_only and serial not in args.shots:
                # Codex: длинная сессия проверяет каждый DL и каждую запись в RAM_G;
                # отсутствие пиксельного прохода явно обозначается null и отдельным отчётом.
                hashes.append(None)
                if not serial % 1000:
                    print(f'verified_state={serial}', flush=True)
                continue
            source_hash = hashlib.sha256()
            for begin, end in regions:
                source_hash.update(struct.pack('<II', begin, end))
                source_hash.update(memoryview(ram)[begin:end])
            key = (bytes(dl[:len(words)*4]), source_hash.digest(), repr(before))
            cached = memo.get(key)
            serial = len(hashes)+1
            if cached and serial not in args.shots:
                digest = cached
            else:
                rendered_regions = []
                rgb, _ = render(words, ram, rendered_regions, handle_state=before)
                assert before == handles and rendered_regions == protected
                pixels = rgb.tobytes()
                digest = hashlib.sha256(pixels).hexdigest()
                memo[key] = digest
                if serial in args.shots:
                    png(args.folder/f'verified_{serial}.png', pixels, rgb.shape[1], rgb.shape[0])
            hashes.append(digest)
            if not serial % 1000:
                print(f'verified={serial} unique={len(memo)}', flush=True)
        elif fifo.write(addr, raw, ram, dl):
            pass
        elif addr not in (0x302034, 0x302048, 0x302070):
            raise AssertionError(hex(addr))
    result = dict(frames=len(hashes), unique_inputs=len(memo), max_dl_words=largest, no_visible_overwrites=True,
                  full_pixel_render=not args.state_only, rendered_frames=sum(v is not None for v in hashes),
                  elf_sha256=hashlib.sha256((args.folder/'HMM2.ELF').read_bytes()).hexdigest(),
                  trace_sha256=hashlib.sha256(data).hexdigest(), frame_hashes=hashes)
    (args.folder/('verified_state.json' if args.state_only else 'verified_trace.json')).write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'frame_hashes'}))


if __name__ == '__main__':
    main()
