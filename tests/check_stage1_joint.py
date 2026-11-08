"""Codex: выпуск требует совместного меню и карты, полного следа SD и точных пикселей DL."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys,zipfile,shutil
from check_stage1_gpu import render
from test_ft_trace import png
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1');EV=ROOT/'Docs/evidence'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rgb(idx,pal):return b''.join(pal[i*3:i*3+3] for i in idx)

def examine(label):
    out=BASE/'joint'/label;r=json.loads((out/'result.json').read_text())
    assert r['elf_sha256']==sha(BASE/'module/HMM2.ELF') and r['spg_sha256']==sha(BASE/'launcher/_HMM2.SPG')
    assert r['world_ready'] and r['frames']>=245 and r['exit']==-999 and r['stage']==12 and not r['code']
    for p,h in r['native']['sources'].items():assert sha(Path(p))==h,p
    log=(out/'esp.log').read_text(encoding='utf-8');assert 'FAIL ' not in log and 'reject ' not in log and 'MAIN_EXIT' not in log
    starts=re.findall(r'HMM2 SD >([A-F0-9]+) op=(\d+) (\S+) seq=(\d+) off=(\d+) len=(\d+)',log)
    ends=re.findall(r'HMM2 SD <([A-F0-9]+) seq=(\d+) result=(-?\d+) wait=(\d+) rx=([0-9A-F]+)/(\d+)/(\d+)/([0-9A-F]+)',log)
    assert len(starts)==len(ends) and len(starts)>5000
    for s,e in zip(starts,ends):assert s[0]==e[0] and s[3]==e[1]==e[5] and int(e[3])<10000
    assert starts[0]==('E4','1','HMM2.DAT','1','0','1016')
    assert any(s[0]=='E5' and s[1]=='3' and s[2]=='fheroes2.key' for s in starts)
    assert r['commands']['76']>0 and r['spi_stats'][2]==0 and r['spi_stats'][3]==0
    assert r['spi_stats'][11]<=2048 and r['spi_stats'][12]<4096
    data=(out/'frames.ft').read_bytes();ram=bytearray(3*1024*1024);at=serial=0;protected=[];dl=[]
    while at<len(data):
        addr,n=struct.unpack_from('<II',data,at);at+=8;raw=data[at:at+n*4];at+=len(raw);assert len(raw)==n*4
        if addr<0x300000:
            assert addr+len(raw)<=len(ram) and not any(addr<end and addr+len(raw)>begin for begin,end in protected)
            ram[addr:addr+len(raw)]=raw
        elif addr==0x300000:assert len(raw)<=8192;dl=list(struct.unpack('<'+'I'*n,raw))
        elif addr==0x302054:
            serial+=1;idx,pal,protected,_=render(dl,ram)
            f=out/f'frame_{serial:04}.idx';assert idx==f.read_bytes() and pal==f.with_suffix('.pal').read_bytes(),(label,serial)
            if serial in (20,245):png(EV/f'stage1_fix3_{label}_{serial:04}.png',rgb(idx,pal),640,480)
        else:raise AssertionError(hex(addr))
    assert serial==r['frames']
    refs={}
    for f in (BASE/'runs/menu64').glob('frame_*.idx'):
        refs.setdefault(hashlib.sha256(rgb(f.read_bytes(),f.with_suffix('.pal').read_bytes())).hexdigest(),[]).append(f.name)
    menu=[]
    for serial in range(1,21):
        f=out/f'frame_{serial:04}.idx';matches=refs.get(hashlib.sha256(rgb(f.read_bytes(),f.with_suffix('.pal').read_bytes())).hexdigest(),[])
        assert matches,(label,serial);menu.append(dict(frame=serial,reference=matches))
    assert (out/'world.be').stat().st_size>10000 and 'WORLD ' in log
    r.update(data_sha256=sha(BASE/'data/HMM2.DAT'),exact_ft_frames=r['frames'],exact_ft_pixels=r['frames']*640*480,
        menu_rgb_exact=20,menu_reference=menu,no_visible_overwrites=True,world_sha256=sha(out/'world.be'),
        sd_requests=len(starts),sd_read_requested=sum(int(s[5]) for s in starts if s[0]=='E4' or s[1]=='1'),sd_max_wait_ms=max(int(e[3]) for e in ends),
        sd_file_requests=[dict(op=int(s[1]),name=s[2],seq=int(s[3]),offset=int(s[4]),length=int(s[5]),result=int(e[2]),wait_ms=int(e[3])) for s,e in zip(starts,ends) if s[0]=='E5'])
    return r

def main():
    labels=sys.argv[1:] or ['map1','map10','bad_seq2','before6'];assert len(labels)==4
    normal,slow=[examine(label) for label in labels[:2]]
    assert slow['poll_period_ms']==10 and slow['spi_latency_reads']==3 and slow['spi_stats'][1]>0
    fault=json.loads((BASE/'joint'/labels[2]/'result.json').read_text())
    assert fault['elf_sha256']==normal['elf_sha256'] and fault['spg_sha256']==normal['spg_sha256'] and fault['stage']==12 and fault['code']==255
    assert any('FILE FHEROES2.CFG' in s for s in fault['screen'])
    fault_log=(BASE/'joint'/labels[2]/'esp.log').read_text(encoding='utf-8')
    assert 'SD reject 74 seq=75' in fault_log and 'SD timeout E5/0 fheroes2.cfg S11 O0 L0 RX74/75' in fault_log
    before=json.loads((BASE/'joint'/labels[3]/'result.json').read_text())
    assert before['spg_sha256']=='9d380bd6a7bce7a12f3801fbd445b487f9fb9772b4ff16092c794d51d229b3f0' and before['spi_stats'][11]==16384
    assert before['frames']==0 and before['exit']==1 and before['spi_stats'][3]>0
    before_log=(BASE/'joint'/labels[3]/'esp.log').read_text(encoding='utf-8')
    # Codex: при ином расписании потоков первым застревает другой запрос;
    # обязательны заполненное кольцо, реальный тайм-аут и отсутствие меню.
    assert re.search(r'SD timeout E[45]/',before_log)
    r=dict(elf_sha256=normal['elf_sha256'],spg_sha256=normal['spg_sha256'],data_sha256=normal['data_sha256'],menu_and_map=True,
           runs=[normal,slow],negative=fault,baseline=before)
    (EV/'stage1_fix3_joint.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
    shutil.copy2(BASE/'joint'/labels[2]/'error.png',EV/'stage1_fix3_error.png')
    with zipfile.ZipFile(EV/'stage1_fix3_joint.zip','w',zipfile.ZIP_DEFLATED) as z:
        for label in labels:
            out=BASE/'joint'/label
            for name in ['esp.log','z80.jsonl','spi.jsonl','result.json','world.be','frames.ft']:
                if (out/name).exists():z.write(out/name,label+'/'+name)
            if label in labels[:2]:
                for p in sorted(out.glob('frame_*')):
                    if p.suffix in ('.idx','.pal'):z.write(p,label+'/'+p.name)
        z.write(BASE/'joint/before/_HMM2.SPG','before/_HMM2.SPG')
        z.write(BASE/'launcher/_HMM2.SPG','after/_HMM2.SPG')
    print(json.dumps({k:r[k] for k in ['elf_sha256','spg_sha256','menu_and_map']},indent=2))
    for s in r['runs']:print(s['poll_period_ms'],s['frames'],s['sd_requests'],s['sd_max_wait_ms'],s['spi_stats'])
if __name__=='__main__':main()
