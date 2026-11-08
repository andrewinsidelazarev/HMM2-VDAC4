"""Codex: настоящий машинный код FastOpen/ScanNextCluster/FastResolve, фрагментированный хвост DAT.
Codex: SD заменён чтением секторов синтетической FAT; файлы и карта пользователя не открываются.
"""
from pathlib import Path
import hashlib, json, os, struct
from test_stage1_joint import BASE, base

out=BASE.parent/'fat_campaign';out.mkdir(exist_ok=True)
report=json.loads((BASE.parent/'update/campaign.json').read_text())
sym=base.symbols(BASE/'launcher/build/hmm2.sym')
binary=(BASE/'launcher/build/hmm2_z80.bin').read_bytes()
results=[]
for spc in (4,8,64,128):
    size=report['target_bytes'];count=(size+spc*512-1)//(spc*512)
    old_count=(report['base_bytes']+spc*512-1)//(spc*512)
    chain=[100+i for i in range(old_count)]+[1000000+(i*65537)%1000003 for i in range(count-old_count)]
    assert len(set(chain))==len(chain)
    fat={}
    for i,cluster in enumerate(chain):
        sector=fat.setdefault(cluster//128,bytearray(512))
        struct.pack_into('<I',sector,(cluster%128)*4,chain[i+1] if i+1<count else 0x0fffffff)
    m=base.z80.Z80Machine();m.set_memory_block(0x8000,binary)
    pages={};state=dict(page=32,reads=0,ticks=0)
    def put(name,value,n=4):m.set_memory_block(sym[name],value.to_bytes(n,'little'))
    def get(name,n=4):return int.from_bytes(bytes(m.memory[sym[name]:sym[name]+n]),'little')
    def port(port,value):
        assert port==0x13af,(port,value)
        pages[state['page']]=bytes(m.memory[0xc000:0x10000]);state['page']=value
        m.set_memory_block(0xc000,pages.get(value,bytes(16384)))
    m.set_output_callback(port)
    put('WC_PAGE3',32,1);put('ScanSectorsPerCluster',spc,1);put('ScanFatStart',2048);put('ScanDataStart',8192);put('ScanEnd',0x20000000)
    entry=bytearray(32);struct.pack_into('<H',entry,26,chain[0]);struct.pack_into('<I',entry,28,size)
    m.set_memory_block(0x3868,entry)
    for pc in (0x7fff,sym['ScanReadLba'],sym['FtTick'],sym['FastBad'],sym['FastError']):m.set_breakpoint(pc)
    def returned():
        m.pc=int.from_bytes(bytes(m.memory[m.sp:m.sp+2]),'little');m.sp+=2
    def call(name):
        m.pc=sym[name];m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0x7fff))
        for _ in range(1000000):
            m.ticks_to_stop=1000000;m.run()
            if m.pc==0x7fff:return
            if m.pc==sym['ScanReadLba']:
                sector=get('ScanLba')-2048
                assert sector in fat,sector
                m.set_memory_block(m.bc,bytes(fat[sector]));m.a=0;m.f=0x40;state['reads']+=1;returned()
            elif m.pc==sym['FtTick']:
                state['ticks']+=1;returned()
            elif m.pc in (sym['FastBad'],sym['FastError']):raise AssertionError(('FastOpen failed',hex(m.pc),m.a,get('ScanCluster'),get('FastCount'),get('FastBuildLeft'),get('FastTablePage',1),state))
        raise AssertionError('Превышен бюджет Z80')
    call('FastOpen')
    assert get('FastEnabled',1)==1 and get('FastSize')==size and get('FastCount')==count
    pages[state['page']]=bytes(m.memory[0xc000:0x10000])
    for i,cluster in enumerate(chain):
        assert struct.unpack_from('<I',pages[64+i//4096],(i%4096)*4)[0]==8192+(cluster-2)*spc,i
    reads=state['reads']
    selected=sorted({0,(size-1)//512,report['base_bytes']//512,report['catalog_offset']//512}|
                    {f['offset']//512 for f in report['files']}|
                    {i*4096*spc for i in range((count+4095)//4096)})
    for sector in selected:
        put('FastSector',sector);call('FastResolve')
        assert get('ScanLba')==8192+(chain[sector//spc]-2)*spc+sector%spc,sector
    assert state['reads']==reads
    results.append(dict(spc=spc,old_clusters=old_count,total_clusters=count,fragmented_appended_clusters=count-old_count,
                        all_index_entries_verified=count,lookups=len(selected),fat_sector_reads=reads,lookup_sd_reads=0,
                        last_index_bank=get('FastTablePage',1),music_banks=get('MusicPages',1)))
    print(results[-1],flush=True)
(out/'result.json').write_text(json.dumps(dict(dat_bytes=report['target_bytes'],z80_sha256=hashlib.sha256(binary).hexdigest(),
    model='Машинный код загрузчика; синтетические FAT32-секторы; FtTick пропущен',checks=results),indent=2,ensure_ascii=False),encoding='utf-8')
