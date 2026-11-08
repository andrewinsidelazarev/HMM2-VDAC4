"""Codex: все последовательные кадры участка FT, без пропусков, с длительностями из QEMU."""
from pathlib import Path
import argparse,json,re,struct
from check_fix5_ft import Image,render
from ft_command_fifo import CommandFifo

def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('first',type=int);p.add_argument('last',type=int);a=p.parse_args()
    times={int(n):int(t) for n,t in re.findall(r'кадр=(\d+) T=(\d+)',(a.folder/'qemu.log').read_text(encoding='utf8'))}
    raw=(a.folder/'menu.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=serial=0
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)];images=[];durations=[]
    while at<len(raw) and serial<a.last:
        address,n=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+n*4];at+=len(data);address&=0x7fffffff
        if address<0x300000:ram[address:address+len(data)]=data
        elif address<0x302000:dl[address-0x300000:address-0x300000+len(data)]=data
        elif address==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            serial+=1
            if serial>=a.first:
                rgb,_=render(words,ram,handle_state=handles);images.append(Image.fromarray(rgb))
                durations.append(max(1,times.get(serial+1,times[serial]+25)-times[serial]))
            else:
                h=0
                for v in words:
                    op=v>>24;d=handles[h]
                    if op==5:h=v&31
                    elif op==1:d['source']=v&0x3fffff
                    elif op==7:d['stride']=(d['stride']&3072)|((v>>9)&1023);d['bh']=(d['bh']&1536)|(v&511)
                    elif op==0x28:d['stride']=(d['stride']&1023)|((v>>2&3)<<10);d['bh']=(d['bh']&511)|((v&3)<<9)
                    elif op==8:d['wrapx']=bool(v&0x80000);d['wrapy']=bool(v&0x40000);d['bw']=(d['bw']&1536)|(v>>9&511);d['oh']=(d['oh']&1536)|(v&511)
                    elif op==0x29:d['bw']=(d['bw']&511)|((v>>2&3)<<9);d['oh']=(d['oh']&511)|((v&3)<<9)
        elif fifo.write(address,data,ram,dl):pass
    assert len(images)==a.last-a.first+1
    name=f'walk_{a.first}_{a.last}'
    images[0].save(a.folder/(name+'.webp'),save_all=True,append_images=images[1:],duration=durations,loop=0,lossless=True,method=4)
    images[0].save(a.folder/(name+'_first.png'));images[-1].save(a.folder/(name+'_last.png'))
    (a.folder/(name+'.json')).write_text(json.dumps(dict(first=a.first,last=a.last,frames=len(images),duration_ms=sum(durations),durations_ms=durations),indent=2))
    print(name,len(images),sum(durations),flush=True)

if __name__=='__main__':main()
