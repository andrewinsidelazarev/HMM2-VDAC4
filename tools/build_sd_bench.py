"""Codex: собираем ELF и SPG теста чтения в разрешённом временном каталоге."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
from compile_probe import TC,ROOT
from link_probe import FW
from prepare_sd_launcher import REF
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/sd_bench')

def run(cmd,name):
    p=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True)
    (OUT/name).write_bytes(p.stdout+p.stderr)
    if p.returncode or b'Unrecognized option' in p.stdout+p.stderr:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
    return p

def main():
    (OUT/'build').mkdir(parents=True,exist_ok=True)
    sdk=FW/'modules/sdk/include'
    run([TC/'xtensa-esp32s3-elf-gcc.exe','-std=gnu11','-Os','-fPIC','-mlongcalls','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-I'+str(sdk),'-c',ROOT/'bench/sd_bench.c','-o','sd_bench.o'],'compile.log')
    run([TC/'xtensa-esp32s3-elf-gcc.exe','-nostdlib','-nostartfiles','-shared','-fPIC','-Wl,-e,main','-Wl,--gc-sections','-Wl,-Bsymbolic','sd_bench.o','-o','HSD.ELF'],'link.log')
    run([TC/'xtensa-esp32s3-elf-strip.exe','--strip-unneeded',*['--remove-section='+n for n in ['.comment','.got.loc','.dynamic','.xt.lit','.xt.prop','.xtensa.info']],'HSD.ELF'],'strip.log')
    run([sys.executable,FW.parent/'Sound/tools/check_module_symbols.py',OUT/'HSD.ELF'],'check.log')
    for dma in [True,False]:
        name='hsd_dma' if dma else 'hsd_plain'
        run([Path('E:/zx/z80/zuma/sjasmplus.exe'),'--nologo',*([] if dma else ['-DHSD_NO_DMA']),
             '--inc='+str(ROOT/'bench/launcher'),'--inc='+str(REF/'third_party/fat32/include'),
             '--sym=build/'+name+'.sym','--lst=build/'+name+'.lst',ROOT/'bench/launcher/hsd.asm'],name+'.log')
        shutil.copy2(OUT/'build/hsd_z80.bin',OUT/('build/'+name+'.bin'))
        blocks=[(2,OUT/('build/'+name+'.bin')),(14,REF/'third_party/fat32/build/fat32.bin'),(15,REF/'third_party/fat32/build/fat32-work.bin')]
        for page,src in blocks[1:]:shutil.copy2(src,OUT/'build'/src.name)
        ini=OUT/'build'/('hsd_'+str(int(dma))+'.ini')
        ini.write_text('Desc = HMM2 SD bench\nStart = #8000\nStack = #bff0\nResident = #5b00\nPage3 = 32\nClock = 2\nINT = 0\nPager = 0\n'+''.join('Block = #c000, '+str(page)+', build/'+src.name+'\n' for page,src in blocks),encoding='ascii')
        run(['E:/zx/z80/zuma/spgbld.exe','-b',ini,OUT/('_HSD.SPG' if dma else '_HSDPIO.SPG'),'-c','0'],name+'_spg.log')
    manifest={p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in OUT.iterdir() if p.suffix in ['.ELF','.SPG']}
    (ROOT/'Docs/evidence/sd_bench_build.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
