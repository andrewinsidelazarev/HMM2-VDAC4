"""Codex: читаем каталоги AGG и заголовки SMK; игровые данные остаются на месте."""
from pathlib import Path
import collections
import hashlib
import json
import struct

ROOT=Path(__file__).resolve().parents[1]
DATA=Path('E:/zx/HMM2/Assets/Original')
def agg_hash(name):
    h=s=0
    for c in name.upper()[::-1]:
        h=((h<<5)+(h>>25))&0xffffffff;s+=c;h=(h+s+c)&0xffffffff
    return h

def main():
    report={'archives':[], 'maps':[], 'smk':[]}
    for p in sorted((DATA/'DATA').glob('*.AGG')):
        data=p.read_bytes();count=struct.unpack_from('<H',data)[0];entries=[]
        for i in range(count):
            raw=data[len(data)-count*15+i*15:len(data)-count*15+(i+1)*15].split(b'\0')[0]
            h,offset,size=struct.unpack_from('<III',data,2+i*12)
            assert agg_hash(raw)==h and offset+size<=len(data)
            name=raw.decode('ascii');e={'name':name,'offset':offset,'bytes':size}
            if name.endswith('.ICN'):
                n=struct.unpack_from('<H',data,offset)[0]
                assert 6+n*13<=size
                frames=[struct.unpack_from('<hhHHBI',data,offset+6+j*13) for j in range(n)]
                e.update(frames=n,pixels=sum(f[2]*f[3] for f in frames),
                         decoded_two_layers=sum(2*f[2]*f[3] for f in frames))
            elif name.endswith('.TIL'):
                n,w,h=struct.unpack_from('<HHH',data,offset);e.update(frames=n,width=w,height=h,pixels=n*w*h)
            entries.append(e)
        group=collections.defaultdict(lambda:{'count':0,'bytes':0,'decoded_two_layers':0})
        for e in entries:
            g=group[Path(e['name']).suffix];g['count']+=1;g['bytes']+=e['bytes'];g['decoded_two_layers']+=e.get('decoded_two_layers',0)
        report['archives'].append({'path':str(p),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'entries':entries,'types':dict(group)})
    for p in sorted((DATA/'MAPS').iterdir()):
        if p.is_file():report['maps'].append({'name':p.name,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    video_dir=next((p for p in [ROOT/'Assets/Original/ANIM',DATA/'ANIM',ROOT/'Assets/ANIM'] if p.exists() and list(p.glob('*.SMK'))),None)
    if video_dir is None:raise FileNotFoundError('Не найден каталог предоставленных SMK')
    report['smk_directory']=str(video_dir)
    for p in sorted(video_dir.glob('*.SMK')):
        with p.open('rb') as f: h=f.read(104)
        assert h[:3]==b'SMK'
        w,hh,n=struct.unpack_from('<III',h,4)
        report['smk'].append({'name':p.name,'bytes':p.stat().st_size,'width':w,'height':hh,'frames':n,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    (ROOT/'Docs/evidence/inventory.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    for a in report['archives']:print(Path(a['path']).name,a['bytes'],a['types'])
    print('Карты',len(report['maps']),'SMK',len(report['smk']),sum(s['bytes'] for s in report['smk']))

if __name__=='__main__':main()
