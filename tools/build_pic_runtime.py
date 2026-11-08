"""Codex: пересобираем исходники GCC в PIC; штатные архивы остаются неизменными."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import re
import subprocess
from compile_probe import TC,ROOT

BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')
SRC=BASE/'runtime_sources/gcc-15.2.0/libstdc++-v3'
OUT=BASE/'pic_runtime'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    sources=list((SRC/'libsupc++').glob('*.cc'))
    for d in ['c++98','c++11','c++17']:
        sources+=list((SRC/'src'/d).glob('*.cc'))
    sources=[p for p in sources if p.name not in ['vtv_stubs.cc','compatibility.cc','hash-long-double-tr1-aux.cc'] and 'fs_' not in p.name]
    def build(p):
        rel=p.relative_to(SRC).as_posix(); obj=OUT/(rel.replace('/','_')+'.o')
        std='gnu++98' if p.parent.name=='c++98' else 'gnu++11' if p.parent.name=='c++11' else 'gnu++17'
        inc=[SRC/'libsupc++',SRC/'src',SRC/'src/c++11',SRC/'include',SRC.parent/'include',SRC.parent/'libgcc']
        cmd=[str(TC/'xtensa-esp32s3-elf-g++.exe'),'-std='+std,'-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-fchar8_t','-D_GNU_SOURCE',*['-I'+str(i) for i in inc],'-c',str(p),'-o',str(obj)]
        sig=hashlib.sha256(p.read_bytes()+repr(cmd).encode()).hexdigest();stamp=obj.with_suffix('.sha256')
        if obj.exists() and stamp.exists() and stamp.read_text()==sig:return dict(source=rel,ok=True,object=str(obj))
        r=subprocess.run(cmd,capture_output=True);obj.with_suffix('.log').write_bytes(r.stdout+r.stderr)
        if not r.returncode:stamp.write_text(sig)
        else:print(rel,r.stderr.decode(errors='replace')[-800:],flush=True)
        return dict(source=rel,ok=not r.returncode,object=str(obj))
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=list(pool.map(build,sources))
    (OUT/'compile.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    good=[r['object'] for r in rows if r['ok']]
    # Codex: это диагностический архив; неудачные единицы явно перечислены в журнале.
    rsp=OUT/'archive.rsp';rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in ['rcs',str(OUT/'libstdcpp_pic.a'),*good]),encoding='utf-8')
    subprocess.run([str(TC/'xtensa-esp32s3-elf-ar.exe'),'@'+str(rsp)],check=True)
    print('PIC: успешно',len(good),'из',len(rows),flush=True)
    args=[str(TC/'xtensa-esp32s3-elf-g++.exe'),'-nostdlib','-nostartfiles','-shared','-fPIC','-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,-e,app_entry',str(BASE/'runtime_probe/min.o'),str(OUT/'libstdcpp_pic.a'),'-o',str(OUT/'min.elf')]
    r=subprocess.run(args,capture_output=True);(OUT/'min_link.log').write_bytes(r.stdout+r.stderr)
    print('PIC link',r.returncode,r.stderr.decode(errors='replace')[-2000:])
    return 0
if __name__=='__main__':raise SystemExit(main())
