"""Codex: такты прежнего линейного поиска; секторный ввод отдельно, без SD пользователя."""
from pathlib import Path
import sys,subprocess,struct,json,hashlib
sys.path.insert(0,'E:/zx/esp/fat32-venv/Lib/site-packages')
import z80
ROOT=Path(__file__).resolve().parents[1]
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix4/old_sd');OUT.mkdir(exist_ok=True)
SOURCE=Path(sys.argv[1]) if len(sys.argv)>1 else OUT.parent/'baseline/launcher/fastread.asm'

def main():
    asm='''    DEVICE ZXSPECTRUM128
    ORG #8000
    INCLUDE "'''+str(SOURCE).replace('\\','/')+'''"
ScanClusterLba:
ScanNextCluster:
ScanReadLba:
    xor a
    ret
ScanFatValid: db 0
ScanSector: db 0
ScanCluster: dd 0
ScanSectorsPerCluster: db 8
ScanLba: dd 0
ScanBuffer equ #C000
Fx: ds 28
    SAVEBIN "old.bin",#8000,$-#8000
'''
    (OUT/'old.asm').write_text(asm,encoding='utf-8')
    r=subprocess.run(['E:/zx/z80/zuma/sjasmplus.exe','--nologo','--sym=old.sym','old.asm'],cwd=OUT,capture_output=True)
    assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
    symbols={line.split(':')[0]:int(line.split()[-1],16) for line in (OUT/'old.sym').read_text().splitlines()}
    rows=[]
    for count in [1,128,256,512,640,1280]:
        m=z80.Z80Machine();m.set_memory_block(0x8000,(OUT/'old.bin').read_bytes())
        m.set_memory_block(symbols['FastCount'],struct.pack('<H',count))
        m.set_memory_block(symbols['FastSector'],struct.pack('<I',(count-1)*8))
        m.set_memory_block(0xc400,b''.join(struct.pack('<IHHI',i*8,8,0,1000+i*8) for i in range(count)))
        m.pc=symbols['FastReadSector'];m.sp=0x7ffe;m.set_memory_block(m.sp,b'\x77\x77');m.set_breakpoint(0x7777)
        ticks=0
        for _ in range(1000):
            m.ticks_to_stop=10000000;m.run();ticks+=10000000-int.from_bytes(m._StateBase__ticks_to_stop,'little')
            if m.pc==0x7777:break
        assert m.pc==0x7777 and m.a==0
        rows.append(dict(extents=count,cycles=ticks,ms_14mhz=ticks/14000))
    result=dict(source=str(SOURCE),sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),scope='FastReadSector, SD read stubbed; no DRAM wait states',rows=rows)
    (ROOT/'Docs/evidence/stage1_fix4_old_sd.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
