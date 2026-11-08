"""Codex: полное RGB-сравнение двух FT-трасс, включая пиксели аппаратных теней."""
from pathlib import Path
import argparse,hashlib,json,struct
from check_fix5_ft import render
from ft_command_fifo import CommandFifo

def frames(folder):
    data=(folder/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192)
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    fifo=CommandFifo();at=0;rgb=None
    while at<len(data):
        address,n=struct.unpack_from('<II',data,at);at+=8;block=data[at:at+n*4];at+=len(block)
        if address==0xffffffff:
            yield struct.unpack('<I',block)[0],rgb
            continue
        address&=0x7fffffff
        if address<0x300000:ram[address:address+len(block)]=block
        elif address<0x302000:dl[address-0x300000:address-0x300000+len(block)]=block
        elif address==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,_=render(words,ram,handle_state=handles)
        else:fifo.write(address,block,ram,dl)

def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('reference',type=Path);a=p.parse_args()
    rows=[];bad=maximum=0
    from itertools import zip_longest
    for current,reference in zip_longest(frames(a.folder),frames(a.reference)):
        assert current is not None and reference is not None,'Разное число кадров'
        serial,rgb=current;other,expected=reference;assert serial==other
        count=int((rgb!=expected).any(axis=2).sum());bad+=bool(count);maximum=max(maximum,count)
        rows.append(dict(frame=serial,differences=count,rgb_sha256=hashlib.sha256(rgb.tobytes()).hexdigest()))
    result=dict(frames=len(rows),bad=bad,maximum_differences=maximum,reference=str(a.reference),rows=rows)
    (a.folder/'full_rgb_comparison.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='rows'}));return int(bad!=0)

if __name__=='__main__':raise SystemExit(main())
