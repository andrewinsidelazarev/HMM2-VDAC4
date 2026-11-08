"""Клод: все чтения прогона совместного стенда по файлам HMM2.DAT и фазам сцены: кто и сколько читает с карты.
    tally_reads.py <каталог прогона> <HMM2.DAT> [первый_кадр последний_кадр]"""
from pathlib import Path
import bisect,json,re,struct,sys
from collections import Counter,defaultdict

def catalog(path):
    with open(path,'rb') as f:
        _,version,count,_=struct.unpack('<4I',f.read(16));table=f.read(count*72)
    rows=[]
    for i in range(count):
        name=table[i*72:i*72+60].split(b'\0')[0].decode('ascii','replace');offset,size=struct.unpack_from('<II',table,i*72+64)
        # Клод: псевдонимы audio/sfx указывают внутрь AGG; при поиске побеждает самая короткая запись.
        rows.append((offset,size,name))
    rows.sort();return rows

def owner(rows,starts,at):
    best=None
    for i in range(bisect.bisect_right(starts,at)-1,max(-1,bisect.bisect_right(starts,at)-40),-1):
        o,s,n=rows[i]
        if o<=at<o+s and (best is None or s<best[1]):best=(o,s,n)
    return best[2] if best else '?'

def main():
    out=Path(sys.argv[1]);rows=catalog(sys.argv[2]);starts=[r[0] for r in rows]
    first=int(sys.argv[3]) if len(sys.argv)>3 else 0;last=int(sys.argv[4]) if len(sys.argv)>4 else 1<<30
    phases={}
    for line in (out/'esp.log').read_text(encoding='utf-8',errors='replace').splitlines():
        m=re.search(r'FRAME (\d+) \S+ phase=(\S+)',line)
        if m:phases[int(m[1])]=m[2]
    frame=0;total=Counter();by_kind=Counter();by_phase=defaultdict(Counter);by_frame=Counter();kept=0
    for line in (out/'z80.jsonl').open(encoding='utf-8'):
        e=json.loads(line)
        if e.get('event')=='frame':frame=e['n'];continue
        kind=e.get('event')
        if kind not in ('ServeRead','ServeGraphics'):continue
        raw=bytes.fromhex(e['data'])
        if kind=='ServeRead':offset,size=struct.unpack_from('<IH',raw,2)
        else:offset=struct.unpack_from('<I',raw,2)[0];size=struct.unpack_from('<H',raw,10)[0]
        if not first<=frame<=last:continue
        name=owner(rows,starts,offset);key=name+(' [E6]' if kind=='ServeGraphics' else '')
        total[key]+=size;by_kind[kind]+=size;by_frame[frame]+=size;kept+=1
        by_phase[phases.get(frame+1,phases.get(frame,'?'))][key]+=size
    print(f'запросов {kept}, байт {sum(total.values())}, по видам {dict(by_kind)}')
    for name,size in total.most_common(25):print(f'{size/1048576:8.2f} МБ  {name}')
    for phase,items in by_phase.items():
        print(f'--- фаза {phase}: {sum(items.values())/1048576:.2f} МБ')
        for name,size in items.most_common(8):print(f'{size/1048576:8.2f} МБ  {name}')
    print('--- самые тяжёлые кадры')
    for f,size in by_frame.most_common(12):print(f'кадр {f} ({phases.get(f+1,"?")}): {size/1048576:.2f} МБ')
if __name__=='__main__':main()
