"""Codex: исполняем отказы MOD_QUERY/передачи/старта на Z80 и читаем показанный DL."""
from pathlib import Path
import sys,json,struct,hashlib,shutil
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,'E:/zx/esp/fat32-venv/Lib/site-packages')
text=(ROOT/'tests/test_sd_launcher.py').read_text(encoding='utf-8')
text=text.replace('stage0b/sd_bench','stage1/launcher').replace('HSD.ELF','HMM2.ELF').replace('HEROES2.AGG','HMM2.DAT')
ns={'__file__':str(ROOT/'tests/test_sd_launcher.py'),'__name__':'fixture'};exec(compile(text,__file__,'exec'),ns)
base,fixture,App,OUT=[ns[k] for k in ['base','fixture','App','OUT']]
from fix4_z80 import patched_machine
base.Machine=patched_machine(base)
from test_ft_trace import png

def screen(m,path):
    rgb=bytearray(bytes([10,16,32])*(1024*768));rows={};source=0
    alphabet='ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-0123456789ABCDEF'
    for w in m.ft.dls[-1]:
        if w>>24==1:source=w&0x3fffff
        if w>>30!=1:continue
        x,y=(w>>15&32767)//16,(w&32767)//16
        assert x>=128 and x<896 and y>=240 and y<480,(x,y)
        rows.setdefault(y,{})[(x-128)//16]=alphabet[source//8]
        for yy in range(16):
            bits=m.ft.mem.get(source+yy//2,0)
            for xx in range(16):
                if bits&(128>>(xx//2)):
                    at=((y+yy)*1024+x+xx)*3;rgb[at:at+3]=b'\xff'*3
    lines=[''.join(row.get(i,' ') for i in range(max(row)+1)) for _,row in sorted(rows.items())]
    png(path,bytes(rgb),1024,768);return lines

def main():
    # Codex: исполняем ту же пару FAT32, которая включена в проверяемый SPG.
    dep=OUT/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for name in ['fat32.bin','fat32-work.bin']:
        shutil.copy2(OUT/'build'/name,dep/name)
    elf=(OUT.parent/'module/HMM2.ELF').read_bytes();data=bytes(8192)
    shutil.copy2(OUT/'build/hmm2_z80.bin',OUT/'build/hmm2.bin')
    rows=[]
    cases=[('query_full',0x69,2,7),('query_invalid',0x69,3,7),('put_rejected',0x6a,0,8),('commit_rejected',0x6b,0,9),('start_rejected',0x6c,1,10),('start_memory',0x6c,254,10),('config_rejected',0x70,0,11)]
    cases += [('timeout_'+name,cmd,None,stage) for name,cmd,_,stage in cases if name in ['query_full','put_rejected','commit_rejected','start_rejected','config_rejected']]
    for label,cmd,status,stage in cases:
        class Fault(App):
            def __init__(self,data,elf):
                base.Esp.__init__(self,cached=False,paths={});self.elf=elf;self.source=data;self.plan=[]
            def __call__(self,c,p):
                if c==0x2a and p[4:].startswith(b'HMM2-E'):
                    self.diagnostic=p[4:].rstrip(b'\0').decode();return base.frame(0xaa,bytes(5))
                if c==cmd:return b'' if status is None else base.frame(c|128,bytes([status])+bytes(4 if c==0x69 else 0))
                if c==0x69:
                    size=struct.unpack_from('<I',p,1)[0]
                    fits=((size+4095)//4096+1)*4096+8192<=1703936
                    return base.frame(0xe9,bytes([1 if fits else 2])+struct.pack('<I',1703936-8192))
                if c==0x6b:
                    assert bytes(self.data)==elf;self.committed=True;return base.frame(0xeb,b'\1')
                return super().__call__(c,p)
        app=Fault(data,elf);disk=fixture(data,elf,False);m=base.Machine(disk,app,'hmm2')
        m.ports.windows[2]=0x8000;m.ports.windows[32]=0xc000
        if status is None:
            # Codex: только длительность ожидания сокращена в ОЗУ стенда; ветви отказа штатные.
            at=m.symbols['WaitLong'];code=bytes(m.memory[at:at+40]);patch=code.index(b'\x11\xff\xff')+at+1
            m.memory[patch:patch+2]=b'\x40\0'
        m.run();got_stage=m.memory[m.symbols['BootStage']];error=m.memory[m.symbols['BootError']]
        assert m.machine.pc==m.symbols['Halt'] and got_stage==stage,(label,got_stage,error)
        assert error==(0xe0 if status is None else status),(label,error)
        diagnostic=f'HMM2-E{stage:02X}{error:02X}';assert app.diagnostic==diagnostic
        lines=screen(m,ROOT/'Docs/evidence'/f'stage1_fix1_{label}.png')
        assert lines[0]=='HMM2: START FAILED' and f'STAGE {stage:02X} CODE {error:02X}' in lines,(label,lines)
        if label=='query_full':assert 'MODULE CACHE FULL' in lines
        if label=='query_invalid':assert 'INVALID MODULE QUERY' in lines
        if status is None:assert 'ESP RESPONSE TIMEOUT' in lines
        assert not m.writes and not m.path_writes
        rows.append(dict(case=label,stage=stage,error=error,screen=lines,esp_log_key=diagnostic,timeout_accelerated=status is None));print(rows[-1],flush=True)
    result=dict(spg_sha256=hashlib.sha256((OUT/'_HMM2.SPG').read_bytes()).hexdigest(),elf_sha256=hashlib.sha256(elf).hexdigest(),cases=rows)
    (ROOT/'Docs/evidence/stage1_fix1_launcher.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
