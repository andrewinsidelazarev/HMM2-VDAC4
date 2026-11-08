"""Codex: фиксируем журналы и кадры стенда как доказательства; графику не дорисовываем."""
from pathlib import Path
import collections
import hashlib
import json
import shutil
import statistics
import struct
import zlib
from summarize_profile import quantile

ROOT=Path(__file__).resolve().parents[1];TMP=Path('E:/zx/cache/tmp/hmm2_esp');OUT=ROOT/'Docs/evidence'
def main():
    logs=OUT/'host_logs';logs.mkdir(exist_ok=True)
    for p in (TMP/'runs').glob('*/run.log'):shutil.copy2(p,logs/(p.parent.name+'.log'))
    inv=json.loads((OUT/'inventory.json').read_text(encoding='utf-8'))
    a=next(a for a in inv['archives'] if Path(a['path']).name=='HEROES2.AGG');p=next(e for e in a['entries'] if e['name']=='KB.PAL')
    with Path(a['path']).open('rb') as f:f.seek(p['offset']);pal=[v<<2 for v in f.read(p['bytes'])]
    frames=OUT/'frames';frames.mkdir(exist_ok=True)
    for scene in ['menu','map','town','battle']:
        p=TMP/'runs/KINGDTH'/(scene+'.idx')
        mid=TMP/'runs_extra_640/KINGDTH'/(scene+'_mid.idx')
        if mid.exists():p=mid
        pixels=p.read_bytes();assert len(pixels)==640*480
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        raw=b''.join(b'\0'+pixels[y*640:(y+1)*640] for y in range(480))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',640,480,8,3,0,0,0))+chunk(b'PLTE',bytes(pal))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
        (frames/(scene+'.png')).write_bytes(png)
    extra={}
    for width,height in [(640,480),(1024,768)]:
        p=TMP/f'runs_extra_{width}/KINGDTH/run.log'
        shutil.copy2(p,OUT/f'extra_{width}.log')
        values=collections.defaultdict(list);writes=collections.defaultdict(list);phases={}
        for line in p.read_text(encoding='utf-8',errors='replace').splitlines():
            f=line.split('\t')
            if f[0]=='FRAME':values[f[1]].append(int(f[5]));writes[f[1]].append(int(f[6]))
            if f[0]=='PHASE':phases[f[1]]=dict(zip(['live','peak','cache','pc_ms','frames','crt_live'],map(int,f[2:])))
        for scene,v in values.items():phases[scene].update(delta_mean=round(statistics.mean(v),1),delta_p50=quantile(v,.5),delta_p95=quantile(v,.95),writes_mean=round(statistics.mean(writes[scene]),1))
        extra[width]=phases
    (OUT/'extra_summary.json').write_text(json.dumps(extra,ensure_ascii=False,indent=2),encoding='utf-8')
    src=ROOT/'third_party/openhmm2';original=Path('E:/zx/OpenHMM2')
    manifest=json.loads((OUT/'source_manifest.json').read_text(encoding='utf-8'))
    bad=[p for p,h in manifest.items() if hashlib.sha256((src/p).read_bytes()).hexdigest()!=h or hashlib.sha256((original/p).read_bytes()).hexdigest()!=h]
    assert not bad,bad
    print('Исходная копия и оригинал сверены:',len(manifest),'файлов. Журналы:',len(list(logs.glob('*.log'))))
    for res,p in extra.items():print(res,p)
if __name__=='__main__':main()
