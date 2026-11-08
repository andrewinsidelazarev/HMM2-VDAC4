"""Codex: проверяем полноту журналов, независимый учёт CRT, дельту и неизменность оригинальных исходников."""
from pathlib import Path
import collections
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
E=ROOT/'Docs/evidence'

def load(name):return json.loads((E/name).read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    inv=load('inventory.json');runs=load('profile_runs.json');details=load('profile_detail.json')
    expected={p['name'].lower() for p in inv['maps'] if Path(p['name']).suffix.lower() in ('.mp2','.mx2')}
    assert len(expected)==97 and {r['map'].lower() for r in runs}==expected
    assert all(r['code']==0 for r in runs)
    days=collections.Counter();delta_bytes=0;minimum_bytes=0;frame_count=0;town_skipped=[]
    by_name={Path(r['map']).stem.lower():r for r in runs}
    for row in details:
        path=E/'host_logs'/(row['map']+'.log');raw=path.read_text(encoding='utf-8',errors='replace')
        assert sha(path)==row['log_sha256']
        assert '\nCOMPLETE\t' in raw and set(row['phases'])=={'startup','menu','load','map','scroll','town','week','battle'}
        assert by_name[row['map'].lower()]['exe_sha256']
        ds=[int(line.split('\t')[1]) for line in raw.splitlines() if line.startswith('DAYS\t')]
        assert len(ds)==1;days[ds[0]]+=1
        minima={int(f[2]):int(f[3]) for line in raw.splitlines() if (f:=line.split('\t'))[0]=='FRAME_MIN'}
        for phase,v in row['phases'].items():
            assert v['heap_peak']>=v['heap_live']>=v['image_cache']>=0
            assert v['crt_live']-v['heap_live']==16
            assert len(v['frame_samples'])==v['frames']
            if phase=='town' and not v['frames']:town_skipped.append(row['map'])
            for frame in v['frame_samples']:
                size=frame['width']*frame['height'];count=frame['bytes'];mini=minima[frame['serial']]
                assert 0<=mini<=count<=size and count%64==0
                assert count==0 or 0<frame['writes']<=count//64
                if frame['serial']<3:assert count==size
                frame_count+=1;delta_bytes+=count;minimum_bytes+=mini
    assert len(details)==len(expected)
    smk=load('smk_profile.json');stream=load('smk_stream_profile.json')
    assert len(smk)==len(stream)==len(inv['smk'])==169
    stream_by_name={r['name']:r for r in stream};inventory_by_name={r['name']:r for r in inv['smk']}
    for row in smk:
        other=stream_by_name[row['name']]
        for r in (row,other):
            assert r['code']==0 and r['complete']
            assert r['info']['decoded_frames']==r['info']['frames']
            assert r['file_sha256']==inventory_by_name[r['name']]['sha256']
            assert all(p['crt_live']-p['live']==16 for p in r['phases'].values())
        assert row['info']==other['info'],row['name']
    manifest=load('source_manifest.json')
    for p,h in manifest.items():
        assert sha(ROOT/'third_party/openhmm2'/p)==h and sha(Path('E:/zx/OpenHMM2')/p)==h,p
    def largest(rows):
        row=max(rows,key=lambda r:max(p['peak'] for p in r['phases'].values()))
        return dict(name=row['name'],peak=max(p['peak'] for p in row['phases'].values()))
    report=dict(ok=True,maps=len(expected),days=dict(days),town_skipped=town_skipped,frames=frame_count,
                doom_union_bytes=delta_bytes,direct_compare_bytes=minimum_bytes,union_overhead_bytes=delta_bytes-minimum_bytes,
                immutable_source_files=len(manifest),smk_files=len(smk),smk_frames=sum(r['info']['decoded_frames'] for r in smk),
                smk_comparison='Совпали FNV-1a-32 пикселей/палитр, число кадров, размеры и объём звука; это не побайтное сравнение PCM.',
                smk_original_max=largest(smk),smk_stream_max=largest(stream))
    (E/'audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    reference=Path('E:/zx/Tang Nano 20k VDAC/VDAC4')
    files=[Path('E:/zx/DOOM VDAC4/main/video.c'),Path('E:/zx/DOOM VDAC4/VALIDATION.md'),
           reference/'Tang Nano 20k/Docs/ft812-emulation.md',reference/'ESP32 S3 Zero N8R8/Firmware/src/zm_symbols.cpp',
           reference/'ESP32 S3 Zero N8R8/Firmware/modules/sdk/include/zm_app.h',
           reference/'ESP32 S3 Zero N8R8/Firmware/modules/sdk/include/zm_source.h']
    files+=list((ROOT/'tests').glob('*'))+list((ROOT/'tools').glob('*.py'))+list((ROOT/'tools').glob('*.ps1'))
    (E/'provenance.json').write_text(json.dumps({str(p):sha(p) for p in files if p.is_file()},ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
