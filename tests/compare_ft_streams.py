"""Codex: попиксельная RGB-проверка оптимизации, включая тени."""
from pathlib import Path
from itertools import zip_longest
import hashlib,json,sys
from native_ft_frames import frames
from check_fix5_ft import np,Image
before,after=map(Path,sys.argv[1:3]);rows=[]
for a,b in zip_longest(frames(before),frames(after)):
    assert a is not None and b is not None and a[0]==b[0]
    count=int(np.any(a[1]!=b[1],axis=2).sum())
    rows.append(dict(frame=a[0],differences=count,rgb_sha256=hashlib.sha256(b[1].tobytes()).hexdigest()))
    if count and sum(r['differences']>0 for r in rows)<=3:Image.fromarray(b[1]).save(after/f'rgb_difference_{a[0]}.png')
result=dict(frames=len(rows),bad=sum(r['differences']>0 for r in rows),maximum_differences=max(r['differences'] for r in rows),reference=str(before),rows=rows)
(after/'full_rgb_comparison.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print({k:v for k,v in result.items() if k!='rows'})
raise SystemExit(int(result['bad']>0))
