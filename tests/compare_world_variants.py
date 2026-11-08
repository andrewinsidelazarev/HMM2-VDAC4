"""Codex: проверяем побайтовое равенство сериализованных миров, а не только сообщения об успехе."""
from pathlib import Path
import hashlib,json,sys,zipfile
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from summarize_stage0b import parse,TMP,OUT
rows=[]
with zipfile.ZipFile(OUT/'world_variant_logs.zip','w',zipfile.ZIP_DEFLATED) as archive:
    runs=json.loads((OUT/'world_baseline_runs.json').read_text());stream=json.loads((OUT/'world_stream_runs.json').read_text())
    assert len(runs)==len(stream)==97 and all(r['code']==0 for r in runs+stream)
    for run in runs:
        name=Path(run['map']).stem;base=TMP/'world_baseline'/name;other=TMP/'world_stream'/name
        a=(base/'world.state').read_bytes();b=(other/'world.state').read_bytes()
        p,w=parse(base/'run.log');q,_=parse(other/'run.log')
        row=dict(map=run['map'],identical=a==b,bytes=len(a),sha256_baseline=hashlib.sha256(a).hexdigest(),sha256_stream=hashlib.sha256(b).hexdigest(),width=w['width'],height=w['height'],baseline=p['load'],stream=q['load'])
        rows.append(row)
        for label,folder in [('baseline',base),('stream',other)]:archive.write(folder/'run.log',label+'/'+run['map']+'/run.log')
(OUT/'world_variants.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('Побайтово совпало',sum(r['identical'] for r in rows),'из',len(rows))
print(next(r for r in rows if r['map']=='GateHell.mp2'))
raise SystemExit(0 if all(r['identical'] for r in rows) else 1)
