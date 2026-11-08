"""Codex: машинный код RTC и запросы ESP → Z80 → настоящий FAT32 → сектор каталога."""
from datetime import datetime, timedelta
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct

from test_stage1_joint import BASE, base, fixture
from fix4_z80 import patched_machine

OUT = Path(os.environ.get('HMM2_FILE_TIME_OUT', str(BASE / 'file_time_checks')))
SYM = base.symbols(BASE / 'launcher/build/hmm2.sym')
CODE = (BASE / 'launcher/build/hmm2_z80.bin').read_bytes()


def fat_time(dt):
    return struct.pack('<HH', dt.hour << 11 | dt.minute << 5 | dt.second // 2,
                       (dt.year - 1980) << 9 | dt.month << 5 | dt.day)


def rtc(dt, binary=False, h24=True, vrt=0, setting=False):
    def encode(n):
        return n if binary else (n // 10) * 16 + n % 10
    hour = dt.hour if h24 else dt.hour % 12 or 12
    return {0: encode(dt.second), 2: encode(dt.minute),
            4: encode(hour) | (0x80 if not h24 and dt.hour >= 12 else 0),
            7: encode(dt.day), 8: encode(dt.month), 9: encode(dt.year - 2000),
            10: 0x20, 11: (4 if binary else 0) | (2 if h24 else 0) | (128 if setting else 0),
            13: vrt}


def clock_checks():
    cases = []
    for binary in (False, True):
        for h24 in (False, True):
            for dt in (datetime(2026, 11, 3, 0, 1, 3), datetime(2026, 11, 3, 12, 34, 56),
                       datetime(2026, 11, 3, 23, 59, 59), datetime(2000, 2, 29),
                       datetime(2099, 12, 31, 23, 59, 58)):
                cases.append((f'{dt.isoformat()} binary={binary} h24={h24}', rtc(dt, binary, h24), dt, ''))
    dt = datetime(2026, 11, 3, 15, 27, 59)
    cases += [('VRT=0 SET=1', rtc(dt, setting=True), dt, ''),
              ('VRT=1', rtc(dt, vrt=128), dt, ''),
              ('UIP first attempt', rtc(dt), dt, 'uip_once'),
              ('second rollover', rtc(dt), dt, 'second_once'),
              ('mode change', rtc(dt), dt, 'mode_once'),
              ('UIP forever', rtc(dt), None, 'uip_forever'),
              ('seconds never stable', rtc(dt), None, 'second_forever'),
              ('missing FF', dict.fromkeys(range(14), 255), None, ''),
              ('missing zero', dict.fromkeys(range(14), 0), None, '')]
    for reg, value in [(0, 0x60), (0, 0x5a), (2, 0x60), (2, 0xa0), (4, 0x24),
                       (4, 0x81), (7, 0), (7, 0x31), (8, 0), (8, 0x13), (9, 0xa0)]:
        cases.append((f'invalid BCD r{reg}={value:02x}', rtc(dt) | {reg: value}, None, ''))
    for reg, value in [(0, 60), (2, 60), (4, 24), (7, 32), (8, 13), (9, 100)]:
        cases.append((f'invalid binary r{reg}={value}', rtc(dt, True) | {reg: value}, None, ''))
    for binary in (False, True):
        for hour in (0, 13, 128, 141):
            cases.append((f'invalid 12h {hour} binary={binary}', rtc(dt, binary, False) | {4: hour}, None, ''))
    for year in (2001, 2026, 2099):
        cases.append((f'non leap {year}', rtc(datetime(year, 2, 28)) | {7: 0x29}, None, ''))
    proof = []
    for label, values, expected, fault in cases:
        m = base.z80.Z80Machine()
        m.set_memory_block(0x8000, CODE)
        guard = bytes((i * 79 + 13) & 255 for i in range(1024))
        m.set_memory_block(0xc000, guard)
        state = dict(reg=0, reads=0, seconds=0, mode=0, uip=0)
        writes = []
        def output(port, value):
            writes.append((port, value))
            assert port in (0xeff7, 0xdff7), hex(port)
            if port == 0xdff7:
                state['reg'] = value
        def input_port(port):
            assert port == 0xbff7, hex(port)
            r = state['reg']
            state['reads'] += 1
            if r == 10:
                state['uip'] += 1
                if fault == 'uip_forever' or fault == 'uip_once' and state['uip'] == 1:
                    return 128
            if r == 0:
                state['seconds'] += 1
                if fault == 'second_once' and state['seconds'] == 1:
                    return 0x58
                if fault == 'second_forever' and state['seconds'] % 2:
                    return 0x58
            if r == 11:
                state['mode'] += 1
                if fault == 'mode_once' and state['mode'] == 2:
                    return values[r] ^ 4
            return values.get(r, 0)
        m.set_input_callback(input_port)
        m.set_output_callback(output)
        m.pc, m.sp = SYM['FileClock'], 0xbff0
        m.set_memory_block(m.sp, struct.pack('<H', 0x7fff))
        m.set_breakpoint(0x7fff)
        m.ticks_to_stop = 50000
        m.run()
        assert m.pc == 0x7fff, (label, 'unbounded RTC', hex(m.pc))
        assert bool(m.f & 1) == (expected is None), (label, m.f)
        if expected:
            at = SYM['FileTimeMeta'] + 11
            assert bytes(m.memory[at:at+4]) == fat_time(expected), label
        assert bytes(m.memory[0xc000:0xc400]) == guard, label
        assert state['reg'] == 0xf0 and state['reads'] <= 44, (label, state)
        proof.append(dict(case=label, accepted=expected is not None, reads=state['reads'],
                          cycles=50000-int.from_bytes(m._StateBase__ticks_to_stop, 'little')))
    return proof


def protocol_checks(nested=False):
    dep = BASE / 'launcher/third_party/fat32/build'
    dep.mkdir(parents=True, exist_ok=True)
    for name in ('fat32.bin', 'fat32-work.bin'):
        shutil.copy2(BASE / 'launcher/build' / name, dep / name)
    shutil.copy2(BASE / 'launcher/build/hmm2_z80.bin', BASE / 'launcher/build/hmm2.bin')
    disk = fixture({'HMM2.ELF': base.ELF, 'HMM2.DAT': bytes(8192), 'OGG.ELF': bytes(512),
                    'FHEROES2.KEY': b'old key'}, nested)
    directory = 7 if nested else 2
    # Codex: ненулевые даты создания/доступа и скрытый системный атрибут обязаны сохраниться.
    lba = disk.data_start + (directory-2)*disk.spc
    sector = bytearray(disk.read(lba))
    at = sector.index(b'FHEROES2KEY')
    sector[at+11], sector[at+13] = 0x26, 125
    struct.pack_into('<HHH', sector, at+14, 0x5bc3, 0x5821, 0x5822)
    disk.write(lba, sector)
    # Codex: даты/атрибуты соседей и содержимое диска проверяет независимый разбор FAT.
    def entry(name):
        return next((e['raw'] for e in disk.entries(directory) if e['name'].upper() == name), None)
    def contents(e):
        first = struct.unpack_from('<H', e, 26)[0] | struct.unpack_from('<H', e, 20)[0] << 16
        size = struct.unpack_from('<I', e, 28)[0]
        return b''.join(disk.cluster_data(c) for c in disk.chain(first))[:size]
    neighbours = {n: entry(n) for n in ('HMM2.ELF', 'HMM2.DAT')}
    now = datetime(2026, 11, 3, 15, 27, 59)
    steps = []
    def add(op, name, *, other='', data=b'', offset=0, values=None, stamp=None, expected=('ok', None), content=None, meta_error=False):
        # Клод: порция записи (op 3) время не ставит и часы не читает: время файла — от создания/усечения и переименования.
        if stamp is None:
            stamp = op != 3
        when = now if values is not None else now + timedelta(seconds=2*len(steps))
        steps.append(dict(op=op, name=name, other=other, data=data, offset=offset,
                          values=rtc(when) if values is None else values, stamp=stamp, when=when,
                          expected=expected, content=content, meta_error=meta_error))
    for name in ('H2S00000.SAV', 'HMM2.IDX', 'FHEROES2.KEY'):
        add(2, name, content=b'')
        payload = bytes((i * 37 + len(name)) & 255 for i in range(960))
        add(3, name, data=payload, content=payload)
        add(1, name, expected=('data', payload), stamp=False)
        add(3, name, data=b'RTC', offset=960, content=payload+b'RTC', values=rtc(now, True, False))
        add(2, name, offset=127, content=payload[:127], values=rtc(now, False, False))
    add(2, 'SAVE.$$$', content=b'')
    add(3, 'SAVE.$$$', data=b'new save', content=b'new save')
    add(4, 'SAVE.$$$', other='H2S00000.SAV', content=b'new save', values=rtc(now, setting=True))
    add(2, 'SAVE.$$$', content=b'')
    add(3, 'SAVE.$$$', data=b'first save', content=b'first save')
    add(4, 'SAVE.$$$', other='NEW.SAV', content=b'first save')
    # Codex: снимок на каждой операции меняется; при отказе остаётся последняя дата.
    add(3, 'HMM2.IDX', data=b'bad RTC', content=b'bad RTC'+bytes((i*37+8)&255 for i in range(7,127)),
        values=dict.fromkeys(range(14), 0), stamp=False)
    add(2, 'FHEROES2.KEY', offset=9, content=bytes((i*37+12)&255 for i in range(9)),
        values=dict.fromkeys(range(14), 255), stamp=False)
    add(2, 'SAVE.$$$', content=b'')
    add(4, 'SAVE.$$$', other='NEW.SAV', content=b'', values=rtc(now) | {8: 0}, stamp=False)
    add(3, 'HMM2.IDX', data=b'!', stamp=False, meta_error=True,
        content=b'!ad RTC'+bytes((i*37+8)&255 for i in range(7,127)))
    add(3, 'HMM2.IDX', data=b'fail', stamp=False, expected=('error', None))
    add(3, 'MISSING.SAV', data=b'no', expected=('missing', None), stamp=False)
    add(6, 'NEW.SAV', stamp=False)
    add(5, 'NEW.SAV', stamp=False)
    add(0, 'NEW.SAV', expected=('missing', None), stamp=False)
    proof = []

    class App(base.Esp):
        def __init__(self):
            super().__init__(cached=True, paths={})
            self.plan = [(0xe5, base.fs(s['op'], s['name'], s['other'], offset=s['offset'], data=s['data'],
                                       length=len(s['expected'][1]) if s['expected'][0] == 'data' else 0),
                          s['expected']) for s in steps]
            self.index = -1
            self.values = rtc(now)
        def next_request(self):
            self.index += 1
            if self.plan:
                s = steps[self.index]
                self.values = s['values']
                self.before = entry(s['name'])
                self.before_data = contents(self.before) if self.before else None
                self.reads_before = getattr(self, 'rtc_reads', 0)
                # Codex: только копия ОЗУ модели: отказ SET_METADATA и отказ основной записи.
                if 'FileTimeFx' in SYM:
                    self.machine.memory[SYM['FileTimeFx']+2] = 255 if s['meta_error'] else 6
                self.machine.fail_writes = s['expected'][0] == 'error'
            return super().next_request()
        def __call__(self, command, p):
            if command == 0x6c:
                assert p == b'HMM2.ELF\0'
                self.started = True
                return base.frame(0xec, b'\0')
            if command == 0x72:
                return base.frame(0xf2, b'\1')
            if command == 0x70:
                assert p == struct.pack('<I', 8192) + b'HMM2.DAT\0'
                return base.frame(0xf0, b'\1') + self.next_request()
            if command == 0x74:
                s = steps[self.index]
                name = s['other'] or s['name']
                e = entry(name)
                if s['expected'][0] not in ('missing', 'error') and s['op'] != 5:
                    assert e is not None, (self.index, name, p.hex(), disk.entries())
                    if s['stamp']:
                        assert e[22:26] == fat_time(s['when']), (self.index, name, e.hex())
                    elif self.before:
                        assert e[22:26] == self.before[22:26], (self.index, name)
                    if self.before:
                        assert e[11:20] == self.before[11:20], (self.index, 'other metadata')
                    if s['content'] is not None:
                        assert contents(e) == s['content'], (self.index, 'content', name)
                if s['expected'][0] == 'error':
                    assert e == self.before, (self.index, 'failed write changed metadata')
                    assert contents(e) == self.before_data, (self.index, 'failed write changed data')
                if s['op'] in (0, 1, 3, 5, 6) or s['expected'][0] in ('missing', 'error'):
                    assert self.rtc_reads == self.reads_before, (self.index, 'unexpected RTC read')
                if s['op'] in (4, 5) and s['expected'][0] == 'ok':
                    assert entry(s['name']) is None
                for n, old in neighbours.items():
                    assert entry(n) == old, n
                proof.append(dict(op=s['op'], name=name, status=p[2],
                                  directory_time=None if e is None else e[22:26].hex()))
            return super().__call__(command, p)

    class Machine(patched_machine(base)):
        def port_in(self, port):
            if port == 0xbff7 and self.gluk_reg < 0xf0:
                self.app.rtc_reads = getattr(self.app, 'rtc_reads', 0) + 1
                return self.app.values.get(self.gluk_reg, 0)
            return super().port_in(port)
    app = App()
    m = Machine(disk, app, 'hmm2')
    app.machine = m
    m.ports.windows[2], m.ports.windows[32] = 0x8000, 0xc000
    m.run()
    assert app.done and len(app.answers) == len(steps), (len(app.answers), m.border)
    assert not any(r < 0xf0 and r != 12 for r, _ in m.gluk_writes), m.gluk_writes
    return dict(nested=nested, requests=proof, pulls=len(app.pulls), sd_writes=m.writes,
                spg_sha256=hashlib.sha256((BASE/'launcher/_HMM2.SPG').read_bytes()).hexdigest())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    clocks = clock_checks()
    print(f'RTC machine code: {len(clocks)} passed', flush=True)
    protocol = [protocol_checks(nested) for nested in (False, True)]
    (OUT/'result.json').write_text(json.dumps(dict(clock=clocks, protocol=protocol), indent=2), encoding='utf-8')
    print(f'ESP/FAT32 protocol: {sum(len(p["requests"]) for p in protocol)} passed (root + nested)', flush=True)


if __name__ == '__main__':
    main()
