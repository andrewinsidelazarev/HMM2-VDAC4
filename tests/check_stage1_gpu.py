"""Codex: независимая модель DL проверяет каждый индекс, палитру и отсутствие записи в показанные текстуры."""
from pathlib import Path
import struct,json,re,sys,statistics
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def render(dl,ram):
    frame=bytearray(640*480);source=palette=stride=bh=bw=oh=0;a=256;tx=ty=0;fmt=4;protected=[];gpu=0
    for v in dl:
        op=v>>24
        if op==1:source=v&0x3fffff
        elif op==0x2a:palette=v&0x3fffff
        elif op==7:assert (v>>19)&31==16;stride=(stride&3072)|((v>>9)&1023);bh=(bh&1536)|(v&511)
        elif op==0x28:stride=(stride&1023)|((v>>2&3)<<10);bh=(bh&511)|((v&3)<<9)
        elif op==8:bw=(bw&1536)|(v>>9&511);oh=(oh&1536)|(v&511)
        elif op==0x29:bw=(bw&511)|((v>>2&3)<<9);oh=(oh&511)|((v&3)<<9)
        elif op==0x15:a=signed(v&0x1ffff,17);assert a in [256,-256]
        elif op==0x17:tx=signed(v&0xffffff,24)
        elif op==0x1a:ty=signed(v&0xffffff,24)
        elif op==0x27:fmt=v&7
        elif v>>30==1:
            x=signed(v>>15&32767,15)//(1<<fmt)-192;y=signed(v&32767,15)//(1<<fmt)-144
            assert 0<=x<x+bw<=640 and 0<=y<y+oh<=480
            assert source+stride*bh<=len(ram)
            for yy in range(oh):
                sy=yy+ty//256;assert 0<=sy<bh;sx=tx//256;at=source+sy*stride+sx
                row=ram[at:at+bw] if a==256 else ram[at-bw+1:at+1][::-1]
                assert len(row)==bw;frame[(y+yy)*640+x:(y+yy)*640+x+bw]=row
            protected.extend([(source,source+stride*bh),(palette,palette+1024)])
            gpu+=source>=0x100000
    pal=b''.join(bytes(reversed(ram[palette+i*4:palette+i*4+3])) for i in range(256))
    return bytes(frame),pal,protected,gpu
def legacy_main():
    folder=BASE/'runs/scene_packed';trace=folder/'frames.ft'
    if len(sys.argv)>1:trace=Path(sys.argv[1]);folder=trace.parent
    data=trace.read_bytes();ram=bytearray(3*1024*1024);dl=[];pos=serial=traffic=0;protected=[];result=[];matched=0;high=0
    phases={int(n):s for n,s in re.findall(r'HMM2 frame=(\d+).*?phase=(\w+)',(folder/'run.log').read_text(encoding='utf-8'))} if (folder/'run.log').exists() else {}
    while pos<len(data):
        address,count=struct.unpack_from('<II',data,pos);pos+=8;raw=data[pos:pos+count*4];pos+=len(raw);traffic+=len(raw);assert len(raw)==count*4
        if address<0x300000:
            assert address+len(raw)<=0x300000
            assert not any(address<end and address+len(raw)>begin for begin,end in protected),'Codex: перезапись показанного изображения'
            ram[address:address+len(raw)]=raw;high=max(high,address+len(raw))
        elif address==0x300000:assert len(raw)<=8192;dl=list(struct.unpack('<'+'I'*count,raw))
        elif address==0x302054:
            assert raw==b'\2\0\0\0';serial+=1;idx,pal,protected,gpu=render(dl,ram)
            ref=folder/f'frame_{serial:04}.idx';equal=None
            if ref.exists():equal=idx==ref.read_bytes() and pal==ref.with_suffix('.pal').read_bytes();matched+=bool(equal);assert equal,(serial,'Codex: пиксели DL отличаются от оригинала')
            result.append(dict(frame=serial,phase=phases.get(serial,''),link_bytes=traffic,gpu_tiles=gpu,dl_bytes=len(dl)*4,exact=equal));traffic=0
        else:raise AssertionError(hex(address))
    stats={}
    for phase in set(x['phase'] for x in result):
        rows=[x for x in result if x['phase']==phase];b=[x['link_bytes'] for x in rows]
        stats[phase]=dict(frames=len(rows),bytes_min=min(b),bytes_max=max(b),bytes_median=statistics.median(b),bytes_total=sum(b),gpu_tiles_max=max(x['gpu_tiles'] for x in rows))
    report=dict(frames=serial,matched=matched,ram_high_water=high,no_visible_overwrites=True,scenes=stats)
    (folder/'gpu_comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2))
def main():
    # Codex: прежняя команда проверки теперь проверяет полный экран и альфа-палитры fix4.
    from check_fix4_ft import main as check
    folder=Path(sys.argv[1]).parent if len(sys.argv)>1 else BASE/'runs/scene_packed'
    sys.argv=[sys.argv[0],str(folder),str(BASE/'runs/scene64')];check()
    report=json.loads((folder/'ft_check.json').read_text());report['matched']=report['frames']
    (folder/'gpu_comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if __name__=='__main__':main()
