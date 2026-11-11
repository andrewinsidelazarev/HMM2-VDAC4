"""Execute the Z80 cache with banked RAM and SD refill data; preserve WC page F7."""
from pathlib import Path
import json, os, struct
from test_stage1_joint import BASE, base

OUT = Path(os.environ.get('HMM2_WC_PAGE_OUT', 'E:/zx/cache/tmp/hmm2_wc_f7/cache_test'))

def main():
    sym = base.symbols(BASE/'launcher/build/hmm2.sym')
    code = (BASE/'launcher/build/hmm2_z80.bin').read_bytes()
    proof = []
    for last in (64, 81, 127):
        m = base.z80.Z80Machine()
        m.set_memory_block(0x8000, code)
        memory = m.memory
        sentinel = bytes((i*37+19)&255 for i in range(16384))
        banks = {0xf7: sentinel}
        state = dict(page=32, fills=0)
        maps = set()
        def get(name, n=1):
            return int.from_bytes(bytes(memory[sym[name]:sym[name]+n]), 'little')
        def put(name, value, n=1):
            m.set_memory_block(sym[name], value.to_bytes(n, 'little'))
        def map_page(page):
            banks[state['page']] = bytes(memory[0xc000:0x10000])
            m.set_memory_block(0xc000, banks.get(page, bytes(16384)))
            state['page'] = page
            maps.add(page)
        def output(port, value):
            assert port == 0x13af, hex(port)
            map_page(value)
        def payload(sector, count):
            return b''.join(struct.pack('<I', sector+i)*128 for i in range(count))
        def call(name):
            m.pc = sym[name]
            m.sp = 0xbff0
            m.set_memory_block(m.sp, struct.pack('<H', 0x7fff))
            for _ in range(30):
                m.ticks_to_stop = 100000
                m.run()
                if m.pc == 0x7fff:
                    return
                if m.pc == sym['FastFill']:
                    # SD stimulus only: execute actual FastBuffer selection, tags and hit path.
                    map_page(get('FastDataPage'))
                    put('WC_PAGE3', state['page'])
                    m.set_memory_block(get('FastFillDest', 2), payload(get('FastSector', 4), get('FastFillLeft')))
                    state['fills'] += 1
                    m.af = 0x0040
                    m.pc = int.from_bytes(bytes(memory[m.sp:m.sp+2]), 'little')
                    m.sp += 2
            raise AssertionError(('unbounded', name, hex(m.pc)))
        m.set_output_callback(output)
        m.set_breakpoint(0x7fff)
        m.set_breakpoint(sym['FastFill'])
        put('WC_PAGE3', 32)
        put('FastTablePage', last)
        call('FastMusicOpen')
        m.set_memory_block(sym['FastTags'], b'\xff'*512)
        put('FastRemaining', 0xffff, 2)
        slots = get('FastDataSlots')
        for block in list(range(slots*2))+[0, slots-1, slots, 119, 120, 126, 127]:
            sector = block*32
            put('FastSector', sector, 4)
            call('FastBuffer')
            assert m.f&0x40
            assert bytes(memory[0xc000:0x10000]) == payload(sector, 32)
            assert banks[0xf7] == sentinel and 0xf7 not in maps, 'WC paths overwritten'
            fills = state['fills']
            call('FastBuffer')
            assert state['fills'] == fills, 'immediate read must hit cache'
        assert set(range(128,256))-{0xf7} <= maps, 'upper pages must remain usable'
        proof.append(dict(last_index_page=last, data_slots=slots, fills=state['fills'],
                          mapped_pages=sorted(maps), wc_page_unchanged=True))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'result.json').write_text(json.dumps(proof, indent=2))
    print('PASS: cache fills, hits, replacement and complete F7 canary preserved')

if __name__ == '__main__':
    main()
