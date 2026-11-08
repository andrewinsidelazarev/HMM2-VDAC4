"""Codex: несжатые исходные пиксели/маски с адресуемыми кадрами, без изменения FT-атласов."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
BASE=Path('E:/zx/cache/tmp/hmm2_esp')
def sha(p):return hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--map-headers',action='store_true');ap.add_argument('--source',type=Path,default=BASE/'stage1/data/HMM2.DAT');ap.add_argument('--out',type=Path,default=BASE/'integration_audio/raw58_data');a=ap.parse_args();a.out.mkdir(exist_ok=True)
    with a.source.open('rb') as f:
        header=f.read(16);count=struct.unpack_from('<I',header,8)[0];rows=[]
        for i in range(count):
            b=f.read(72);rows.append(dict(name=b[:64].split(b'\0',1)[0].decode(),offset=struct.unpack_from('<I',b,64)[0],size=struct.unpack_from('<I',b,68)[0],name_bytes=b[:64]))
        byname={r['name']:r for r in rows}
        def payload(name):
            r=byname[name];f.seek(r['offset']);b=f.read(r['size']);assert len(b)==r['size'];return b
        gfx=payload('hmm2.gfx');ftg=payload('hmm2.ftg')
        magic,version,icn,til=struct.unpack_from('<4I',gfx);assert magic==0x4b503248 and version==1
        groups=[struct.unpack_from('<II',gfx,16+i*8) for i in range(icn+til*4)]
        # Codex: таблицы исходного GFX были перемежены с zlib-потоками. Новая таблица компактна.
        cursor=16+len(groups)*8;raw_groups=[]
        for at,n in groups:raw_groups.append((cursor,n));cursor+=n*28
        meta=bytearray(cursor);struct.pack_into('<4I',meta,0,magic,2,icn,til)
        for g,pair in enumerate(raw_groups):struct.pack_into('<II',meta,16+g*8,*pair)
        fm,fv,fi,ft=struct.unpack_from('<4I',ftg);assert (fm,fv,fi,ft)==(0x35475448,3,icn,til)
        fg=[struct.unpack_from('<II',ftg,272+i*8) for i in range(icn+til*4)]
        ft_end=max(at+n*100 for at,n in fg);ft_meta=bytearray(ftg[:ft_end]);struct.pack_into('<I',ft_meta,4,4)
        pixels=frames=0;raw_path=a.out/'HMM2.GFX'
        with raw_path.open('wb+') as out:
            out.write(meta)
            for group,(at,n) in enumerate(groups):
                assert fg[group][1]==n
                for i in range(n):
                    rec=list(struct.unpack_from('<IIiiiiI',gfx,at+i*28));old,size,w,h,x,y,single=rec
                    data=zlib.decompress(gfx[old:old+size]) if size else b''
                    assert len(data)==w*h*(1 if single else 2)
                    position=(out.tell()+511)&~511 if data else 0
                    if data:out.seek(position);out.write(data)
                    struct.pack_into('<IIiiiiI',meta,raw_groups[group][0]+i*28,position,len(data),w,h,x,y,single)
                    ft_rec=fg[group][0]+i*100;assert tuple(rec)==struct.unpack_from('<IIiiiiI',ft_meta,ft_rec)
                    struct.pack_into('<II',ft_meta,ft_rec,position,len(data));pixels+=len(data);frames+=1
            out.seek(0);out.write(meta)
        ft_path=a.out/'HMM2.FTG'
        with ft_path.open('wb') as out:out.write(ft_meta);out.write(ftg[ft_end:])
        # Codex: сохраняем вложенные SFX-алиасы AGG; меняются только физические смещения.
        roots=[]
        for r in sorted(rows,key=lambda r:(r['offset'],-r['size'])):
            if not r['size']:continue
            if roots and r['offset']+r['size']<=roots[-1]['offset']+roots[-1]['size']:continue
            assert not roots or r['offset']>=roots[-1]['offset']+roots[-1]['size']
            roots.append(r)
        replacements={'hmm2.gfx':raw_path,'hmm2.ftg':ft_path};locations=[];target=a.out/'HMM2.DAT'
        with target.open('wb+') as out:
            out.seek((16+count*72+511)&~511)
            map_headers={}
            if a.map_headers:
                # Codex: заголовки карты читаются списком, поэтому их точные копии идут подряд.
                for r in rows:
                    if not r['name'].startswith('maps/'):continue
                    position=(out.tell()+511)&~511;out.seek(position);f.seek(r['offset'])
                    data=f.read(min(512,r['size']));out.write(data.ljust(512,b'\0'));map_headers[r['name']]=position
                header=bytearray(header);struct.pack_into('<I',header,4,2)
            for r in roots:
                position=(out.tell()+511)&~511;out.seek(position)
                changed=replacements.get(r['name']);source=changed.open('rb') if changed else f
                if not changed:f.seek(r['offset'])
                left=changed.stat().st_size if changed else r['size'];size=left
                while left:
                    b=source.read(min(left,1048576));assert b;out.write(b);left-=len(b)
                if changed:source.close()
                locations.append((r,position,size))
            out.seek(0);out.write(header)
            for r in rows:
                if r['size']:
                    owner,position,size=next(v for v in locations if v[0]['offset']<=r['offset'] and r['offset']+r['size']<=v[0]['offset']+v[0]['size'])
                    offset=position+r['offset']-owner['offset'];length=size if r is owner else r['size']
                    assert r is owner or owner['name'] not in replacements
                else:offset=length=0
                name=r['name_bytes']
                if a.map_headers:
                    encoded=r['name'].encode();assert len(encoded)<60
                    name=encoded.ljust(60,b'\0')+struct.pack('<I',map_headers.get(r['name'],0))
                out.write(name+struct.pack('<II',offset,length))
    result=dict(source=str(a.source),source_sha256=sha(a.source),target=str(target),bytes=target.stat().st_size,sha256=sha(target),frames=frames,raw_pixel_bytes=pixels,map_header_copies=len(map_headers),fs_version=2 if a.map_headers else 1,frame_alignment=512,ftg_version=4,gfx_version=2,ogg_sfx_unchanged=True)
    (a.out/'build.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
