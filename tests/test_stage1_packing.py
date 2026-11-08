"""Codex: упаковка обратима побайтно; адреса данных повторяют правила штатного загрузчика."""
from pathlib import Path
import struct,json,sys,hashlib,random,zlib
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from pack_stage1_module import elf_read
BASE=Path(sys.argv[1]) if len(sys.argv)>1 else Path('E:/zx/cache/tmp/hmm2_esp/stage1/module')

def main():
    raw=(BASE/'HMM2.unpacked.ELF').read_bytes();packed=(BASE/'HMM2.ELF').read_bytes()
    _,sh,n=elf_read(raw);_,ph,pn=elf_read(packed)
    def data(b,s):return b[s[4]:s[4]+s[5]]
    original=data(raw,sh[n['.data']]);blob=data(packed,ph[pn['.rodata']]);h=struct.unpack_from('<12I',blob)
    decoded=zlib.decompress(blob[h[11]:h[11]+h[2]],-15);assert decoded==original
    assert data(raw,sh[n['.text']])==data(packed,ph[pn['.text']])
    assert data(raw,sh[n['.rela.plt']])==data(packed,ph[pn['.rela.plt']])
    old=list(struct.iter_unpack('<IIi',data(raw,sh[n['.rela.dyn']])));new=list(struct.iter_unpack('<IIi',data(packed,ph[pn['.rela.dyn']])))
    ds=sh[n['.data']];expected=sorted(o-ds[3] for o,i,a in old if ds[3]<=o<ds[3]+ds[5]);assert len(expected)==h[3]
    assert new==[(o,i,a) for o,i,a in old if not ds[3]<=o<ds[3]+ds[5]]
    offsets=[];offset=0;pos=h[11]+h[2]
    for _ in range(h[3]):
        delta=shift=0
        while True:
            b=blob[pos];pos+=1;delta|=(b&127)<<shift;shift+=7
            if not b&128:break
        offset+=delta;offsets.append(offset)
    assert offsets==expected and pos==h[11]+h[2]+h[4] and len(set(offsets))==len(offsets)
    rng=random.Random(451)
    for _ in range(64):
        textbase=rng.randrange(0x42000000,0x42800000,16);dbase=rng.randrange(0x3c000000,0x3c800000,16)
        actual=bytearray(decoded);expected_data=bytearray(original)
        for off in offsets:
            v=struct.unpack_from('<I',actual,off)[0]
            if h[5]<=v<h[5]+h[6]:v=textbase+v-h[5]
            elif h[7]<=v<=h[7]+h[8]:v=dbase+v-h[7]
            else:raise AssertionError(hex(v))
            struct.pack_into('<I',actual,off,v)
        for o,info,add in old:
            if not ds[3]<=o<ds[3]+ds[5]:continue
            assert info==5 and add==0
            off=o-ds[3];v=struct.unpack_from('<I',original,off)[0];target=0
            for vs,sz,base in [(h[5],h[6],textbase),(h[7],h[8],dbase)]:
                if vs<=v<vs+sz:target=base+v-vs;break
            if not target:
                for vs,sz,base in [(h[5],h[6],textbase),(h[7],h[8],dbase)]:
                    if vs<=v-1<vs+sz:target=base+v-vs;break
            assert target;struct.pack_into('<I',expected_data,off,target)
        assert actual==expected_data
    report=dict(elf_sha256=hashlib.sha256(packed).hexdigest(),initialized_bytes_exact=len(decoded),text_bytes_exact=ph[pn['.text']][5],relative_data_relocations=len(offsets),address_layouts=64,constructors_and_unwind_preserved=True)
    (BASE/'packing_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
if __name__=='__main__':main()
