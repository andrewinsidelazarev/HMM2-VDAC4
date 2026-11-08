"""Codex: лимит DL и повторяемость параметров bitmap при повторном scanout."""
from pathlib import Path
import struct,sys,json
from ft_command_fifo import CommandFifo
FIELDS={1:0,7:1,8:2,0x28:3,0x29:4}
def execute(words,state):
    state=[row[:] for row in state];handle=0;primitive=1;vertices=[]
    for word in words:
        op=word>>24
        if op==5:handle=word&31
        elif op in FIELDS:state[handle][FIELDS[op]]=word
        elif op==0x1f:primitive=word&15
        elif word>>30 in (1,2) and primitive==1:
            assert word>>30==2,'BITMAPS VERTEX2F требует отдельной проверки HANDLE'
            h=(word>>7)&31
            assert all(v is not None for v in state[h]),('Неопределённая ручка',h,state[h])
            vertices.append((word,tuple(state[h])))
    return state,vertices

def main():
    folder=Path(sys.argv[1]);raw=(folder/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo()
    state=[[None]*5 for _ in range(32)];at=swaps=markers=maximum=0;timing=[]
    while at<len(raw):
        address,count=struct.unpack_from('<II',raw,at);at+=8;n=count*4;data=raw[at:at+n];at+=n;assert len(data)==n
        if address==0xffffffff:markers+=1;continue
        address&=0x7fffffff
        if address<0x300000:ram[address:address+n]=data
        elif address<0x302000:dl[address-0x300000:address-0x300000+n]=data
        elif address==0x302054:
            words=[]
            for word, in struct.iter_unpack('<I',dl):
                words.append(word)
                if word==0:break
            assert words[-1]==0 and len(words)<=2048
            after,first=execute(words,state);again,second=execute(words,after)
            assert first==second,('Повторный scanout меняет bitmap',swaps)
            assert after==again,('Параметры bitmap нестабильны',swaps)
            state=after;swaps+=1;maximum=max(maximum,len(words))
        elif address in (0x302034,0x302048,0x302070):timing.append(dict(after_frame=markers,address=hex(address),value=struct.unpack('<I',data)[0]))
        else:fifo.write(address,data,ram,dl)
    result=dict(frames=markers,swaps=swaps,max_words=maximum,repeated_scanout_equal=True,timing_writes=timing)
    (folder/'dl_state.json').write_text(json.dumps(result,indent=2));print(result)
if __name__=='__main__':main()
