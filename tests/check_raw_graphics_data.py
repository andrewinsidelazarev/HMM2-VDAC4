"""Codex: независимая проверка всех исходных пикселей/масок и неизменности остальных файлов DAT."""
from pathlib import Path
import hashlib,json,struct,zlib,sys
ROOT=Path('E:/zx/cache/tmp/hmm2_esp');before=ROOT/'stage1/data/HMM2.DAT';after=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'integration_audio/raw58_data/HMM2.DAT'
def files(path):
 f=path.open('rb');h=f.read(16);n=struct.unpack_from('<I',h,8)[0];rows={}
 for _ in range(n):
  b=f.read(72);rows[b[:64].split(b'\0',1)[0].decode()]=struct.unpack_from('<II',b,64)
 return f,rows
def read(f,r):f.seek(r[0]);b=f.read(r[1]);assert len(b)==r[1];return b
a,old=files(before);b,new=files(after);assert old.keys()==new.keys()
b.seek(0);version=struct.unpack('<4I',b.read(16))[1];prefixes=0
if version==2:
    b.seek(16)
    descriptors=[b.read(72) for _ in range(len(new))]
    for row in descriptors:
        name=row[:60].split(b'\0',1)[0].decode();prefix=struct.unpack_from('<I',row,60)[0]
        if prefix:
            assert name.startswith('maps/') and prefix%512==0
            expected=read(b,new[name])[:512];b.seek(prefix);assert b.read(len(expected))==expected;prefixes+=1
    assert prefixes==sum(name.startswith('maps/') for name in new)
unchanged=0
for name,r in old.items():
 if name in ('hmm2.gfx','hmm2.ftg'):continue
 assert r[1]==new[name][1] and read(a,r)==read(b,new[name]),name
 unchanged+=1
gx,raw=read(a,old['hmm2.gfx']),read(b,new['hmm2.gfx']);ft,rf=read(a,old['hmm2.ftg']),read(b,new['hmm2.ftg'])
magic,v,icn,til=struct.unpack_from('<4I',gx);assert struct.unpack_from('<4I',raw)==(magic,2,icn,til)
assert struct.unpack_from('<4I',rf)==(0x35475448,4,icn,til)
frames=pixels=0;end=0
for g in range(icn+til*4):
 at,n=struct.unpack_from('<II',gx,16+g*8);ra,rn=struct.unpack_from('<II',raw,16+g*8);assert rn==n
 fa,fn=struct.unpack_from('<II',ft,272+g*8);assert fn==n and (fa,fn)==struct.unpack_from('<II',rf,272+g*8);end=max(end,fa+n*100)
 for i in range(n):
  o,size,w,h,x,y,single=struct.unpack_from('<IIiiiiI',gx,at+i*28);ro,rs,*dims=struct.unpack_from('<IIiiiiI',raw,ra+i*28)
  assert dims==[w,h,x,y,single] and rs==w*h*(1 if single else 2)
  assert not rs or ro%512==0
  expected=zlib.decompress(gx[o:o+size]) if size else b'';assert raw[ro:ro+rs]==expected
  assert struct.unpack_from('<II',rf,fa+i*100)==(ro,rs)
  assert rf[fa+i*100+8:fa+(i+1)*100]==ft[fa+i*100+8:fa+(i+1)*100]
  pixels+=rs;frames+=1
assert rf[end:]==ft[end:]
result=dict(frames=frames,exact_raw_pixel_mask_bytes=pixels,unchanged_files=unchanged,ft_gpu_payload_unchanged=True,fs_version=version,map_header_copies=prefixes,frame_alignment=512,bytes=after.stat().st_size,sha256=hashlib.file_digest(after.open('rb'),'sha256').hexdigest())
(after.parent/'check.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
