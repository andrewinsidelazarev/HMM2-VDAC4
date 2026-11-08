"""Codex: модуль runtime с конструкторами и коррекцией pcrel FDE после штатного загрузчика."""
from pathlib import Path
import subprocess,struct,sys,json
from compile_probe import TC,ROOT
from link_probe import FW
from build_pic_runtime import OUT,BASE,SRC

def run(args,name):
    r=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True)
    (OUT/name).write_bytes(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    return r.stdout.decode(errors='replace')
def main():
    flags=['-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-fvisibility=hidden']
    run([TC/'xtensa-esp32s3-elf-g++.exe',*flags,'-std=c++17','-c',ROOT/'tests/runtime_min.cpp','-o',BASE/'runtime_probe/min.o'],'runtime_compile.log')
    run([TC/'xtensa-esp32s3-elf-gcc.exe',*flags,'-I'+str(FW/'modules/sdk/include'),'-c',ROOT/'bench/runtime_bootstrap.c','-o','bootstrap.o'],'bootstrap_compile.log')
    run([TC/'xtensa-esp32s3-elf-gcc.exe',*flags,'-c',SRC.parent/'libgcc/config/xtensa/lib2funcs.S','-o','windows.o'],'windows_compile.log')
    game=ROOT/'third_party/openhmm2/src'
    includes=[ROOT/'tools/probe_include',game/'engine',*sorted(p for p in (game/'fheroes2').iterdir() if p.is_dir())]
    run([TC/'xtensa-esp32s3-elf-g++.exe',*flags,'-std=c++17',*['-I'+str(p) for p in includes],'-c',ROOT/'tests/abi_metrics.cpp','-o','abi_metrics.o'],'abi_compile.log')
    run([TC/'xtensa-esp32s3-elf-g++.exe','-nostdlib','-nostartfiles','-shared',*flags,'-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,--exclude-libs,ALL','-Wl,-e,main','-T',ROOT/'bench/runtime.ld','bootstrap.o',BASE/'runtime_probe/min.o','windows.o','abi_metrics.o','-Wl,--start-group','libstdcpp_pic.a','libunwind_pic.a','-Wl,--end-group','-o','HRT.raw.ELF'],'bootstrap_link.log')
    symbols=run([TC/'xtensa-esp32s3-elf-nm.exe','HRT.raw.ELF'],'bootstrap_nm.txt')
    syms={line.split()[-1]:int(line.split()[0],16) for line in symbols.splitlines() if len(line.split())==3}
    raw=(OUT/'HRT.raw.ELF').read_bytes();marker=struct.pack('<I',0x4d324548)
    assert raw.count(marker)==1
    raw=raw.replace(marker,struct.pack('<i',syms['anchor']-syms['__hmm_eh_begin']))
    (OUT/'HRT.ELF').write_bytes(raw)
    run([TC/'xtensa-esp32s3-elf-strip.exe','--strip-unneeded',*['--remove-section='+n for n in ['.comment','.got.loc','.dynamic','.xt.lit','.xt.prop','.xtensa.info']],'HRT.ELF'],'bootstrap_strip.log')
    result=run([sys.executable,FW.parent/'Sound/tools/check_module_symbols.py',OUT/'HRT.ELF'],'bootstrap_check.log')
    print(result,flush=True)
    print(run([TC/'xtensa-esp32s3-elf-size.exe','-A','HRT.ELF'],'bootstrap_size.txt'))
if __name__=='__main__':main()
