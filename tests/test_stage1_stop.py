"""Codex: настоящий Z80 Stop: проверка ED, отказ отправки, ограниченное ожидание и M_SEL=0."""
from pathlib import Path
import json,sys,time,types,shutil
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from prepare_stage1 import BASE
sys.path.insert(0,str(ROOT/'tests'))
from test_stage1_joint import base
base.ROOT=BASE/'launcher'

class App:
    def __init__(self,answer):self.answer=answer;self.seen=[];self.done=False
    def __call__(self,command,payload):
        self.seen.append(command);assert command==0x6d
        return b'' if self.answer is None else base.frame(0xed,self.answer)

def main():
    dep=base.ROOT/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for name in ['fat32.bin','fat32-work.bin']:shutil.copy2(BASE/'launcher/build'/name,dep/name)
    shutil.copy2(BASE/'launcher/build/hmm2_z80.bin',BASE/'launcher/build/hmm2.bin')
    rows=[]
    for name,answer in [('ok',b'\1'),('negative',b'\0'),('empty',b''),('long',b'\1\1'),('timeout',None),('send_failed',None)]:
        app=App(answer);m=base.Machine(base.Disk(),app,'hmm2');z=m.machine
        m.ports.windows[2]=0x8000;m.ports.windows[32]=0xc000
        z.pc=m.symbols['ZiFi_Init'];z.sp=0xbff0;m.memory[z.sp:z.sp+2]=b'\0\x70';z.set_breakpoint(0x7000)
        for _ in range(500):
            z.ticks_to_stop=100000;ev=z.run()
            if ev&2 and z.pc==0x7000:break
        assert z.pc==0x7000
        if answer is None:
            # Codex: сокращаем только внутренний счётчик в копии ОЗУ; все 64 повтора остаются настоящими.
            at=m.symbols['WaitLong'];off=at+bytes(m.memory[at:at+40]).index(b'\x11\xff\xff')+1
            m.memory[off:off+2]=b'\4\0'
        if name=='send_failed':z.set_breakpoint(m.symbols['Proto_SendEmpty'])
        z.pc=m.symbols['Stop'];z.sp=0xbff0;m.vconfig=[4]
        for _ in range(3000):
            z.ticks_to_stop=100000;ev=z.run()
            if hasattr(m,'error'):raise m.error
            if ev&2 and z.pc==m.symbols.get('Proto_SendEmpty') and name=='send_failed':
                z.a=0xe2;z.f|=1;z.pc=int.from_bytes(m.memory[z.sp:z.sp+2],'little');z.sp+=2
            if ev&2 and z.pc==m.symbols['Halt']:break
        assert z.pc==m.symbols['Halt'] and m.vconfig==[4,0],(name,z.pc,m.vconfig)
        error=m.memory[m.symbols['BootError']]
        assert (error==0 and m.border[-1]==0) if name=='ok' else (error!=0 and m.border[-1]==2),(name,error,m.border)
        rows.append(dict(case=name,error=error,border=m.border,m_sel=m.vconfig,commands=app.seen,shortened_timeout=answer is None))
    out=BASE/'exit_checks';out.mkdir(exist_ok=True);(out/'launcher.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print(json.dumps(rows,indent=2))

if __name__=='__main__':main()
