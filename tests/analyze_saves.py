"""Codex: E5, круги 76 и входы FAT32 из настоящей совместной трассы."""
from pathlib import Path
from collections import Counter
import argparse,json,hashlib

def requests(folder):
    rows=[];current=None
    for line in (folder/'z80.jsonl').read_text(encoding='utf-8').splitlines():
        e=json.loads(line);event=e.get('event')
        if event=='ServeFile':
            p=bytes.fromhex(e['data'])
            current=dict(seq=int.from_bytes(p[:2],'little'),op=p[2],name=p[3:16].split(b'\0')[0].decode(),
                         offset=int.from_bytes(p[29:33],'little'),length=int.from_bytes(p[33:35],'little'),begin=e,pulls=0,fat=Counter())
        elif current and event.startswith('FAT32_'):current['fat'][event]+=1
        elif current and event=='Proto_Send':
            if e['command']==0x76:current['pulls']+=1
            elif e['command']==0x74:
                p=bytes.fromhex(e['data']);assert int.from_bytes(p[:2],'little')==current['seq']
                current.update(status=p[2],end=e)
                rows.append(current);current=None
    return rows

def summary(rows):
    fat=Counter()
    for r in rows:fat.update(r['fat'])
    return dict(requests=len(rows),bytes=sum(r['length'] for r in rows),pulls=sum(r['pulls'] for r in rows),
                service_ms=sum(r['end']['ms']-r['begin']['ms'] for r in rows),
                span_ms=rows[-1]['end']['ms']-rows[0]['begin']['ms'] if rows else 0,
                fat=dict(fat),errors=sum(bool(r['status']) for r in rows))

def analyze(folder):
    rows=requests(folder);files={}
    for name in sorted({r['name'] for r in rows if r['name'].endswith('.SAV')}):
        own=[r for r in rows if r['name']==name]
        # Codex: первое последовательное чтение; seek и намеренный короткий файл считаем отдельно.
        reads=[];at=0
        for r in own:
            if r['op'] in (1,7) and not r['status'] and r['offset']==at:
                reads.append(r);at+=r['length']
        files[name]=dict(all=summary(own),write=summary([r for r in own if r['op'] in (3,8)]),
                        sequential_read=summary(reads),create=summary([r for r in own if r['op']==2]),
                        close=summary([r for r in own if r['op']==6]))
    return dict(folder=str(folder),files=files,
                hashes={p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in (folder/'sd').glob('*')})

def main():
    ap=argparse.ArgumentParser();ap.add_argument('folders',type=Path,nargs='+');ap.add_argument('--out',type=Path);a=ap.parse_args()
    data=[analyze(p) for p in a.folders];text=json.dumps(data,ensure_ascii=False,indent=2)
    if a.out:a.out.write_text(text+'\n',encoding='utf-8')
    print(text)
if __name__=='__main__':main()
