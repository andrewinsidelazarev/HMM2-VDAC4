"""Codex: упаковка инициализаторов данных внутри обычного ELF, без изменения загрузчика.

Код и его перемещения остаются штатными. Вход приложения восстанавливает данные
в .bss до конструкторов; формат raw DEFLATE, перемещения данных — дельты ULEB128.
"""
from pathlib import Path
import struct, json, hashlib, zlib,sys
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix5/python')
import zopfli.zlib

def elf_read(raw):
    h=list(struct.unpack_from('<16sHHIIIIIHHHHHH',raw)); sh=[list(struct.unpack_from('<10I',raw,h[6]+40*i)) for i in range(h[12])]
    ns=sh[h[13]];names=raw[ns[4]:ns[4]+ns[5]]
    return h,sh,{names[s[0]:].split(b'\0')[0].decode():i for i,s in enumerate(sh)}

def pack(path):
    raw=path.read_bytes(); h,sh,names=elf_read(raw)
    def sec(n):return sh[names[n]]
    def payload(s):return raw[s[4]:s[4]+s[5]] if s[1]!=8 else b''
    ds,bs,ts,ps=map(sec,['.data','.bss','.text','.rodata']);data=payload(ds)
    relocs=[];parts=[payload(s) for s in sh]
    for name in ['.rela.dyn','.rela.plt']:
        keep=bytearray();s=sec(name)
        for off,info,add in struct.iter_unpack('<IIi',payload(s)):
            if ds[3]<=off<ds[3]+ds[5]:
                assert info==5 and add==0,(off,info,add)
                relocs.append(off-ds[3])
            else:keep+=struct.pack('<IIi',off,info,add)
        parts[names[name]]=bytes(keep);s[5]=len(keep)
    reloc=bytearray();prev=0
    for off in sorted(relocs):
        delta=off-prev;prev=off
        while delta>=128:reloc.append((delta&127)|128);delta>>=7
        reloc.append(delta)
    # Format3 compresses the relocation-position metadata with the data.
    # Only existing zero-initialized BSS is used temporarily, then cleared.
    assert len(data)+len(reloc)<=bs[3]+bs[5]-ds[3]
    combined=data+reloc
    packed=zopfli.zlib.compress(combined,numiterations=200,blocksplittingmax=0)[2:-4]
    assert zlib.decompress(packed,-15)==combined
    assert len(packed)>=192*1024,'Recycled audio history requires192KiB'
    original_resident=ts[5]+ds[5]+bs[5]+ps[5]
    memsize=bs[3]+bs[5]-ds[3]
    boot=payload(ps);boot+=bytes((-len(boot))%16)
    header=struct.pack('<12I',0x314b5048,ds[5],len(packed),len(relocs),len(reloc),ts[3],ts[5],ds[3],memsize,h[4],3,len(boot))
    parts[names['.rodata']]=header+boot[48:]+packed
    parts[names['.rodata']]+=bytes((-len(parts[names['.rodata']]))%16)
    ps[5]=len(parts[names['.rodata']])
    # Codex: единая .bss сохраняет исходные виртуальные адреса данных и промежуток выравнивания.
    bs[3]=ds[3];bs[5]=memsize
    ds[1]=8;ds[2]=0;ds[5]=0;parts[names['.data']]=b''
    # Codex: секционная загрузка штатного VDAC4 не использует программные заголовки.
    h[5]=0;h[9]=0;h[10]=0
    out=bytearray(52)
    for i,s in enumerate(sh):
        if i==0:continue
        align=max(1,s[8]);out+=bytes((-len(out))%align);s[4]=len(out)
        if s[1]!=8:out+=parts[i]
    out+=bytes((-len(out))%4);h[6]=len(out)
    for s in sh:out+=struct.pack('<10I',*s)
    struct.pack_into('<16sHHIIIIIHHHHHH',out,0,*h)
    path.write_bytes(out)
    result=dict(before=len(raw),after=len(out),data_raw=len(data),data_deflate=len(packed),bootstrap_data_bytes=len(boot),data_relocations=len(relocs),relocation_bytes=len(reloc),decoded_bytes=len(combined),packed_format=3)
    result['compressor']='zopfli 0.4.3, 200 iterations, unlimited block splitting, raw DEFLATE'
    result['resident_bytes']=ts[5]+ps[5]+memsize
    result['resident_extra']=result['resident_bytes']-original_resident
    result['sha256']=hashlib.sha256(out).hexdigest()
    path.with_suffix('.pack.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result

if __name__=='__main__':
    import sys
    print(json.dumps(pack(Path(sys.argv[1])),indent=2))
