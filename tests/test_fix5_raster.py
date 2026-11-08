"""Codex: снимки RAM_G/DL исполняются точным RTL 1.34 с потактовой SDRAM."""
from pathlib import Path
import argparse,json,struct,subprocess,sys,re
from check_fix5_ft import render,np,Image

OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/raster')

def scanout640(p):
    """Codex: независимый расчёт весов штатного scanout 640×480 → 960×720."""
    rgb=np.stack([((p>>11)<<3)|(p>>13),((p>>5&63)<<2)|((p>>9)&3),((p&31)<<3)|((p>>2)&7)],axis=2)
    x=np.arange(960)*2//3;y=np.arange(720)*2//3
    wx=(np.arange(960)%3==1).astype(np.int32)[None,:,None]*8
    wy=(np.arange(720)%3==1).astype(np.int32)[:,None,None]*8
    a=rgb[y[:,None],x];b=rgb[y[:,None],np.minimum(x+1,639)]
    c=rgb[np.minimum(y+1,479)[:,None],x];d=rgb[np.minimum(y+1,479)[:,None],np.minimum(x+1,639)]
    top=a*16+(b-a)*wx;bottom=c*16+(d-c)*wx
    out=np.zeros((720,1280,3),dtype=np.uint8);out[:,160:1120]=(top*16+(bottom-top)*wy+128)//256
    return out

def snapshots(trace,selected):
    ram=bytearray(0x300000);dl=bytearray(8192)
    with trace.open('rb') as f:
        while header:=f.read(8):
            address,count=struct.unpack('<II',header);data=f.read(count*4)
            if address==0xffffffff:
                serial=struct.unpack('<I',data)[0]
                if serial in selected:yield serial,bytes(ram),bytes(dl)
            else:
                address &= 0x7fffffff
                if address<0x300000:ram[address:address+len(data)]=data
                elif address<0x302000:dl[address-0x300000:address-0x300000+len(data)]=data

def main():
    ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);ap.add_argument('frames',type=int,nargs='+')
    ap.add_argument('--ramg-write',type=int,default=0);a=ap.parse_args()
    rows=[]
    for serial,ram,dl in snapshots(a.trace,set(a.frames)):
        folder=OUT/f'frame_{serial}';folder.mkdir(exist_ok=True)
        words=[]
        for v, in struct.iter_unpack('<I',dl):
            words.append(v)
            if not v:break
        protected=[];rgb,shadow=render(words,ram,protected)
        (folder/'ramg.bin').write_bytes(ram)
        record=struct.pack('<5I',serial,0,7000,0,len(words))+struct.pack('<%dI'%len(words),*words)
        (folder/'trace.bin').write_bytes(b'VTR1'+struct.pack('<I',2)+record+record)
        cmd=[str(OUT/'vsys.exe'),'--trace',str(folder/'trace.bin'),'--ramg',str(folder/'ramg.bin'),'--seconds','0.15','--loop','--no-skip','--dump',str(folder/'frame.hex')]
        if rgb.shape[1]==640:cmd+=['--native640','--hdmi',str(folder/'hdmi.rgb')]
        if a.ramg_write:
            # Codex: нагрузка SPI28 идёт в свободный хвост, не повреждая показанный снимок.
            assert all(end<=0x2e0000 for begin,end in protected)
            cmd+=['--ramg-wr',str(a.ramg_write),'--ramg-at','0x2e0000']
        with (folder/'run.log').open('wb') as log:r=subprocess.run(cmd,cwd=OUT,stdout=log,stderr=subprocess.STDOUT)
        r.check_returncode()
        raw=np.array([int(l,16) for l in (folder/'frame.hex').read_text().split()],dtype='<u4')
        p=raw.view('<u2').reshape(768,1024).astype(np.int32)
        p=p[:rgb.shape[0],:rgb.shape[1]]
        expected=(rgb[:,:,0].astype(np.int32)>>3)<<11|(rgb[:,:,1].astype(np.int32)>>2)<<5|(rgb[:,:,2].astype(np.int32)>>3)
        # Codex: при включённом дизеринге допустим один младший разряд RGB565.
        delta=np.maximum.reduce([abs((p>>11)-(expected>>11)),abs((p>>5&63)-(expected>>5&63)),abs((p&31)-(expected&31))])
        bad=int((delta>1).sum());row=dict(frame=serial,dl_bytes=4*len(words),different_over_one_bit=bad,max_channel_error=int(delta.max()),exit=r.returncode)
        log=(folder/'run.log').read_text(encoding='utf-8')
        times=re.search(r'кадров FT\s+\d+ \(([\d.]+)/([\d.]+) мс\)',log)
        assert times;row.update(raster_mean_ms=float(times[1]),raster_max_ms=float(times[2]))
        row['within_output_period']=row['raster_max_ms']<=1000/60
        if a.ramg_write:
            checks=re.search(r'не тех байтов (\d+); FT_STAT 25 .*? (\d+), FT_STAT 26 .*? (\d+)',log)
            assert checks
            row.update(ramg_wrong_bytes=int(checks[1]),ramg_drop=int(checks[2]),ramg_late=int(checks[3]),spi28_write_per_step=a.ramg_write)
        picture=np.stack([(p>>11)*255//31,(p>>5&63)*255//63,(p&31)*255//31],axis=2).astype(np.uint8)
        Image.fromarray(picture).save(folder/'frame.png');Image.fromarray(rgb).save(folder/'software.png')
        if rgb.shape[1]==640:
            hdmi=np.frombuffer((folder/'hdmi.rgb').read_bytes(),dtype=np.uint8).reshape(720,1280,3)
            Image.fromarray(hdmi).save(folder/'hdmi.png')
            row['hdmi_different_pixels']=int(np.any(hdmi!=scanout640(p),axis=2).sum())
        rows.append(row);print(row,flush=True)
    (OUT/'results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    return int(any(r['different_over_one_bit'] or r.get('hdmi_different_pixels',0) or not r['within_output_period'] or r.get('ramg_wrong_bytes',0) or r.get('ramg_drop',0) or r.get('ramg_late',0) for r in rows))

if __name__=='__main__':raise SystemExit(main())
