"""Codex: при движении во время загрузки меняются только координаты хвоста DL."""
from pathlib import Path
import json,struct,sys
from ft_command_fifo import CommandFifo
p=Path(sys.argv[1]);raw=(p/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=0;frames={};writes=[];changes=[]
while at<len(raw):
    addr,n=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+n*4];at+=n*4
    if addr==0xffffffff:
        serial=struct.unpack('<I',data)[0];words=list(struct.unpack('<2048I',dl));words=words[:words.index(0)+1]
        frames[serial]=words
        if 4<=serial<=12:
            base=frames[3];assert len(words)==len(base)
            different=[i for i,(a,b) in enumerate(zip(words,base)) if a!=b]
            assert all(i>=len(words)-64 and base[i]>>24 in (0x2b,0x2c) and words[i]>>24==base[i]>>24 for i in different)
            assert all(a>=0x300000 or (len(d)==4 and int.from_bytes(d,'little')>>24 in (0x2b,0x2c)) for a,d in writes)
            changes.append(dict(frame=serial,coordinate_words=len(different),ram_g_bytes=sum(len(d) for a,d in writes if a<0x300000)))
        writes=[];continue
    addr&=0x7fffffff;writes.append((addr,data))
    if addr<0x300000:ram[addr:addr+len(data)]=data
    elif addr<0x302000:dl[addr-0x300000:addr-0x300000+len(data)]=data
    else:fifo.write(addr,data,ram,dl)
assert len(frames)==13 and len(changes)==9 and sum(x['coordinate_words']>0 for x in changes)>=8
result=dict(frames=len(frames),moves=changes,background_unchanged=True,no_texture_uploads=True)
(p/'cursor_trace_check.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
