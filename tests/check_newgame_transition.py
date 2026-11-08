"""Codex: исходный вход N/S/Enter; три дополнительных кадра WAIT исключаются из сравнения."""
from pathlib import Path
import struct,sys,json,hashlib
sys.path.insert(0,"E:/zx/cache/tmp/hmm2_esp/fix4/python")
import numpy as np
from PIL import Image
from check_fix5_ft import render
from ft_command_fifo import CommandFifo

def main():
    folder=Path(sys.argv[1]);reference=Path(sys.argv[2]);raw=(folder/'frames.ft').read_bytes()
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=0;rows=[];rgb=shadow=None;worst=0
    while at<len(raw):
        address,count=struct.unpack_from('<II',raw,at);at+=8;n=count*4;data=raw[at:at+n];at+=n
        if address==0xffffffff:
            frame=struct.unpack('<I',data)[0]
            if frame<62:continue
            other=frame-3
            idx=np.frombuffer((reference/f'frame_{other:04}.idx').read_bytes(),dtype=np.uint8).reshape(480,640)
            pal=np.frombuffer((reference/f'frame_{other:04}.pal').read_bytes(),dtype=np.uint8).reshape(256,3)
            expected=pal[idx];bad=int((np.any(rgb!=expected,axis=2)&~shadow).sum());worst=max(worst,bad)
            rows.append(dict(frame=frame,reference=other,differences=bad,rgb_sha256=hashlib.sha256(rgb.tobytes()).hexdigest()))
            if frame in (62,66,100,200):Image.fromarray(rgb).save(folder/f'newgame_{frame}.png')
            if bad and sum(x['differences']>0 for x in rows)<=3:
                Image.fromarray(rgb).save(folder/f'newgame_fail_{frame}_gpu.png');Image.fromarray(expected).save(folder/f'newgame_fail_{frame}_cpu.png')
            continue
        address&=0x7fffffff
        if address<0x300000:ram[address:address+n]=data
        elif address<0x302000:dl[address-0x300000:address-0x300000+n]=data
        elif address==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,shadow=render(words,ram,handle_state=handles)
        elif fifo.write(address,data,ram,dl):pass
    result=dict(frames=len(rows),bad=sum(x['differences']>0 for x in rows),worst=worst,reference=str(reference),rows=rows)
    (folder/'comparison.json').write_text(json.dumps(result,indent=2));print({k:v for k,v in result.items() if k!='rows'},flush=True)
    return int(result['bad']!=0)
if __name__=='__main__':raise SystemExit(main())
