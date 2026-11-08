"""Codex: измеряем LTO на полном движке, не подменяя его минимальной точкой входа."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import subprocess
from compile_probe import ROOT,SRC,TC,SDK,OUT as BASE

OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/lto')
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    inc=[BASE/'headers',ROOT/'tools/probe_include',SRC/'engine',SRC/'thirdparty/libsmacker',SDK,SDK/'SDL2']
    inc+=sorted(p for p in (SRC/'fheroes2').iterdir() if p.is_dir())
    flags=['-std=c++17','-Os','-DNDEBUG','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-mlongcalls','-fno-strict-aliasing','-fpermissive','-flto']+['-I'+str(p) for p in inc]
    rows=json.loads((ROOT/'Docs/evidence/compile_probe.json').read_text(encoding='utf-8'))
    def build(r):
        source=BASE/('staged_'+r['source'].replace('/','_'));obj=OUT/(r['source'].replace('/','_')+'.o')
        cmd=[str(TC/'xtensa-esp32s3-elf-g++.exe'),*flags,'-c',str(source),'-o',str(obj)]
        signature=hashlib.sha256(source.read_bytes()+repr(cmd).encode()).hexdigest();stamp=obj.with_suffix('.sha256')
        if obj.exists() and stamp.exists() and stamp.read_text()==signature:return obj,0
        p=subprocess.run(cmd,capture_output=True);obj.with_suffix('.log').write_bytes(p.stdout+p.stderr)
        if not p.returncode:stamp.write_text(signature)
        else:print(source.name,p.stderr.decode(errors='replace')[-600:],flush=True)
        return obj,p.returncode
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:result=list(pool.map(build,rows))
    assert not any(r[1] for r in result)
    print('LTO: скомпилировано',len(result),flush=True)
    for mode,opts in [('all',[]),('no_editor',[])]:
        if mode=='no_editor':continue
        args=['-nostdlib','-nostartfiles','-shared','-fPIC','-Os','-flto=6','-Wl,-e,main','-Wl,--gc-sections','-Wl,-Bsymbolic',
              *[str(r[0]) for r in result],str(BASE/'smacker.o'),'-o',str(OUT/(mode+'.elf')),'-Wl,-Map,'+str(OUT/(mode+'.map'))]
        rsp=OUT/(mode+'.rsp');rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args),encoding='utf-8')
        p=subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),'@'+str(rsp)],capture_output=True)
        (OUT/(mode+'.log')).write_bytes(p.stdout+p.stderr)
        print('LTO link',mode,p.returncode,p.stderr.decode(errors='replace')[-1000:],flush=True)
        if not p.returncode:
            sec=subprocess.check_output([str(TC/'xtensa-esp32s3-elf-size.exe'),'-A',str(OUT/(mode+'.elf'))],text=True)
            (OUT/(mode+'_size.txt')).write_text(sec,encoding='utf-8');print(sec,flush=True)
    return 0
if __name__=='__main__':raise SystemExit(main())
