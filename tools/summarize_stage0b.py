"""Codex: сохраняем первичные журналы и одновременные пики непиксельной памяти 97 карт."""
from pathlib import Path
import collections,hashlib,json,struct,zipfile
ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')
OUT=ROOT/'Docs/evidence'

def parse(path):
    result={};world=None
    for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
        f=line.split('\t')
        if f[0]=='PHASE':result.setdefault(f[1],{}).update(zip(('live','peak','cache','pc_ms','frames','crt_live'),map(int,f[2:])))
        elif f[0]=='NOPIXEL':result.setdefault(f[1],{}).update(pixel_live=int(f[2]),nonpixel_peak=int(f[3]))
        elif f[0]=='WORKSET':
            d=result.setdefault(f[1],{});d['accessed_pixels_max']=max(d.get('accessed_pixels_max',0),int(f[3]))
        elif f[0]=='WORLD':world=dict(width=int(f[2]),height=int(f[3]),serialized_bytes=int(f[4]),fnv1a=f[5])
    return result,world

def main():
    runs=json.loads((OUT/'stage0b_profile_runs.json').read_text());assert len(runs)==97 and all(r['code']==0 for r in runs)
    maps=[];summary={}
    with zipfile.ZipFile(OUT/'stage0b_logs.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for r in runs:
            p=Path(r['log']);raw=p.read_bytes();assert b'COMPLETE\t' in raw
            days=int(next(line.split(b'\t')[1] for line in raw.splitlines() if line.startswith(b'DAYS\t')));assert days>=7
            phases,_=parse(p);assert all(d['crt_live']-d['live']==16 for d in phases.values())
            for d in phases.values():d['nonpixel_live']=d['live']-d['pixel_live']
            data=Path('E:/zx/HMM2/Assets/Original/MAPS',r['map']).read_bytes();w,h=struct.unpack_from('<II',data,420)
            assert w==h and w in [36,72,108,144]
            row=dict(map=r['map'],width=w,height=h,days=days,phases=phases,loaded_nonpixel_increment=phases['load']['nonpixel_live']-phases['menu']['nonpixel_live'],
                     tile_stl_adjustment=-24*w*h,log_sha256=hashlib.sha256(raw).hexdigest(),exe_sha256=r['exe_sha256'])
            maps.append(row);archive.writestr(r['map']+'/run.log',raw)
    for phase in maps[0]['phases']:
        summary[phase]={}
        for metric in ['peak','cache','pixel_live','nonpixel_peak','nonpixel_live','accessed_pixels_max']:
            winner=max(maps,key=lambda r:r['phases'][phase].get(metric,0))
            summary[phase][metric]=dict(bytes=winner['phases'][phase].get(metric,0),map=winner['map'])
    result=dict(scope='Win32 MSVC requested heap; без накладных расходов аллокатора и DLL. Поправка Tile известна, остальные ABI различия не вычтены.',maps=maps,summary=summary)
    (OUT/'stage0b_memory.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
