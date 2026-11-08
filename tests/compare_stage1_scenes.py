"""Codex: сравниваем сохранённые пиксели, палитры и состояния; несовпадения остаются в результате."""
from pathlib import Path
import re,json,hashlib,sys
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def load(name):
    d=BASE/'runs'/name;txt=(d/'run.log').read_text(encoding='utf-8');frames=[]
    for num,phase in re.findall(r'HMM2 frame=(\d+).*?phase=(\w+)',txt):
        p=d/f'frame_{int(num):04}.idx';raw=p.read_bytes();pal=p.with_suffix('.pal').read_bytes();frames.append((int(num),phase,raw,pal))
    return frames,re.findall(r'^STATE.*$|^HERO.*$|^BATTLE.*$',txt,re.M)
def main():
    base,bs=load('scene64');packed,ps=load('scene_packed');result={}
    pairs=[('packed32_vs_original64',packed,ps)]
    if '--history' in sys.argv:
        wide,ws=load('scene');pairs.append(('original32_with_platform_hash',wide,ws))
    for label,other,states in pairs:
        same=sum(a[1:]==b[1:] for a,b in zip(base,other));scenes={}
        for phase in ['menu','map','hero','town','battle']:
            a=next(f for f in base if f[1]==phase);b=next(f for f in other if f[1]==phase)
            rgb_a=b''.join(a[3][k*3:k*3+3] for k in a[2]);rgb_b=b''.join(b[3][k*3:k*3+3] for k in b[2]);n=sum(rgb_a[i:i+3]==rgb_b[i:i+3] for i in range(0,len(rgb_a),3))
            scenes[phase]=dict(matched_pixels=n,pixels=307200,exact=n==307200,first_frames=[a[0],b[0]])
        result[label]=dict(base_frames=len(base),other_frames=len(other),same_index_frames=same,base_states=bs,other_states=states,scenes=scenes)
    (BASE/'runs/comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
    current=result['packed32_vs_original64']
    assert current['base_frames']==current['other_frames']==current['same_index_frames'] and bs==ps,'Codex: сцены или состояния отличаются от эталона'
if __name__=='__main__':main()
