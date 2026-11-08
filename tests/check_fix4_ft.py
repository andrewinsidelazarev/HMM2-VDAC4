"""Codex: исполняем именно RAM_G/DL; сравниваем физический экран с масштабированным эталоном."""
from pathlib import Path
import sys,struct,json,re,hashlib
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np
from PIL import Image

def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v

def render(words,ram):
    rgb=np.zeros((768,1024,3),dtype=np.uint8);protected=[]
    source=palette=stride=bh=bw=oh=0;a=e=256;tx=ty=0;fmt=4
    for v in words:
        op=v>>24
        if op==1:source=v&0x3fffff
        elif op==0x2a:palette=v&0x3fffff
        elif op==7:
            assert (v>>19)&31==16
            stride=(stride&3072)|((v>>9)&1023);bh=(bh&1536)|(v&511)
        elif op==0x28:stride=(stride&1023)|((v>>2&3)<<10);bh=(bh&511)|((v&3)<<9)
        elif op==8:
            assert not v&0x1c0000,'Codex: проверяется NEAREST/BORDER'
            bw=(bw&1536)|(v>>9&511);oh=(oh&1536)|(v&511)
        elif op==0x29:bw=(bw&511)|((v>>2&3)<<9);oh=(oh&511)|((v&3)<<9)
        elif op==0x15:a=signed(v&0x1ffff,17)
        elif op==0x19:e=signed(v&0x1ffff,17)
        elif op==0x17:tx=signed(v&0xffffff,24)
        elif op==0x1a:ty=signed(v&0xffffff,24)
        elif op==0x27:fmt=v&7
        elif v>>30==1:
            x=signed(v>>15&32767,15)//(1<<fmt);y=signed(v&32767,15)//(1<<fmt)
            assert 0<=x<x+bw<=1024 and 0<=y<y+oh<=768
            sx=(np.arange(bw)*a+tx)//256;sy=(np.arange(oh)*e+ty)//256
            assert min(sx)>=0 and max(sx)<stride and min(sy)>=0 and max(sy)<bh
            indexes=np.frombuffer(ram,dtype=np.uint8)[source+sy[:,None]*stride+sx]
            pal=np.frombuffer(ram,dtype=np.uint8,count=1024,offset=palette).reshape(256,4)
            rgba=pal[indexes];assert np.isin(rgba[:,:,3],[0,255]).all()
            dst=rgb[y:y+oh,x:x+bw];mask=rgba[:,:,3]==255;dst[mask]=rgba[:,:,:3][:,:,::-1][mask]
            protected.extend([(source,source+stride*bh),(palette,palette+1024)])
    return rgb,protected

def expected(folder,serial):
    idx=np.frombuffer((folder/f'frame_{serial:04}.idx').read_bytes(),dtype=np.uint8).reshape(480,640)
    pal=np.frombuffer((folder/f'frame_{serial:04}.pal').read_bytes(),dtype=np.uint8).reshape(256,3)
    return pal[idx[(np.arange(768)*5//8)[:,None],np.arange(1024)*5//8]]

def main():
    folder=Path(sys.argv[1]);original=Path(sys.argv[2]) if len(sys.argv)>2 else None
    raw=(folder/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192)
    protected=[];rgb=None;pos=traffic=swaps=0;rows=[];phases={}
    if (folder/'run.log').exists():
        log=(folder/'run.log').read_bytes();log=log.decode('utf-16' if log.startswith(b'\xff\xfe') else 'utf-8')
        phases=dict((int(n),p) for n,p in re.findall(r'frame=(\d+).*?phase=(\w+)',log))
    elif (folder/'esp.log').exists():
        # Codex: совместный стенд помечает фактическую сцену в том же потоке времени.
        log=(folder/'esp.log').read_text(encoding='utf-8')
        phases=dict((int(n),p) for n,p in re.findall(r'FRAME (\d+).*?phase=(\w+)',log))
    while pos<len(raw):
        address,count=struct.unpack_from('<II',raw,pos);pos+=8;n=count*4;data=raw[pos:pos+n];pos+=n;assert len(data)==n
        if address==0xffffffff:
            serial=struct.unpack('<I',data)[0];assert rgb is not None
            ref=expected(folder,serial);different=np.any(rgb!=ref,axis=2)
            if different.any():
                Image.fromarray(rgb).save(folder/f'failure_{serial}_ft.png');Image.fromarray(ref).save(folder/f'failure_{serial}_cpu.png')
                raise AssertionError((serial,int(different.sum()),'FT != CPU'))
            if original:assert np.array_equal(rgb,expected(original,serial)),(serial,'FT != original')
            rows.append(dict(frame=serial,phase=phases.get(serial,''),link_bytes=traffic,swaps=swaps));traffic=swaps=0
            if serial in [1,20,30,200,245,370]:Image.fromarray(rgb).save(folder/f'ft_{serial:04}.png')
        elif address<0x300000:
            assert address+n<=0x300000
            assert not any(address<b and address+n>a for a,b in protected),(hex(address),'visible overwrite')
            ram[address:address+n]=data;traffic+=n
        elif 0x300000<=address<0x302000:
            at=address-0x300000;assert at+n<=8192;dl[at:at+n]=data;traffic+=n
        elif address==0x302054:
            assert data==b'\2\0\0\0';words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,protected=render(words,ram);swaps+=1;traffic+=n
        else:raise AssertionError(hex(address))
    assert rows,'Codex: в следе нет границ кадров; пропущенный DLSWAP не равен пропущенному render'
    stats={}
    for phase in sorted(set(r['phase'] for r in rows)):
        rr=[r for r in rows if r['phase']==phase];bb=[r['link_bytes'] for r in rr]
        stats[phase]=dict(frames=len(rr),bytes_min=min(bb),bytes_max=max(bb),bytes_mean=sum(bb)/len(bb),link_only_fps=1330000*len(bb)/max(1,sum(bb)))
    report=dict(frames=len(rows),exact_ft=True,exact_original=bool(original),resolution=[1024,768],scaling='nearest floor(x*5/8)',no_visible_overwrites=True,scenes=stats,rows=rows)
    (folder/'ft_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2))
if __name__=='__main__':main()
