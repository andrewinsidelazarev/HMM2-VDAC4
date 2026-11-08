"""Codex: побайтный PCM и число чтений до/после общего кэша при 32/8 каналах."""
from pathlib import Path
import argparse,hashlib,json,re

def main():
    ap=argparse.ArgumentParser();ap.add_argument('before',type=Path);ap.add_argument('after',type=Path);a=ap.parse_args()
    stems=[a.before,a.after];pcm=[p.with_suffix('.f32').read_bytes() for p in stems]
    assert pcm[0]==pcm[1],"Изменились исходные отсчёты или панорама SFX"
    rows=[]
    for p in stems:
        log=p.with_suffix('.log').read_text(encoding='utf-8');assert 'AUDIO CACHE complete' in log
        rows.append([dict(zip(('phase','begin','end','reads','bytes'),map(int,m))) for m in re.findall(r'phase=(\d+) begin=(\d+) end=(\d+) reads=(\d+) bytes=(\d+)',log)])
    assert len(rows[0])==len(rows[1])==3
    assert rows[1][0]['reads']<rows[0][0]['reads']/8,"Не используются общие данные одинаковых каналов"
    assert rows[1][1]['reads']<=rows[0][1]['reads'],"32 разных канала вытесняют друг друга"
    assert rows[1][2]['reads']<rows[0][2]['reads']/2,"Окна 8 каналов не работают после смены размера"
    result=dict(exact_pcm=True,stereo_frames=len(pcm[0])//8,sha256=hashlib.sha256(pcm[0]).hexdigest(),before=rows[0],after=rows[1])
    a.after.with_suffix('.cachecheck.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
