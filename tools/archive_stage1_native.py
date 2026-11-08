"""Codex: сами кадры и состояния в доказательствах, плюс журналы и хеши исходного сравнения."""
from pathlib import Path
import hashlib,json,sys,zipfile,shutil,re
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'))
from test_ft_trace import png
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1');OUT=ROOT/'Docs/evidence'
def main():
    comparison=json.loads((BASE/'runs/comparison.json').read_text())['packed32_vs_original64']
    assert comparison['same_index_frames']==comparison['base_frames']==comparison['other_frames']==370
    gpu=json.loads((BASE/'runs/scene_packed/gpu_comparison.json').read_text());assert gpu['matched']==370 and gpu['no_visible_overwrites']
    shutil.copy2(BASE/'runs/comparison.json',OUT/'stage1_comparison.json');shutil.copy2(BASE/'runs/scene_packed/gpu_comparison.json',OUT/'stage1_gpu.json')
    selected={'menu':1,'map':2,'hero':37,'town':42,'battle':230}
    for scene,n in selected.items():
        p=BASE/f'runs/scene_packed/frame_{n:04}.idx';raw=p.read_bytes();pal=p.with_suffix('.pal').read_bytes();png(OUT/f'stage1_native_{scene}.png',b''.join(pal[v*3:v*3+3] for v in raw),640,480)
    with zipfile.ZipFile(OUT/'stage1_native.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name in ['menu64','scene64','scene_packed','scene64_allmaps','scene_packed_allmaps','scene_packed_save','scene64_save','target_save64']:
            folder=BASE/'runs'/name;z.write(folder/'run.log',name+'/run.log')
            count=max(map(int,re.findall(r'HMM2 frame=(\d+)',(folder/'run.log').read_text(encoding='utf-8'))))
            for pattern in ['frame_*.idx','frame_*.pal','states/*.bin','states/*.regions','states/*.be']:
                for p in sorted(folder.glob(pattern)):
                    if p.name.startswith('frame_') and int(p.stem.split('_')[1])>count:continue
                    z.write(p,name+'/'+str(p.relative_to(folder)).replace('\\','/'))
        z.write(BASE/'runs/scene_packed/frames.ft','scene_packed/frames.ft')
    binaries={}
    for n in ['host64','host32_packed']:
        p=BASE/n/'hmm2_host.exe';binaries[n]=dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    (OUT/'stage1_native_binaries.json').write_text(json.dumps(binaries,indent=2),encoding='utf-8')
    print('Кадры, миры и графы сохранены:',(OUT/'stage1_native.zip').stat().st_size,'байт')
if __name__=='__main__':main()
