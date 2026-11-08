"""Codex: сводим фактические журналы, не подменяя время ПК временем ESP или SD."""
from pathlib import Path
import collections
import hashlib
import json
import statistics

ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp')
def quantile(values,p):
    s=sorted(values);return s[min(len(s)-1,int((len(s)-1)*p))] if s else None
def main():
    scenes=collections.defaultdict(list);maps=[];all_reads=collections.defaultdict(lambda:collections.defaultdict(lambda:{'calls':0,'bytes':0}))
    for log in sorted((TMP/'runs').glob('*/run.log')):
        text=log.read_text(encoding='utf-8',errors='replace')
        if '\nCOMPLETE\t' not in text:continue
        phases={};frames=collections.defaultdict(list);reads=collections.defaultdict(list)
        for line in text.splitlines():
            fields=line.split('\t')
            if fields[0]=='PHASE':
                name=fields[1];v=list(map(int,fields[2:]));phases[name]=dict(zip(('heap_live','heap_peak','image_cache','pc_ms','frames','crt_live'),v))
            elif fields[0]=='FRAME':frames[fields[1]].append({'serial':int(fields[2]),'width':int(fields[3]),'height':int(fields[4]),'bytes':int(fields[5]),'writes':int(fields[6])})
            elif fields[0]=='AGG':reads[fields[1]].append({'name':fields[2],'bytes':int(fields[3])})
        for name,v in phases.items():
            v['map']=log.parent.name;v['agg_calls']=len(reads[name]);v['agg_bytes']=sum(r['bytes'] for r in reads[name]);v['frame_samples']=frames[name];v['reads']=reads[name]
            scenes[name].append(v)
            for r in reads[name]:
                x=all_reads[name][r['name']];x['calls']+=1;x['bytes']+=r['bytes']
        maps.append({'map':log.parent.name,'phases':phases,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
    summary={}
    for name,rows in scenes.items():
        vals=[f['bytes'] for r in rows for f in r['frame_samples']];writes=[f['writes'] for r in rows for f in r['frame_samples']]
        winner=max(rows,key=lambda r:r['heap_peak'])
        summary[name]={'maps':len(rows),'heap_peak_max':winner['heap_peak'],'heap_peak_map':winner['map'],
            'cache_max':max(r['image_cache'] for r in rows),'heap_live_max':max(r['heap_live'] for r in rows),
            'agg_bytes_max':max(r['agg_bytes'] for r in rows),'agg_bytes_min':min(r['agg_bytes'] for r in rows),
            'frames':len(vals),'delta_mean':round(statistics.mean(vals),1) if vals else None,
            'delta_p50':quantile(vals,.5),'delta_p95':quantile(vals,.95),'delta_max':max(vals,default=0),
            'writes_mean':round(statistics.mean(writes),1) if writes else None,
            'crt_minus_tracker':sorted({r.get('crt_live',r['heap_live'])-r['heap_live'] for r in rows})}
    out=ROOT/'Docs/evidence'
    (out/'profile_detail.json').write_text(json.dumps(maps,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'profile_summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'resource_reads.json').write_text(json.dumps(all_reads,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Завершённые карты:',len(maps))
    for name,v in summary.items():print(name,v)
if __name__=='__main__':main()
