"""Codex: исполняем собранный Z80 и настоящий FAT32; подменяем только носитель и ответы ESP."""
from pathlib import Path
import importlib.util,json,shutil,struct,sys,types
ROOT=Path(__file__).resolve().parents[1]
REF=Path('E:/zx/DOOM VDAC4')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/sd_bench')
spec=importlib.util.spec_from_file_location('doom_reference',REF/'tests/test_launcher.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);base.ROOT=OUT
picture=types.ModuleType('make_launcher_pic');picture.blocks=lambda:[];sys.modules['make_launcher_pic']=picture

class App(base.Esp):
    def __init__(self,data,elf):
        super().__init__(cached=False,paths={});self.elf=elf;self.source=data
        offsets=[0,1,511,512,4093,33553416,33553920,33554431,33554432,33554433,len(data)-1016,len(data)-17]
        offsets += [i*65521%(len(data)-1016) for i in range(64)]
        self.plan=[(0xE4,struct.pack('<IH',o,min(1016,len(data)-o)),('wad',data[o:o+1016])) for o in offsets]
    def __call__(self,c,p):
        if c==0x6C:
            assert p==b'HSD.ELF\0',p
            assert bytes(self.data)==self.elf
            self.started=True;return base.frame(0xEC,b'\0')
        if c==0x70:
            assert p==struct.pack('<I',len(self.source))+b'HEROES2.AGG\0',p
            return base.frame(0xF0,b'\1')+self.next_request()
        return super().__call__(c,p)

def fixture(data,elf,fragmented):
    disk=base.Disk(spc=8,partition=2048)
    def put(blob,first,breaks=False):
        count=(len(blob)+4095)//4096
        clusters=[first+i+(i//4000)*17 if breaks else first+i for i in range(count)]
        for i,c in enumerate(clusters):
            disk.set_fat(c,clusters[i+1] if i+1<count else 0x0fffffff)
            for j in range(8):disk.write(disk.data_start+(c-2)*8+j,blob[i*4096+j*512:i*4096+(j+1)*512].ljust(512,b'\0'))
        return first
    records=base.entry('HSD.ELF',put(elf,100),len(elf))+base.entry('HEROES2.AGG',put(data,1000,fragmented),len(data))
    disk.write(disk.data_start,records.ljust(512,b'\0'))
    return disk

def main():
    dep=OUT/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for n in ['fat32.bin','fat32-work.bin']:shutil.copy2(REF/'third_party/fat32/build'/n,dep/n)
    data=Path('E:/zx/HMM2/Assets/Original/DATA/HEROES2.AGG').read_bytes();elf=(OUT/'HSD.ELF').read_bytes()
    results=[]
    for dma,fragmented in [(False,False),(True,False),(True,True)]:
        app=App(data,elf);disk=fixture(data,elf,fragmented)
        m=base.Machine(disk,app,'hsd_dma' if dma else 'hsd_plain')
        m.ports.windows[2]=0x8000;m.ports.windows[32]=0xc000
        m.run()
        assert app.done and app.started and len(app.answers)==76,(m.border,m.memory[m.symbols['BootStage']],m.memory[m.symbols['BootError']],len(app.answers))
        assert not m.writes and not m.path_writes
        fast=bool(m.memory[m.symbols['FastEnabled']]);assert fast
        count=int.from_bytes(m.memory[m.symbols['FastCount']:m.symbols['FastCount']+2],'little')
        runs=sum(mode in (0x02,0x82) for mode,length in m.ports.dma_runs)
        assert (runs>0)==dma
        row=dict(dma=dma,fragmented=fragmented,bytes=len(data),answers=len(app.answers),fast=fast,extents=count,sd_reads=m.reads,dma_runs=runs,sd_writes=m.writes)
        results.append(row);print(row,flush=True)
    (ROOT/'Docs/evidence/sd_launcher_test.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if __name__=='__main__':main()
