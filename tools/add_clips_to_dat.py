"""Клод: новый HMM2.DAT = прежний DAT + ролики в конце. Каталог обычно умещается в запас перед данными и смещения
не меняются; иначе прежние данные сдвигаются кратно 4 КиБ: их выравнивание сохраняется.
Записи video/* выровнены по сектору: E6 читает кадры кратно 512."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct

def main():
    ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('clips',type=Path);ap.add_argument('out',type=Path);args=ap.parse_args()
    with args.source.open('rb') as f:
        head=f.read(16);magic,version,count,reserved=struct.unpack('<4I',head)
        if magic!=0x31465348 or version not in (1,2):raise SystemExit('не каталог HMM2.DAT')
        table=bytearray(f.read(count*72))
    used=[(bytes(table[i*72:i*72+60 if version==2 else i*72+64]).split(b'\0')[0].decode('ascii'),*struct.unpack_from('<II',table,i*72+64)) for i in range(count)]
    # Клод: в каталоге версии 2 первый сектор записи лежит отдельно, в префиксе: сразу за каталогом, ниже смещений записей.
    prefixes=[p for p in (struct.unpack_from('<I',table,i*72+60)[0] for i in range(count)) if p] if version==2 else []
    first=min([o for _,o,_ in used]+prefixes);size=args.source.stat().st_size
    if any(n.startswith('video/') for n,_,_ in used):raise SystemExit('в DAT уже есть ролики')
    clips=sorted(p for p in args.clips.iterdir() if p.suffix in ('.smk','.ogg'))
    new_count=count+len(clips);new_first=(16+new_count*72+4095)&~4095
    shift=max(0,(new_first-first+4095)&~4095)
    for i in range(count):
        offset=struct.unpack_from('<I',table,i*72+64)[0];struct.pack_into('<I',table,i*72+64,offset+shift)
        if version==2:
            prefix=struct.unpack_from('<I',table,i*72+60)[0]
            if prefix:struct.pack_into('<I',table,i*72+60,prefix+shift)
    at=(size+shift+511)&~511;rows=[]
    for p in clips:
        name=('video/'+p.name).encode('ascii');length=p.stat().st_size
        if len(name)>=60:raise SystemExit(name)
        rows.append((p,at,length));table+=name.ljust(64,b'\0')+struct.pack('<II',at,length);at=(at+length+511)&~511
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('wb') as o,args.source.open('rb') as f:
        o.write(struct.pack('<4I',magic,version,new_count,reserved));o.write(table)
        o.seek(first+shift);f.seek(first);shutil.copyfileobj(f,o,4*1024*1024)
        for p,offset,_ in rows:
            o.seek(offset)
            with p.open('rb') as r:shutil.copyfileobj(r,o,1024*1024)
    digest=hashlib.file_digest(args.out.open('rb'),'sha256').hexdigest()
    print('DAT',args.out.stat().st_size,'байт, записей',new_count,'сдвиг',shift,'SHA256',digest)
    args.out.with_suffix('.json').write_text(json.dumps(dict(bytes=args.out.stat().st_size,sha256=digest,shift=shift,clips=len(rows)),indent=1),encoding='utf-8')
if __name__=='__main__':main()
