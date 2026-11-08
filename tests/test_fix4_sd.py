"""Codex: фактические такты Z80 + явно заданные задержки DMA/SD, без карты пользователя."""
from pathlib import Path
import sys,struct,json,shutil
import test_stage1_joint as j
from fix4_z80 import install
base=j.base
OUT=j.BASE/'launcher'
shutil.copy2(OUT/'build/hmm2_z80.bin',OUT/'build/hmm2.bin')

class App(base.Esp):
    def __init__(self,data):
        super().__init__(cached=True,paths={});self.source=data;self.rows=[];self.packets={};self.got=bytearray()
        requests=[(0,32768),(32768,32768),(85950464,32768),(85983232,32768),(85950464,32768),
                  (len(data)-32767,32767),(4093,32768),(65536,32767)]
        self.plan=[(0xe4,struct.pack('<IH',at,n),('wad',data[at:at+n])) for at,n in requests]
    def next_request(self):
        self.got.clear();self.started_at=self.m.clock();self.start_reads=self.m.reads
        return super().next_request()
    def __call__(self,c,p):
        if c==0x6c:self.started=True;return base.frame(0xec,b'\0')
        if c==0x70:return base.frame(0xf0,b'\1')+self.next_request()
        if c in (0x31,0x71):
            seq,at,n=struct.unpack('<HHH',p[:6]);assert seq==self.seq and at==len(self.got) and n==len(p)-6,(c,seq,at,n)
            self.got+=p[6:];self.packets[c]=self.packets.get(c,0)+1
            expected=self.expected[1];assert self.got==expected[:len(self.got)]
            if len(self.got)<len(expected):return b''
            delta=self.m.clock()-self.started_at
            self.rows.append(dict(offset=struct.unpack('<I',self.last_payload[2:6])[0],bytes=n if False else len(expected),
                cycles=round(delta),ms=delta/14000,KiBs=len(expected)*14000000/delta/1024,sd_sectors=self.m.reads-self.start_reads))
            return base.frame(0xf1,b'\1')+self.next_request()
        return super().__call__(c,p)

class Machine(base.Machine):
    def __init__(self,disk,app):
        super().__init__(disk,app,'hmm2');install(self,base)
        self.link.core.from_capacity=16384
        self.ports.windows[2]=0x8000;self.ports.windows[32]=0xc000
        self.cycles=0;self.dma_cycles=0;self.budget=0;app.m=self
        original=self.ports._dma_launch
        def dma(mode):
            n=2*(self.ports.dma[0x26]+1)*(self.ports.dma[0x28]+1)
            self.dma_cycles+=n*(1 if mode==1 else 4)
            original(mode)
        self.ports._dma_launch=dma
    def clock(self):
        left=int.from_bytes(self.machine._StateBase__ticks_to_stop,'little')
        return self.cycles+max(0,self.budget-left)+self.dma_cycles+self.sd_stream_sectors*2100
    def run(self):
        m=self.machine;m.pc=self.symbols['Start']
        for _ in range(4000000):
            self.budget=10000;m.ticks_to_stop=self.budget;events=m.run()
            left=int.from_bytes(m._StateBase__ticks_to_stop,'little');self.cycles+=self.budget-left;self.budget=0
            if hasattr(self,'error'):raise self.error
            if self.app.done:return
            if not events&2:continue
            if m.pc==self.symbols['Halt']:raise AssertionError(('halt',self.memory[self.symbols['BootError']]))
            if m.pc in (base.READ,base.WRITE):
                assert m.pc==base.READ
                lba=m.de<<16|m.hl;self.reads+=1;self.read_lbas.add(lba);m.set_memory_block(m.bc,self.disk.read(lba))
                # Codex: штатный драйвер метаданных: INIR + CRC16 и 150 мкс ожидания сектора.
                self.cycles+=53000+2100
            elif m.pc not in (base.INIT,base.SYNC):raise AssertionError(hex(m.pc))
            m.bc,m.de,m.hl=0xa55a,0x1234,0x5678;m.a,m.f=0,64
            m.pc=int.from_bytes(self.memory[m.sp:m.sp+2],'little');m.sp+=2
        raise AssertionError('timeout')

def fixture(data,fragmented):
    disk=base.Disk(spc=8,partition=2048)
    def put(blob,first,stride):
        count=(len(blob)+4095)//4096
        for i in range(count):
            c=first+i*stride;disk.set_fat(c,c+stride if i+1<count else 0xfffffff)
            for s in range(8):disk.write(disk.data_start+(c-2)*8+s,blob[i*4096+s*512:i*4096+(s+1)*512].ljust(512,b'\0'))
        return first
    record=base.entry('HMM2.ELF',put(bytes(1024),100,1),1024)+base.entry('HMM2.DAT',put(data,1000,2 if fragmented else 1),len(data))
    disk.write(disk.data_start,record.ljust(512,b'\0'));return disk

def main():
    data=(j.BASE/'data/HMM2.DAT').read_bytes();results=[]
    for fragmented in (False,True):
        app=App(data);m=Machine(fixture(data,fragmented),app);m.run()
        assert (0x20af,2) in m.clock_writes and (0x2baf,6) in m.clock_writes
        assert len(app.rows)==8 and app.packets.get(0x31,0)>0 and app.packets.get(0x71,0)>0
        row=dict(fragmented=fragmented,clusters=22756,packet_counts=app.packets,rows=app.rows)
        results.append(row);print(json.dumps(row),flush=True)
    (j.ROOT/'Docs/evidence/stage1_fix4_sd.json').write_text(json.dumps(dict(
        model='Z80 actual instructions at 14MHz, SPI28 DMA 4T/byte, RAM DMA 1T/byte, SD wait 150us/sector; not physical board measurement',results=results),indent=2),encoding='utf-8')
if __name__=='__main__':main()
