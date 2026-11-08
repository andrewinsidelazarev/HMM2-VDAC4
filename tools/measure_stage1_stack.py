"""Codex: отчёт GCC о кадрах стека; runtime пересобирается только для измерения в tmp."""
from pathlib import Path
import concurrent.futures,hashlib,json,re,subprocess
from compile_probe import ROOT,TC
from prepare_stage1 import BASE
from build_pic_runtime import SRC as RS,OUT as RT
OUT=BASE.parent/'fix2/stack_runtime'

def rows(folder):
    result=[]
    for p in folder.glob('*.su'):
        for line in p.read_text(encoding='utf-8').splitlines():
            v=line.split('\t')
            if len(v)==3:result.append(dict(function=v[0],bytes=int(v[1]),kind=v[2]))
    return sorted(result,key=lambda r:r['bytes'],reverse=True)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    used=set(re.findall(r'libstdcpp_pic\.a\(([^)]+)\)',(BASE/'module/HMM2.map').read_text()))
    sources=[RS/r['source'] for r in json.loads((RT/'compile.json').read_text()) if Path(r['object']).name in used]
    cpp_inc=[RS/'libsupc++',RS/'src',RS/'src/c++11',RS/'include',RS.parent/'include',RS.parent/'libgcc']
    jobs=[]
    for p in sources:
        std='gnu++98' if p.parent.name=='c++98' else 'gnu++11' if p.parent.name=='c++11' else 'gnu++17'
        jobs.append((p,['-std='+std,'-fchar8_t','-D_GNU_SOURCE'],cpp_inc,'g++'))
    gcc=RS.parent
    unwind_inc=[RT/'config',gcc/'gcc',gcc/'include',gcc/'libgcc',gcc/'libgcc/config/xtensa',TC.parent/'xtensa-esp-elf/include/c++/15.2.0/xtensa-esp-elf/esp32s3/bits']
    for p in [gcc/'libgcc/config/xtensa/unwind-dw2-xtensa.c',gcc/'libgcc/unwind-dw2-fde.c']:
        jobs.append((p,['-std=gnu11','-DIN_LIBGCC2'],unwind_inc,'gcc'))
    zlib=BASE/'runtime/zlib-1.3.2'
    for name in ['adler32','crc32','compress','deflate','trees','uncompr','inflate','inftrees','inffast','zutil']:
        jobs.append((zlib/(name+'.c'),['-std=gnu11','-fvisibility=hidden','-fexceptions'],[zlib],'gcc'))
    jobs.append((gcc/'libgcc/unwind-c.c',['-std=gnu11','-fvisibility=hidden','-fexceptions','-DIN_LIBGCC2'],[BASE/'runtime',*unwind_inc],'gcc'))
    def compile(job):
        p,flags,includes,cc=job;obj=OUT/(p.parent.name+'_'+p.stem+'.o')
        cmd=[TC/('xtensa-esp32s3-elf-'+cc+'.exe'),'-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-fstack-usage','-Wstack-usage=2048',*flags,*['-I'+str(i) for i in includes],'-c',p,'-o',obj]
        r=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True);obj.with_suffix('.log').write_bytes(r.stdout+r.stderr)
        assert not r.returncode,r.stderr.decode(errors='replace')
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(compile,jobs))
    game=rows(BASE/'module');runtime=rows(OUT);baseline=rows(BASE.parent/'fix2/baseline')
    report=dict(elf_sha256=hashlib.sha256((BASE/'module/HMM2.ELF').read_bytes()).hexdigest(),
        flags=['-Os','-fstack-usage','-Wstack-usage=2048'],game_lto=True,runtime_translation_units=len(jobs),
        baseline=baseline,game=game,runtime=runtime)
    (ROOT/'Docs/evidence/stage1_fix2_stack_static.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    for label,items in [('игра',game),('runtime',runtime)]:
        print(label,'кадров:',len(items))
        for r in items[:8]:print(r['bytes'],r['kind'],r['function'])
if __name__=='__main__':main()
