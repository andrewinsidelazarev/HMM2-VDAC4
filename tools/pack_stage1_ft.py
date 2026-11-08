"""Codex: оригинальные кадры OpenHMM2 -> готовые PALETTED8 атласы для SPI DMA Z80.

Индексы 0 и 2..5 зарезервированы под прозрачность и тени; объединяются только
дубли чёрного, не участвующие в циклической палитре. Исходные сжатые кадры
остаются в GFX для редких точных табличных эффектов.
"""
from pathlib import Path
import struct,zlib,hashlib,json,collections
from prepare_stage1 import BASE
REC=struct.Struct('<IIiiiiI')
FT=struct.Struct('<IIHHHH')
COVER=struct.Struct('<16H')
SILHOUETTE=struct.Struct('<II')

def silhouette(mask,w,h):
    """Клод: 64-битный отпечаток маски 0/1 вместе с размером кадра.
    Кадр с тем же отпечатком на том же месте закрывает прежний целиком (фонарь меню,
    анимации без восстановления фона). Тени (2..5) и однослойные кадры дают 0 — правило не применяется."""
    if mask is None or max(mask)>1:return (0,0)
    v=int.from_bytes(hashlib.blake2b(struct.pack('<ii',w,h)+mask,digest_size=8).digest(),'little') or 1
    return (v&0xffffffff,v>>32)

def opaque_rects(mask,w,h):
    """Codex: четыре непересекающихся непрозрачных прямоугольника, без изменения кадра."""
    if mask is None or not any(mask):return (0,0,w,h)+(0,)*12
    # Codex: блок 4×4 даёт консервативное покрытие; прозрачная точка исключает весь блок.
    step=4;cols=w//step;rows=h//step
    grid=[[all(not any(mask[(y*step+j)*w+x*step:(y*step+j)*w+x*step+step]) for j in range(step))
           for x in range(cols)] for y in range(rows)]
    result=[]
    for _ in range(4):
        heights=[0]*cols;best=(0,0,0,0,0)
        for y,row in enumerate(grid):
            heights=[v+1 if row[x] else 0 for x,v in enumerate(heights)];stack=[]
            for x,v in enumerate(heights+[0]):
                left=x
                while stack and stack[-1][1]>v:
                    left,hh=stack.pop();area=(x-left)*hh
                    if area>best[0]:best=(area,left,y+1-hh,x-left,hh)
                if v and (not stack or stack[-1][1]<v):stack.append((left,v))
        area,x,y,rw,rh=best
        if not area:result.extend([0,0,0,0]);continue
        result.extend([x*step,y*step,rw*step,rh*step])
        for row in grid[y:y+rh]:row[x:x+rw]=[False]*rw
    return tuple(result)

def original_palette():
    agg=Path('E:/zx/HMM2/Assets/Original/DATA/HEROES2.AGG').read_bytes()
    count=struct.unpack_from('<H',agg)[0]
    for i in range(count):
        if agg[-count*15+i*15:][:15].split(b'\0')[0]==b'KB.PAL':
            _,at,size=struct.unpack_from('<III',agg,2+i*12)
            assert size==768
            return agg[at:at+size]
    raise ValueError('KB.PAL missing')

def build():
    out=BASE/'data';source=out/'HMM2.GFX';target=out/'HMM2.FTG'
    data=source.read_bytes();magic,version,icn,til=struct.unpack_from('<4I',data)
    assert magic==0x4b503248 and version==1
    count=icn+til*4;groups=[struct.unpack_from('<II',data,16+i*8) for i in range(count)]
    pal=original_palette();black=pal[:3];mapping=bytes(1 if pal[i*3:i*3+3]==black and i not in range(238,242) else i for i in range(256))
    assert set(mapping).isdisjoint({0,2,3,4,5}) and len(set(mapping))==236
    records=[];total=0
    for offset,n in groups:
        rows=[list(REC.unpack_from(data,offset+i*REC.size))+[None,None,None,(0,0)] for i in range(n)]
        records.append(rows);total+=len(rows)
    # Codex: таблица содержит исходную геометрию и две записи атласа: Blit / Copy.
    record_size=REC.size+2*FT.size+COVER.size+SILHOUETTE.size
    metadata_end=16+256+count*8+total*record_size
    atlases=0;unsupported=0;cells=0
    with target.open('wb+') as f:
        f.seek((metadata_end+511)&~511)
        for group,rows in enumerate(records):
            if group==icn+4:
                # Codex: CLOF32 повторяется по (x+y)%4. Один 128×128 образец
                # сохраняет исходные четыре тайла и позволяет объединять область тумана.
                tiles=[]
                for r in rows:
                    offset,n,w,h,_,_,single=r[:7];assert (w,h,single)==(32,32,1)
                    tiles.append(zlib.decompress(data[offset:offset+n]).translate(mapping))
                assert len(tiles)==4
                at=f.tell();pattern=bytearray()
                for y in range(128):
                    for tx in range(4):pattern+=tiles[(tx+y//32)%4][(y%32)*32:(y%32+1)*32]
                f.write(pattern);atlases+=1;cells+=4
                for r in rows:r[7]=r[8]=(at,len(pattern),128,128,0,5);r[9]=(0,0,32,32)+(0,)*12
                continue
            buckets=collections.defaultdict(list)
            for index,r in enumerate(rows):
                offset,n,w,h,x,y,single=r[:7]
                if not w or not h:
                    r[7]=r[8]=(0,0,0,0,0,0);r[9]=(0,)*16;continue
                raw=zlib.decompress(data[offset:offset+n]);area=w*h
                assert len(raw)==area*(1 if single else 2)
                colors=raw[:area].translate(mapping);mask=None if single else raw[area:]
                r[9]=opaque_rects(mask,w,h);r[10]=silhouette(mask,w,h)
                flags=(1 if single else 0)|(2 if mask and max(mask)>5 else 0)
                if flags&2:unsupported+=1
                # Codex: CELL хранит одинаковую геометрию, строка выровнена до четырёх байт.
                stride=(w+15)&~15 if w<=64 and h<=128 else (w+3)&~3
                cell_height=(h+15)&~15 if w<=64 and h<=128 else h
                def padded(p):return b''.join(p[y*w:(y+1)*w]+bytes(stride-w) for y in range(h))+bytes((cell_height-h)*stride)
                blend=colors if single else bytes(c if not t else t if 2<=t<=5 else 0 for c,t in zip(colors,mask))
                buckets[(stride,cell_height,0)].append((index,0,flags,padded(blend)))
                if single:r[8]='same'
                else:buckets[(stride,cell_height,1)].append((index,1,1,padded(colors)))
            for (stride,h,kind),items in buckets.items():
                per_cell=stride*h;batch=max(1,min(128,16384//per_cell))
                for first in range(0,len(items),batch):
                    part=items[first:first+batch];at=f.tell();assert at%512==0
                    blob=b''.join(item[3] for item in part);size=(len(blob)+511)&~511
                    f.write(blob);f.write(bytes(size-len(blob)));atlases+=1
                    for cell,(index,kind,flags,_) in enumerate(part):
                        rows[index][7+kind]=(at,size,stride,h,cell,flags);cells+=1
            for r in rows:
                if r[8]=='same':r[8]=r[7]
        # Клод: версия 3 — к записи добавлен отпечаток маски (SILHOUETTE).
        f.seek(0);f.write(struct.pack('<4I',0x35475448,3,icn,til));f.write(mapping)
        at=16+256+count*8
        for rows in records:f.write(struct.pack('<II',at,len(rows)));at+=len(rows)*record_size
        for rows in records:
            for r in rows:f.write(REC.pack(*r[:7])+FT.pack(*r[7])+FT.pack(*r[8])+COVER.pack(*r[9])+SILHOUETTE.pack(*r[10]))
    report=dict(file=str(target),bytes=target.stat().st_size,sha256=hashlib.file_digest(target.open('rb'),'sha256').hexdigest(),
                source_sha256=hashlib.sha256(data).hexdigest(),frames=total,atlases=atlases,cells=cells,
                unsupported_transform_frames=unsupported,colors=len(set(mapping)),record_bytes=record_size)
    (out/'ft_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
    return target
if __name__=='__main__':build()
