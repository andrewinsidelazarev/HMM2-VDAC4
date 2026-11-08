"""Codex: изменить размер атласных порций без повторной обработки исходных пикселей."""
from pathlib import Path
import argparse,collections,hashlib,json,struct
from prepare_stage1 import BASE
FT=struct.Struct('<IIHHHH');REC_BYTES=100

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--limit',type=int,default=16384);a=ap.parse_args();assert a.source.resolve()!=a.out.resolve() and a.limit%512==0
    raw=a.source.read_bytes();magic,version,icn,til=struct.unpack_from('<4I',raw);assert (magic,version)==(0x35475448,3)
    groups=[struct.unpack_from('<2I',raw,272+i*8) for i in range(icn+til*4)];metadata_end=max(o+n*REC_BYTES for o,n in groups);meta=bytearray(raw[:metadata_end]);atlases=cells=0
    def cell(desc):
        at,size,stride,h,index,flags=desc;n=stride*h;assert (index+1)*n<=size
        return raw[at+index*n:at+(index+1)*n]
    with a.out.open('wb+') as f:
        f.seek((metadata_end+511)&~511)
        for g,(offset,count) in enumerate(groups):
            if g==icn+4:
                desc=FT.unpack_from(raw,offset+28);at=f.tell();blob=raw[desc[0]:desc[0]+desc[1]];f.write(blob);atlases+=1
                for i in range(count):
                    for k in (0,1):
                        values=list(FT.unpack_from(raw,offset+i*REC_BYTES+28+k*16));values[0]=at;FT.pack_into(meta,offset+i*REC_BYTES+28+k*16,*values);cells+=1
                continue
            buckets=collections.defaultdict(list);same=[]
            for i in range(count):
                pos=offset+i*REC_BYTES;d0=FT.unpack_from(raw,pos+28);d1=FT.unpack_from(raw,pos+44)
                if not d0[1]:continue
                for k,d in enumerate((d0,d1)):
                    if k and d==d0:same.append(pos);continue
                    buckets[d[2],d[3],k].append((pos+28+k*16,d))
            for (stride,h,kind),items in buckets.items():
                size=stride*h;batch=max(1,min(128,a.limit//size))
                for begin in range(0,len(items),batch):
                    part=items[begin:begin+batch];at=f.tell();blob=b''.join(cell(d) for _,d in part);padded=(len(blob)+511)&~511;f.write(blob);f.write(bytes(padded-len(blob)));atlases+=1
                    for index,(pos,d) in enumerate(part):FT.pack_into(meta,pos,at,padded,stride,h,index,d[5]);cells+=1
            for pos in same:meta[pos+44:pos+60]=meta[pos+28:pos+44]
        f.seek(0);f.write(meta)
    changed=a.out.read_bytes();checked=0
    for offset,count in groups:
        for i in range(count):
            pos=offset+i*REC_BYTES;assert raw[pos:pos+28]==changed[pos:pos+28] and raw[pos+60:pos+100]==changed[pos+60:pos+100]
            for k in (0,1):
                old=FT.unpack_from(raw,pos+28+k*16);new=FT.unpack_from(changed,pos+28+k*16)
                assert old[2:4]==new[2:4] and old[5]==new[5]
                if not old[1]:assert old==new;continue
                length=new[2]*new[3];assert cell(old)==changed[new[0]+new[4]*length:new[0]+(new[4]+1)*length];checked+=1
    result=dict(file=str(a.out),bytes=len(changed),sha256=hashlib.sha256(changed).hexdigest(),source_sha256=hashlib.sha256(raw).hexdigest(),atlas_batch_bytes=a.limit,atlases=atlases,cells=cells,checked_cell_references=checked,all_cell_bytes_exact=True,metadata_exact=True)
    (BASE/'data/ft_repack_check.json').write_text(json.dumps(result,indent=2),encoding='utf-8');(BASE/'data/ft_manifest.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
