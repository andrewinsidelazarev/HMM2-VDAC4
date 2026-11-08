"""Codex: независимая проверка L1 и точной альфы по формулам ft_rd_exec/pix Tang."""
from pathlib import Path
import ctypes as C
import json
import sys
from walk3x_compare_fast import library


def main():
    d = library()
    ram = bytearray(0x300000)
    for y in range(32):
        for x in range(32):
            if (x * 7 + y * 11) % 13 < 6:
                ram[y * 4 + x // 8] |= 128 >> (x & 7)
    rp = (C.c_uint8 * len(ram)).from_buffer(ram)
    rgb = C.create_string_buffer(1024 * 768 * 3)
    background = (17, 89, 203)
    cases = []
    for color in [(0, 0, 0), (255, 255, 255), (73, 149, 211)]:
        for alpha in [17, 34, 51, 60, 85, 88, 119, 153, 254, 255]:
            words = [0x19000100, 0x02000000 | 17 << 16 | 89 << 8 | 203, 0x26000007,
                     0x01000000, 0x07080820, 0x08004020,
                     0x04000000 | color[0] << 16 | color[1] << 8 | color[2],
                     0x10000000 | alpha, 0x1b000000, 0x1c020020,
                     0x1f000001, 0x80000000, 0x21000000, 0]
            wp = (C.c_uint32 * len(words))(*words)
            handles = (C.c_uint32 * (32 * 8))()
            assert d.ft_rgb(wp, len(words), rp, handles, rgb) == 640 * 480 * 3
            raw = rgb.raw
            for y in range(32):
                for x in range(32):
                    active = (x * 7 + y * 11) % 13 < 6
                    expected = bytes(c * alpha // 255 + b * (255 - alpha) // 255
                                     for c, b in zip(color, background)) if active else bytes(background)
                    at = (y * 640 + x) * 3
                    assert raw[at:at + 3] == expected, (color, alpha, x, y)
            cases.append(dict(color=color, alpha=alpha, pixels=1024, passed=True))
    result = dict(cases=cases, passed=True)
    Path(sys.argv[1]).write_text(json.dumps(result, indent=2))
    print('L1: 30 cases, 30720 pixels, exact alpha and RGB')


if __name__ == '__main__':
    main()
