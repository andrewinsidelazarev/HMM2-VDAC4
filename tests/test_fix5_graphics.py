"""Codex: исполняем E6 на Z80: DAT -> кольцо Evo -> DMA SPI FT812, с проверкой каждого байта."""
from pathlib import Path
import json,struct,shutil
import test_fix4_sd as sd
base=sd.base

class App(base.Esp):
    def __init__(self,data):
        super().__init__(cached=True,paths={});self.source=data;self.rows=[]
        self.plan=[(0xe4,struct.pack('<IH',0,32),('wad',data[:32]))]
        self.cases=[(0,0x10000,32768,0),(32768,0xff800,32768,0),
                    (0,0x2f8000,32768,0),(512,0x160000,512,0),
                    (1,0x10000,512,1),(0,0x10000,513,1),(0,0x2fffff,512,1),
                    (0,0xffffff00,512,1),(0,0x10000,0,1)]
    def __call__(self,c,p):
        if c==0x6c:self.started=True;return base.frame(0xec,b'\0')
        if c==0x70:return base.frame(0xf0,b'\1')+self.next_request()
        if c==0x71:
            assert p[4:]==self.source[:32]
            return base.frame(0xf1,b'\1')+self.graphics()
        if c==0x77:
            seq,status=struct.unpack('<HB',p);at,dst,n,error=self.case
            assert seq==self.seq and status==error,(self.case,p)
            if not error:
                got=bytes(self.m.ft.mem.get(dst+i,0) for i in range(n))
                assert got==self.source[at:at+n],(self.case,got[:32])
            cycles=self.m.clock()-self.started_at
            self.rows.append(dict(offset=at,address=dst,bytes=n,status=status,cycles=cycles,
                KiBs=n*14000000/cycles/1024 if not error else None))
            return base.frame(0xf7,b'\1')+self.graphics()
        return super().__call__(c,p)
    def graphics(self):
        if not self.cases:self.done=True;return b''
        self.case=self.cases.pop(0);at,dst,n,error=self.case;self.seq+=1
        self.started_at=self.m.clock()
        return base.frame(0xe6,struct.pack('<HIIH',self.seq,at,dst,n))

def main():
    out=sd.OUT;dep=out/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for name in ['fat32.bin','fat32-work.bin']:shutil.copy2(out/'build'/name,dep/name)
    data=bytes((i*37+(i>>9)*13)&255 for i in range(96*1024))
    results=[]
    for fragmented in (False,True):
        app=App(data);m=sd.Machine(sd.fixture(data,fragmented),app);m.run()
        assert len(app.rows)==9 and not m.writes and not m.path_writes
        results.append(dict(fragmented=fragmented,rows=app.rows));print(results[-1],flush=True)
    (sd.j.ROOT/'Docs/evidence/stage1_fix5_graphics_dma.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if __name__=='__main__':main()
