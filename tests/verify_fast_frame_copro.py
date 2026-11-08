"""Codex: исполнить журнал приложения настоящим ft_copro.c BL616, сверить RAM_DL с независимой моделью."""
from pathlib import Path
import ctypes as C,struct,subprocess,sys,json,hashlib,os
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT
from ft_command_fifo import CommandFifo
SRC=Path(os.environ.get('HMM2_COPRO_SRC','E:/zx/Tang Nano 20k VDAC/VDAC4/Tang Nano 20k/BL616/copro'))
OUT=Path(os.environ.get('HMM2_COPRO_EVIDENCE','E:/zx/cache/tmp/hmm2_esp/fast_frame/copro'))
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    compat=OUT/'host_compat.h'
    compat.write_text('/* Codex: только подсказки оптимизации GCC заменяются для MSVC. Алгоритмы исходного сопроцессора не меняются. */\n#define __attribute__(x)\n#define __builtin_expect(x,y) (x)\n#define __builtin_fabsf fabsf\n',encoding='utf-8')
    sources=[SRC/'test/copro_sim.c',*[SRC/n for n in ['ft_copro.c','ft_inflate.c','ft_image.c','ft_3d.c']]]
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],SRC]
    objects=[]
    for source in sources:
        obj=OUT/(source.stem+'.obj');objects.append(obj)
        p=subprocess.run([str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/TC','/std:c11','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/FI'+str(compat),*['/I'+str(p) for p in inc],'/c',str(source),'/Fo'+str(obj)],cwd=OUT,capture_output=True)
        (OUT/(source.stem+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(p.stdout.decode(errors='replace')[-6000:])
    dll=OUT/'copro_actual.dll';libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
    p=subprocess.run([str(VC/'bin/Hostx64/x64/link.exe'),'/NOLOGO','/DLL','/OUT:'+str(dll),*map(str,objects),*['/LIBPATH:'+str(p) for p in libs]],cwd=OUT,capture_output=True)
    (OUT/'link.log').write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(p.stdout.decode(errors='replace'))
    lib=C.CDLL(str(dll));lib.sim_init.argtypes=[C.c_int];lib.sim_poll.argtypes=[C.c_int]
    lib.sim_write.argtypes=[C.c_uint32,C.c_void_p,C.c_uint32];lib.sim_read.argtypes=lib.sim_write.argtypes
    results=[]
    for file in map(Path,sys.argv[1:]):
        lib.sim_init(1);raw=file.read_bytes();at=0;frames=0;fifo=CommandFifo();ram=bytearray(0x300000);dlmem=bytearray(8192);received=C.create_string_buffer(8192)
        actual_ram=C.create_string_buffer(len(ram))
        while at<len(raw):
            addr,n=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+n*4];at+=n*4
            assert len(data)==n*4
            if addr==0xffffffff:continue
            addr&=0x7fffffff
            if addr<0x300000:ram[addr:addr+len(data)]=data
            elif 0x300000<=addr<0x302000:dlmem[addr-0x300000:addr-0x300000+len(data)]=data
            else:fifo.write(addr,data,ram,dlmem)
            lib.sim_write(addr,data,len(data))
            if addr==0x3020fc:
                target=struct.unpack('<I',data)[0];pointer=C.c_uint32()
                for _ in range(1000):
                    lib.sim_read(0x3020f8,C.byref(pointer),4)
                    assert not lib.sim_fault(),lib.sim_fault()
                    if pointer.value==target:break
                assert pointer.value==target,'CMD_READ timeout'
                lib.sim_read(0x300000,received,8192)
                assert received.raw==dlmem,'Разные RAM_DL после CMD_APPEND'
            if addr==0x302054:
                lib.sim_poll(10);lib.sim_dl_front(received)
                assert received.raw==dlmem,'Неверный показанный список после DLSWAP'
                lib.sim_read(0,actual_ram,len(ram))
                assert actual_ram.raw==ram,'Different RAM_G after coprocessor blits/inflate'
                frames+=1
        results.append(dict(file=str(file),swaps=frames,appends=fifo.appends,source_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},equal_dl=True,fault=lib.sim_fault()))
    (OUT/'result.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps([{k:v for k,v in r.items() if k!='source_sha256'} for r in results],ensure_ascii=False))
if __name__=='__main__':main()
