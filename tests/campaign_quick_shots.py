"""Codex: кадры кампаний из трассы FT812, включая RGB565 роликов."""
from pathlib import Path
import sys,struct,re,copy
base=Path(__file__).parent;sys.path.insert(0,str(base))
from check_fix5_ft import render
from ft_command_fifo import CommandFifo
from test_ft_trace import png
folder=Path(sys.argv[1]);native=(folder/'frames.ft').exists();raw=(folder/('frames.ft' if native else 'menu.ft')).read_bytes()
selected=set(map(int,sys.argv[2:]));ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=0;frames=0;words=[]
handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)];before=copy.deepcopy(handles)
def shot(serial):
    if serial in selected:
        rgb,_=render(words,ram,handle_state=copy.deepcopy(before));png(folder/f'shot_{serial}.png',rgb.tobytes(),rgb.shape[1],rgb.shape[0])
while at<len(raw):
    address,n=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+n*4];at+=len(data)
    if address==0xffffffff:shot(struct.unpack('<I',data)[0]);continue
    address&=0x7fffffff
    if address<0x300000:ram[address:address+len(data)]=data
    elif address<0x302000:dl[address-0x300000:address-0x300000+len(data)]=data
    elif address==0x302054:
        words=[]
        for v, in struct.iter_unpack('<I',dl):
            words.append(v)
            if not v:break
        before=copy.deepcopy(handles);h=0
        for v in words:
            op=v>>24;d=handles[h]
            if op==5:h=v&31
            elif op==1:d['source']=v&0x3fffff
            elif op==7:d['format']=(v>>19)&31;d['stride']=(d['stride']&3072)|((v>>9)&1023);d['bh']=(d['bh']&1536)|(v&511)
            elif op==0x28:d['stride']=(d['stride']&1023)|((v>>2&3)<<10);d['bh']=(d['bh']&511)|((v&3)<<9)
            elif op==8:d['wrapx']=bool(v&0x80000);d['wrapy']=bool(v&0x40000);d['bw']=(d['bw']&1536)|(v>>9&511);d['oh']=(d['oh']&1536)|(v&511)
            elif op==0x29:d['bw']=(d['bw']&511)|((v>>2&3)<<9);d['oh']=(d['oh']&511)|((v&3)<<9)
        frames+=1
        if not native:shot(frames)
    elif fifo.write(address,data,ram,dl):pass
print('swaps',frames)
