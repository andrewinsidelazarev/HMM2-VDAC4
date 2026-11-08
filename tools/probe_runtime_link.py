"""Codex: изолируем причину отказа библиотек и сохраняем отрицательные проверки без изменения toolchain."""
from pathlib import Path
import subprocess
import json
from compile_probe import ROOT,TC

OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/runtime_probe')
def run(args,log):
    p=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True)
    (OUT/log).write_bytes(p.stdout+p.stderr)
    return p
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    g=TC/'xtensa-esp32s3-elf-g++.exe';copy=TC/'xtensa-esp32s3-elf-objcopy.exe'
    assert run([g,'-std=c++17','-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-c',ROOT/'tests/runtime_min.cpp','-o','min.o'],'compile.log').returncode==0
    libs=[]
    for name in ['stdc++','supc++','c','m','gcc']:
        path=Path(subprocess.check_output([str(g),'-print-file-name=lib'+name+'.a'],text=True).strip())
        dst=OUT/('lib'+name+'.a')
        p=run([copy,'--set-section-flags','.rodata*=alloc,load,data,contents','--set-section-flags','.literal*=alloc,load,data,contents',
               '--set-section-flags','.eh_frame=alloc,load,data,contents','--set-section-flags','.gcc_except_table*=alloc,load,data,contents',path,dst],name+'_copy.log')
        assert p.returncode==0
        libs.append(dst)
    results=[]
    for mode in ['normal','writable']:
        libraries=['-l'+n for n in ['stdc++','supc++','c','m','gcc']] if mode=='normal' else libs
        p=run([g,'-nostdlib','-nostartfiles','-shared','-fPIC','-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,-e,app_entry','min.o',
               '-Wl,--start-group',*libraries,'-Wl,--end-group','-o',mode+'.elf'],mode+'.log')
        results.append(dict(mode=mode,code=p.returncode,diagnostic=(p.stdout+p.stderr).decode(errors='replace')[-1400:]))
    print(json.dumps(results,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
