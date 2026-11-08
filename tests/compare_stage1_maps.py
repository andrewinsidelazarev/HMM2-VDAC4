"""Codex: побайтовое сравнение миров и графов ИИ, не только коротких контрольных сумм."""
from pathlib import Path
import json,hashlib,re
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1/runs')
def main():
    a=BASE/'scene64_allmaps';b=BASE/'scene_packed_allmaps';files=sorted((a/'states').glob('*.bin'));assert len(files)==97
    rows=[]
    original={n:int(v) for n,v in re.findall(r'REGION_QUEUE (\S+) bytes=(\d+)',(a/'run.log').read_text())}
    packed={n:int(v) for n,v in re.findall(r'REGION_QUEUE (\S+) bytes=(\d+)',(b/'run.log').read_text())}
    for p in files:
        other=b/'states'/p.name;assert p.read_bytes()==other.read_bytes(),p.name
        g=p.with_suffix('.regions');assert g.read_bytes()==other.with_suffix('.regions').read_bytes(),g.name
        map_name='maps/'+p.stem.removeprefix('maps_')
        rows.append(dict(map=map_name,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),regions_sha256=hashlib.sha256(g.read_bytes()).hexdigest(),queue_original=original[map_name],queue_packed=packed[map_name]))
    save=BASE/'scene_packed_save/states';assert (save/'moved.bin').read_bytes()==(save/'reloaded.bin').read_bytes()
    result=dict(exact_maps=len(rows),exact_region_graphs=len(rows),save_roundtrip_exact=True,maps=rows)
    (ROOT/'Docs/evidence/stage1_maps.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Миры и графы:',len(rows),'из 97 совпали; сохранение/загрузка совпали')
    print(next(x for x in rows if 'gatehell' in x['map']))
if __name__=='__main__':main()
