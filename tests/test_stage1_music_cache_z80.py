"""Codex: машинный код Z80: независимое окно музыки, границы таблицы FAT и приоритет запроса."""
from pathlib import Path
import json,os,struct
from test_stage1_joint import BASE,base

OUT=Path(os.environ.get('HMM2_MUSIC_Z80_OUT','E:/zx/cache/tmp/hmm2_esp/integration_audio/music_cache56_z80'))
def main():
    sym=base.symbols(BASE/'launcher/build/hmm2.sym');proof=[]
    ring_pages=[p for p in range(128,256) if p!=0xf7]
    assert len(ring_pages)==127
    def fresh():
        m=base.z80.Z80Machine();m.set_memory_block(0x8000,(BASE/'launcher/build/hmm2_z80.bin').read_bytes());return m
    def put(m,k,v,n=4):m.set_memory_block(sym[k],int(v).to_bytes(n,'little'))
    def get(m,k,n=4):return int.from_bytes(bytes(m.memory[sym[k]:sym[k]+n]),'little')
    def call(m,k):
        m.pc=sym[k];m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0x7fff));m.set_breakpoint(0x7fff)
        for _ in range(10):
            m.ticks_to_stop=100000;m.run()
            if m.pc==0x7fff:return
        raise AssertionError(('unbounded',k,hex(m.pc)))
    for last,expected in [(64,122),(81,105),(96,90),(120,66),(127,59)]:
        m=fresh();put(m,'FastTablePage',last,1);call(m,'FastMusicOpen');pages=get(m,'MusicPages',1)
        assert pages==expected,(last,pages)
        assigned=list(bytes(m.memory[sym['MusicPageMap']:sym['MusicPageMap']+pages]))
        protected={2,14,15,32,56}|set(range(64,last+1))|set(range(128,256))
        assert len(assigned)==len(set(assigned)) and set(assigned)==set(range(256))-protected
        slots=get(m,'MusicSlots',1);data_slots=get(m,'FastDataSlots',1)
        assert data_slots==len(ring_pages)+pages-slots
        data_pages=ring_pages+assigned[slots:]
        # Execute every data slot, including F6 -> F8, FF -> extra bank, wrap and memoized hits.
        for block in list(range(data_slots*2))+[0x10000,0x1234567,0x7ffffff]:
            put(m,'FastBase',block*32);call(m,'FastMusicSelect')
            index=block%data_slots;page=get(m,'FastDataPage',1)
            assert page==data_pages[index] and page!=0xf7,(last,block,page)
            assert get(m,'FastRing',1)==(index if index<127 else index-127+slots)
            table=sym['FastTags'] if index<127 else sym['MusicTags']
            assert m.de==table
            call(m,'FastMusicSelect');assert get(m,'FastDataPage',1)==page and m.de==table
        assert len(data_pages)==len(set(data_pages)) and not set(data_pages)&set(assigned[:slots])
        proof.append(dict(last_index_page=last,music_pages=pages,music_bytes=pages*16384,all_4mib_accounted=True,reserved_pages=len(protected),pages=assigned))
    m=fresh();put(m,'FastTablePage',81,1);call(m,'FastMusicOpen');put(m,'FastEnabled',1,1);put(m,'FastSize',291_675_244)
    origin=0x0FFF01AB;size=3679247;m.set_memory_block(sym['ProtoBuf'],struct.pack('<II',origin,size));put(m,'ProtoRxLen',8,2);call(m,'ServeMusic')
    start=origin//16384*32;assert get(m,'MusicActive',1)==1 and get(m,'MusicStart')==start and get(m,'MusicEnd')==(origin+size+511)//512
    for sector in [start+k*32 for k in (0,1,10,104,105,106,209,210,224)]+[0]:
        put(m,'FastBase',sector);put(m,'FastRing',(sector>>5)&127,1);call(m,'FastMusicSelect');page=get(m,'FastDataPage',1)
        assigned=list(bytes(m.memory[sym['MusicPageMap']:sym['MusicPageMap']+105]))
        # Клод: музыке 64 слота, данным — остальные банки: блок файла по модулю числа слотов данных.
        slots=get(m,'FastDataSlots',1);assert slots==127+105-64
        music=sector>=start and sector<get(m,'MusicEnd')
        data=(sector>>5)%slots
        assert page==(assigned[((sector-start)>>5)%64] if music else ring_pages[data] if data<127 else assigned[data-127+64])
    # Music may only use its own slots, even after a full data-cache cycle.
    for block in range(64*2):
        put(m,'FastBase',start+block*32);call(m,'FastMusicSelect')
        assert get(m,'FastDataPage',1)==assigned[block%64]
    put(m,'FastSector',start+65);call(m,'FastMusicNotice');assert get(m,'MusicDemand')==start+64
    put(m,'MusicDemand',start);put(m,'MusicCursor',start);put(m,'WC_PAGE3',56,1)
    # Клод: биты 0..3 метки — прочитанные строки 4 КиБ; слот в кэше — все четыре.
    index=0;m.set_memory_block(sym['MusicTags']+index*4,struct.pack('<I',start|15));ports=[]
    m.set_output_callback(lambda p,v:ports.append((p,v)));call(m,'FastIdle')
    assert get(m,'MusicCursor')==start+32 and get(m,'WC_PAGE3',1)==56
    assert ports and all(p==0x13af for p,v in ports),ports
    for hint,length in [(struct.pack('<II',origin,0),8),(struct.pack('<II',291_600_000,200000),8),(struct.pack('<II',0xffffff00,0x1000),8),(struct.pack('<II',origin,size),7)]:
        m.set_memory_block(sym['ProtoBuf'],hint);put(m,'ProtoRxLen',length,2);call(m,'ServeMusic');assert get(m,'MusicActive',1)==0
    result=dict(layout=proof,range_checks=True,graphics_ring_bytes=len(ring_pages)*16384,reserved_wc_path_page=0xf7,all_data_slots_checked=True,cached_idle_advances=True,working_page_restored=True,invalid_hints_rejected=True,code_end=sym['CodeEnd'],graphics_code_end=sym['GraphicsCodeEnd'])
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'result.json').write_text(json.dumps(result,indent=2));print('PASS: music/data slots, F7 exclusion, boundaries, wrap, memoization and range checks')
if __name__=='__main__':main()
