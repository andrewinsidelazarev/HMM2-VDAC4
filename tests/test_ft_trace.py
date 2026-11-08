"""Codex: воспроизводим настоящие Copy/Blit оригинала командами C-прототипа; пропуски не замалчиваем."""
from pathlib import Path
import collections,ctypes as C,hashlib,json,struct,sys,zlib,re
ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')
OUT=ROOT/'Docs/evidence/ft_prototype'
sys.path.insert(0,'E:/zx/HMM2/Source/Tools')
from icn_transform import SHADOW_ALPHA

class Sprite(C.Structure):
    _fields_=[('source',C.c_uint32),('palette',C.c_uint32)]+[(s,C.c_int32) for s in ['sw','sh','ix','iy','x','y','w','h','flip']]
class Entry(C.Structure):
    _fields_=[('key',C.c_uint64),('bytes',C.c_uint32),('age',C.c_uint32),('page',C.c_uint16),('pages',C.c_uint16),('pins',C.c_uint8),('valid',C.c_uint8)]
class Cache(C.Structure):
    _fields_=[('entries',Entry*1024),('pages',C.c_uint8*768),('age',C.c_uint32),('build_slot',C.c_int)]

def png(path,pixels,w,h):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    raw=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
def palette():
    a=next(a for a in json.loads((ROOT/'Docs/evidence/inventory.json').read_text())['archives'] if Path(a['path']).name=='HEROES2.AGG')
    e=next(e for e in a['entries'] if e['name']=='KB.PAL')
    with Path(a['path']).open('rb') as f:f.seek(e['offset']);raw=f.read(e['bytes'])
    return [tuple(v<<2 for v in raw[i:i+3]) for i in range(0,768,3)]
def signed(v,bits):return v-(1<<bits) if v&(1<<(bits-1)) else v

def raster(words,ram,w,h):
    # Codex: независимый разбор полей DL; аппаратный тайминг и дизеринг эта модель не моделирует.
    rgb=bytearray(w*h*3);source=pal=stride=bh=bw=oh=0;a=256;tc=tf=0
    for word in words:
        op=word>>24
        if op==1:source=word&0x3fffff
        elif op==0x2a:pal=word&0x3fffff
        elif op==7:
            assert (word>>19)&31==16;stride=(stride&3072)|((word>>9)&1023);bh=(bh&1536)|(word&511)
        elif op==0x28:stride=(stride&1023)|(((word>>2)&3)<<10);bh=(bh&511)|((word&3)<<9)
        elif op==8:bw=(bw&1536)|((word>>9)&511);oh=(oh&1536)|(word&511)
        elif op==0x29:bw=(bw&511)|(((word>>2)&3)<<9);oh=(oh&511)|((word&3)<<9)
        elif op==0x15:a=signed(word&0x1ffff,17)
        elif op==0x17:tc=signed(word&0xffffff,24)
        elif op==0x1a:tf=signed(word&0xffffff,24)
        elif word>>30==1:
            ox=signed(word>>15&0x7fff,15)//16;oy=signed(word&0x7fff,15)//16
            for y in range(oh):
                sy=y+tf//256;dy=oy+y
                if not 0<=dy<h or not 0<=sy<bh:continue
                for x in range(bw):
                    sx=(x*a+tc)//256;dx=ox+x
                    if not 0<=dx<w or not 0<=sx<stride:continue
                    i=ram[source+sy*stride+sx];b,g,r,alpha=ram[pal+4*i:pal+4*i+4];at=(dy*w+dx)*3
                    if alpha==255:rgb[at:at+3]=bytes([r,g,b])
                    elif alpha:
                        for k,value in enumerate([r,g,b]):rgb[at+k]=(value*alpha+rgb[at+k]*(255-alpha)+127)//255
    return bytes(rgb)

def main():
    OUT.mkdir(exist_ok=True,parents=True);pal=palette();lib=C.CDLL(str(TMP/'ft/ft_sprite.dll'))
    lib.hft_emit.argtypes=[C.POINTER(C.c_uint32),C.c_uint32,C.POINTER(Sprite)];lib.hft_emit.restype=C.c_uint32
    lib.hft_compact.argtypes=[C.POINTER(C.c_uint32),C.c_uint32];lib.hft_compact.restype=C.c_uint32
    lib.hft_precompose.argtypes=[C.c_void_p,C.c_int,C.c_int,C.POINTER(Sprite),C.c_void_p,C.c_void_p,C.c_int,C.c_void_p,C.c_void_p,C.c_void_p];lib.hft_precompose.restype=C.c_int
    table_text=(ROOT/'third_party/openhmm2/src/engine/image.cpp').read_text(encoding='utf-8').split('const uint8_t transformTable[256 * 16] = {',1)[1].split('};',1)[0]
    table_text=re.sub(r'//[^\n]*','',table_text);table=bytes(map(int,re.findall(r'\b\d+\b',table_text)));assert len(table)==4096
    lib.hft_cache_init.argtypes=[C.POINTER(Cache)];lib.hft_cache_begin.argtypes=[C.POINTER(Cache),C.c_int]
    lib.hft_cache_get.argtypes=[C.POINTER(Cache),C.c_uint64,C.c_uint32,C.POINTER(C.c_uint32),C.POINTER(C.c_int)];lib.hft_cache_get.restype=C.c_int
    cache=Cache();lib.hft_cache_init(C.byref(cache));lib.hft_cache_begin(C.byref(cache),0)
    address=C.c_uint32();upload=C.c_int()
    assert lib.hft_cache_get(C.byref(cache),1,2*1024*1024,C.byref(address),C.byref(upload));visible=address.value
    lib.hft_cache_begin(C.byref(cache),1)
    assert not lib.hft_cache_get(C.byref(cache),2,2*1024*1024,C.byref(address),C.byref(upload))
    assert cache.entries[0].valid and cache.entries[0].page*4096==visible
    lib.hft_cache_begin(C.byref(cache),0)
    assert lib.hft_cache_get(C.byref(cache),2,2*1024*1024,C.byref(address),C.byref(upload))
    results=[]
    for scene,variant in [(s,v) for s in ['menu','map','town','battle'] for v in ['alpha','precompose']]:
        folder=TMP/'runs_trace/KINGDTH';raw=(folder/(scene+'.blits')).read_bytes();magic,w,h=struct.unpack_from('<III',raw);assert magic==0x30304248
        initial=raw[12:12+w*h];pos=12+w*h;ops=[]
        while pos<len(raw):
            values=struct.unpack_from('<12I',raw,pos);pos+=48;sw,sh,*_=values;n=sw*sh
            p=raw[pos:pos+n];pos+=n;t=bytes(n) if values[10] else raw[pos:pos+n]
            if not values[10]:pos+=n
            ops.append((values,p,t))
        history=C.create_string_buffer(initial,len(initial));patch_max=0;patched=0
        if variant=='precompose':
            revised=[]
            for values,p,t in ops:
                sw,sh,ix,iy,ox,oy,bw,bh,flip,copy,single,identified=values
                has_transform=not(copy or single) and any(k>=2 for k in t)
                patch=C.create_string_buffer(bw*bh) if has_transform else None;mask=C.create_string_buffer(bw*bh) if has_transform else None
                s=Sprite(0,0,sw,sh,ix,iy,ox,oy,bw,bh,flip)
                result=lib.hft_precompose(history,w,h,C.byref(s),p,t,copy or single,table,patch,mask);assert result>=0
                if result:
                    revised.append(((bw,bh,0,0,ox,oy,bw,bh,0,0,0,0),patch.raw,mask.raw));patched+=1;patch_max=max(patch_max,bw*bh*2)
                else:revised.append((values,p,t))
            ops=revised
        ram=bytearray();known={};words=[0x02000000,0x26000007,0x1f000001,0x0b000025,0x04000000|0xffffff,0x100000ff,0x18000100]
        counts=collections.Counter();unsupported=collections.Counter();cache=Cache();lib.hft_cache_init(C.byref(cache));lib.hft_cache_begin(C.byref(cache),0);cache_ok=True;allocated=0;packed_bytes=0
        def add(values,p,t):
            nonlocal cache_ok,allocated,packed_bytes
            sw,sh,ix,iy,ox,oy,bw,bh,flip,copy,single,identified=values
            rgba=[];index={};data=bytearray();tables={}
            for pixel,kind in zip(p,t):
                k=0 if copy or single else kind
                key=(pixel,0) if k==0 else (0,k)
                if key not in tables:
                    if k==0:color=(*pal[pixel],255)
                    elif k==1:color=(0,0,0,0)
                    elif k in SHADOW_ALPHA:color=(0,0,0,SHADOW_ALPHA[k]*17)
                    else:color=(0,0,0,0)
                    if color not in index:index[color]=len(rgba);rgba.append(color)
                    tables[key]=index[color]
                data.append(tables[key])
                if k>=6:unsupported[k]+=1
            assert len(rgba)<=256,len(rgba)
            packed=b''.join(bytes([b,g,r,a]) for r,g,b,a in rgba).ljust(1024,b'\0')
            blob=packed+data;blob+=bytes((-len(blob))%4);digest=hashlib.sha256(blob).digest();key=struct.unpack('<Q',digest[:8])[0]
            if digest not in known:
                cache_ok=bool(lib.hft_cache_get(C.byref(cache),key,len(blob),C.byref(address),C.byref(upload)));assert cache_ok
                at=address.value;known[digest]=at;packed_bytes+=len(blob)
                if len(ram)<at+len(blob):ram.extend(bytes(at+len(blob)-len(ram)))
                ram[at:at+len(blob)]=blob
                allocated=sum(cache.pages)*4096
            at=known[digest];sprite=Sprite(at+1024,at,sw,sh,ix,iy,ox,oy,bw,bh,flip)
            out=(C.c_uint32*10)();assert lib.hft_emit(out,10,C.byref(sprite))==10;words.extend(out)
            counts['identified' if identified else 'unidentified']+=1
        add((w,h,0,0,0,0,w,h,0,1,1,0),initial,bytes(w*h))
        for values,p,t in ops:add(values,p,t)
        words+=[0x21000000,0]
        compact=(C.c_uint32*len(words))(*words);compacted=lib.hft_compact(compact,len(words));optimized=list(compact[:compacted])
        rendered=raster(words,ram,w,h);assert rendered==raster(optimized,ram,w,h)
        ref=bytes(c for i in (folder/(scene+'.trace.idx')).read_bytes() for c in pal[i])
        equal=sum(rendered[i:i+3]==ref[i:i+3] for i in range(0,len(ref),3));diff=bytes(c for i in range(0,len(ref),3) for c in ((255,0,255) if rendered[i:i+3]!=ref[i:i+3] else (0,0,0)))
        stem=scene if variant=='alpha' else scene+'_'+variant
        png(OUT/(stem+'_ft.png'),rendered,w,h);png(OUT/(stem+'_original.png'),ref,w,h);png(OUT/(stem+'_diff.png'),diff,w,h)
        # Codex: графические дампы с ресурсами игры остаются во временном каталоге.
        (TMP/'ft'/(stem+'.dl')).write_bytes(struct.pack('<'+'I'*len(words),*words));(TMP/'ft'/(stem+'.ramg')).write_bytes(ram)
        row=dict(scene=scene,blits=len(ops),identified=counts['identified'],unidentified=counts['unidentified'],equal=equal,pixels=w*h,match_percent=round(100*equal/(w*h),6),ramg_bytes=len(ram),ramg_pages_bytes=allocated,cache_fits=cache_ok,dl_bytes=len(words)*4,dl_fits=len(words)*4<=8192,unsupported_transform_pixels=dict(unsupported),initial_frame_bytes=w*h,unique_textures=len(known),cold_payload_bytes=len(ram)+len(words)*4,warm_dl_bytes=len(words)*4)
        row.update(variant=variant,ramg_bytes=packed_bytes,ramg_span_bytes=len(ram),cold_payload_bytes=packed_bytes+compacted*4,compact_dl_bytes=compacted*4,compact_dl_fits=compacted*4<=8192,cpu_history_bytes=w*h if variant=='precompose' else 0,patch_bytes_max=patch_max,precomposed_draws=patched)
        results.append(row);print(row,flush=True)
    result=dict(scopes='Copy/Blit/Fill в Display; исходное состояние кадра сохранено; прочие операции не перенесены; это модель DL, не аппаратный прогон',cache_metadata_host_bytes=C.sizeof(Cache),pinned_eviction_test=True,scenes=results)
    (OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    assert all(r['equal']==r['pixels'] and r['cache_fits'] and r['compact_dl_fits'] for r in results if r['variant']=='precompose')
if __name__=='__main__':main()
