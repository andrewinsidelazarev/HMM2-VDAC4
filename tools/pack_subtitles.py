"""Pack reviewed private Russian speech captions beside unchanged DAT/videos."""
from pathlib import Path
import argparse,hashlib,json,struct

def fnv(data):
    v=2166136261
    for c in data:v=((v^c)*16777619)&0xffffffff
    return v

def build(catalog,output,audio=None):
    rows=json.loads(catalog.read_text(encoding='utf-8'))
    tracks=[];names=set()
    for row in rows:
        name=row['name'].lower()
        assert row.get('reviewed') is True,'Unreviewed dialogue: '+name
        assert name.endswith('.smk') and len(name)<=12 and name not in names
        names.add(name)
        if audio:
            p=audio/(name[:-4]+'.ogg')
            assert hashlib.sha256(p.read_bytes()).hexdigest()==row['audio_sha256'],name
        if not row['cues']:
            assert row.get('exclude_reason'),'Missing speech without review: '+name
            continue
        strings=bytearray();items=[];end=0
        for cue in row['cues']:
            start,stop=cue['start'],cue['end'];text=cue['ru']
            assert isinstance(start,int) and isinstance(stop,int) and end<=start<stop<=1800000
            assert text and not any(ord(c)<32 and c!='\n' for c in text)
            raw=text.encode('cp1251')+b'\0';assert len(raw)<=513
            items.append((start,stop,len(row['cues'])*12+len(strings)));strings+=raw;end=stop
        block=b''.join(struct.pack('<3I',*r) for r in items)+strings
        assert len(block)<=65536 and len(items)<=512
        tracks.append((fnv(name.encode('ascii')),name,len(items),block))
    tracks.sort();assert tracks and len(tracks)<=128 and len({r[0] for r in tracks})==len(tracks)
    payload=bytearray();directory=[];at=24+20*len(tracks)
    for key,name,count,block in tracks:
        padding=(-at)&3;payload+=bytes(padding);at+=padding
        directory.append((key,at,len(block),count,fnv(block)));payload+=block;at+=len(block)
    index=b''.join(struct.pack('<5I',*r) for r in directory)
    blob=struct.pack('<6I',0x42555348,1,at,len(tracks),fnv(index),0)+index+payload
    assert len(blob)==at and len(blob)<=256*1024
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(blob)
    report=dict(bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest(),tracks=len(tracks),
        cues=sum(t[2] for t in tracks),reviewed_audio=len(rows),excluded=len(rows)-len(tracks),private=True)
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report);return report

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--catalog',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--audio',type=Path)
    a=ap.parse_args();build(a.catalog,a.output,a.audio)
