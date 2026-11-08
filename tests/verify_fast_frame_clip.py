"""Codex: отрицательные вершины, SCISSOR и приоритет курсора проверяются настоящим RTL."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys
from check_fix5_ft import render
ROOT=Path(__file__).resolve().parents[1]
SRC=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/Tang Nano 20k/Firmware')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fast_frame/rtl_clip')
BIN=Path('E:/zx/tools/rtl/ucrt64/bin')
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    # Codex: 8x8 карта с различимыми адресами текселей; курсор 2x2 содержит цвет и аппаратную тень.
    ram=bytearray(8192);ram[:64]=bytes(range(1,65));ram[64:68]=bytes([200,201,0,200])
    for i in range(1,65):struct.pack_into('<I',ram,4096+i*4,0xff000000|(i<<16)|(2*i<<8)|3*i)
    struct.pack_into('<I',ram,4096+200*4,0xffff0000);struct.pack_into('<I',ram,4096+201*4,0x66000000)
    words=[0x02111f2b,0x0f0000ff,0x26000007,0x100000ff,0x0b000014,0x04ffffff,0x27000004,
           0x15000100,0x16000000,0x17000000,0x18000000,0x19000100,0x1a000000,
           0x05000000,0x01000000,0x07000000|(16<<19)|(8<<9)|8,0x28000000,
           0x08000000|(8<<9)|8,0x29000000,0x2a001000,0x1f000001,
           0x1b000000|(2<<11)|1,0x1c000000|(4<<12)|5,
           0x2b000000|((-512*16)&0x1ffff),0x2c000000|((-512*16)&0x1ffff),
           0x80000000|(508<<21)|(509<<12),
           0x2b000000,0x2c000000,0x1b000000|(3<<11)|2,0x1c000000|(2<<12)|2,
           0x1f000009,0x04326496,0x0e000010,0x40000000|(3*16<<15)|(2*16),0x40000000|(5*16<<15)|(4*16),
           0x1b000000,0x1c2801e0,0x1f000001,0x04ffffff,0x01000040,
           0x07000000|(16<<19)|(2<<9)|2,0x08000000|(2<<9)|2,
           0x80000000|(3<<21)|(2<<12),0x21000000,0]
    (OUT/'dl.hex').write_text('\n'.join(f'{v:08x}' for v in words+[0]*(2048-len(words))))
    (OUT/'ramg.hex').write_text('\n'.join(f'{v:08x}' for v, in struct.iter_unpack('<I',ram)))
    files=[SRC/'rtl'/n for n in ('sdram_burst.sv','sd_bridge.sv','ft_mul9.sv','ft_rd_walk.sv','ft_rd_exec.sv','ft_rd_pix.sv','ft_render.sv')]+[SRC/'tb/sdram_model.sv',SRC/'tb/tb_ft_render.sv']
    env=dict(os.environ);env['PATH']=str(BIN)+os.pathsep+env['PATH']
    cmd=[str(BIN/'iverilog.exe'),'-g2012','-DSIMULATION','-I',str(SRC/'rtl'),'-s','tb_ft_render','-Ptb_ft_render.N_LINES=16','-Ptb_ft_render.SDF=1','-o',str(OUT/'clip.vvp'),*map(str,files)]
    p=subprocess.run(cmd,cwd=OUT,env=env,capture_output=True);(OUT/'compile.log').write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace')[-3000:])
    p=subprocess.run([str(BIN/'vvp.exe'),'-n','clip.vvp','+dl=dl.hex','+ramg=ramg.hex'],cwd=OUT,env=env,capture_output=True,timeout=180)
    (OUT/'run.log').write_bytes(p.stdout+p.stderr)
    assert p.returncode==0 and 'ТЕСТ ПРОЙДЕН' in p.stdout.decode('utf-8',errors='replace'),p.stdout[-2000:]
    rgb,shade=render(words,ram);different=0
    for y,line in enumerate((OUT/'ft_lines.hex').read_text().splitlines()):
        values=line.split();assert int(values[0])==y
        for x,raw in enumerate(values[1:641]):
            c=int(raw,16);actual=(c>>16&255,c>>8&255,c&255)
            if actual!=tuple(rgb[y,x]):different+=1
    assert different==0,different
    assert tuple(rgb[2,3])==(255,0,0) and tuple(rgb[2,4])==(30,60,90)
    result=dict(rtl_matches_model=True,differences=different,cursor_above_interface=True,shadow_above_interface=True,
                source_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    (OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'}))
if __name__=='__main__':main()
