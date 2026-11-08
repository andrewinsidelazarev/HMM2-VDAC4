"""Codex: настоящий автономный распаковщик — данные ELF, три вида DEFLATE и обрыв входа."""
from pathlib import Path
import sys,ctypes as C,subprocess,json,zlib,random,hashlib,struct
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from prepare_stage1_puff import prepare_puff
from build_stage1 import VC,KIT,BASE
from pack_stage1_module import elf_read
OUT=BASE.parent/'fix1/puff_test'
def main():
    OUT.mkdir(parents=True,exist_ok=True);source=prepare_puff()
    cc=VC/'bin/Hostx64/x64/cl.exe';inc=[VC/'include',KIT/'Include/10.0.26100.0/ucrt',KIT/'Include/10.0.26100.0/um',KIT/'Include/10.0.26100.0/shared']
    libs=[VC/'lib/x64',KIT/'Lib/10.0.26100.0/ucrt/x64',KIT/'Lib/10.0.26100.0/um/x64']
    args=[cc,'/nologo','/LD','/O2','/MT','/utf-8',*['/I'+str(p) for p in inc],source,'/Fo'+str(OUT/'puff.obj'),'/Fe'+str(OUT/'puff.dll'),'/link','/EXPORT:puff',*['/LIBPATH:'+str(p) for p in libs]]
    r=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
    dll=C.CDLL(str(OUT/'puff.dll'));fn=dll.puff;fn.argtypes=[C.c_void_p,C.POINTER(C.c_ulong),C.c_void_p,C.POINTER(C.c_ulong)];fn.restype=C.c_int
    cases=[]
    def check(encoded,expected,label):
        target=C.create_string_buffer(len(expected));inp=C.create_string_buffer(encoded);n=C.c_ulong(len(expected));m=C.c_ulong(len(encoded))
        status=fn(target,C.byref(n),inp,C.byref(m));assert status==0 and n.value==len(expected) and target.raw==expected and m.value==len(encoded),label
        for cut in [0,1,len(encoded)//2,len(encoded)-1]:
            m=C.c_ulong(cut);n=C.c_ulong(len(expected));assert fn(target,C.byref(n),inp,C.byref(m))!=0,(label,cut)
        cases.append(dict(case=label,encoded=len(encoded),decoded=len(expected),truncations=4))
    elf=(BASE/'module/HMM2.ELF').read_bytes();_,sh,ns=elf_read(elf);s=sh[ns['.rodata']];data=elf[s[4]:s[4]+s[5]];h=struct.unpack_from('<12I',data)
    encoded=data[h[11]:h[11]+h[2]];check(encoded,zlib.decompress(encoded,-15),'ELF')
    rng=random.Random(112);raw=bytes(rng.randrange(32) for _ in range(100000))+b'HMM2'*50000
    for label,level,strategy in [('stored',0,zlib.Z_DEFAULT_STRATEGY),('fixed',9,zlib.Z_FIXED),('dynamic',9,zlib.Z_DEFAULT_STRATEGY)]:
        compressor=zlib.compressobj(level,zlib.DEFLATED,-15,8,strategy);check(compressor.compress(raw)+compressor.flush(),raw,label)
    result=dict(elf_sha256=hashlib.sha256(elf).hexdigest(),puff_adaptation_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),cases=cases)
    (ROOT/'Docs/evidence/stage1_fix1_puff.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
