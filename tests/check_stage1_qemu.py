"""Codex: разбор реальных ft_write из Xtensa, проверка DL и сравнение пикселей с хостом."""
from pathlib import Path
import struct,json,hashlib
from test_ft_trace import raster,png
from check_fix4_ft import render,expected
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1');ROOT=Path(__file__).resolve().parents[1]
def main():
    folder=BASE/'qemu_stage1';data=(folder/'menu.ft').read_bytes();ram=bytearray(3*1024*1024);dl=bytearray(8192);pos=0;frames=[];written=0
    ref={}
    for p in (BASE/'runs/menu64').glob('frame_*.idx'):
        rgb=expected(p.parent,int(p.stem.split('_')[1])).tobytes();ref.setdefault(hashlib.sha256(rgb).hexdigest(),[]).append(p.name)
    while pos<len(data):
        addr,count=struct.unpack_from('<II',data,pos);pos+=8;raw=data[pos:pos+count*4];pos+=len(raw);assert len(raw)==count*4;written+=len(raw)
        if addr<0x300000:assert addr+len(raw)<=len(ram);ram[addr:addr+len(raw)]=raw
        elif 0x300000<=addr<0x302000:
            at=addr-0x300000;assert at+len(raw)<=8192;dl[at:at+len(raw)]=raw
        elif addr==0x302054:
            assert raw==b'\2\0\0\0';words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,_=render(words,ram);crop=rgb.tobytes();h=hashlib.sha256(crop).hexdigest()
            i=len(frames)+1;png(folder/f'frame_{i:04}.png',crop,1024,768)
            frames.append(dict(frame=i,sha256=h,exact_host_frames=ref.get(h,[]),link_bytes=written));written=0
        else:raise AssertionError(hex(addr))
    assert len(frames)==30;result=dict(frames=frames,exact=sum(bool(f['exact_host_frames']) for f in frames),total=30,
        elf_sha256=hashlib.sha256((BASE/'module/HMM2.ELF').read_bytes()).hexdigest(),resolution=[1024,768],trace_sha256=hashlib.sha256(data).hexdigest())
    (folder/'compare.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='frames'}))
    return 0 if result['exact']==30 else 1
if __name__=='__main__':raise SystemExit(main())
