"""Codex: самостоятельная PIC-сборка настоящего Xtensa unwinder из GCC 15.2."""
from pathlib import Path
import subprocess,tarfile
from build_pic_runtime import BASE,SRC,OUT
from compile_probe import TC

def main():
    gcc=SRC.parent
    with tarfile.open(BASE/'runtime_sources/gcc-15.2.0.tar.xz') as tf:
        for n in ['gcc/tsystem.h','gcc/coretypes.h','gcc/auto-host.h.in','COPYING3','COPYING.RUNTIME']:
            try:
                if not (gcc/n).exists():tf.extract('gcc-15.2.0/'+n,BASE/'runtime_sources',filter='data')
            except KeyError:pass
    gen=OUT/'config';gen.mkdir(exist_ok=True)
    # Codex: заголовки конфигурации сборки; ABI и код обхода окон берём из GCC.
    (gen/'tconfig.h').write_text('#define IN_LIBGCC2 1\n#define USED_FOR_TARGET 1\n#define HAVE_STDLIB_H 1\n#define HAVE_UNISTD_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_SYS_TYPES_H 1\n#define HAVE_MACHINE_ENDIAN_H 1\n',encoding='ascii')
    (gen/'tm.h').write_text('#define TARGET_64BIT 0\n#define BITS_PER_UNIT 8\n#define BITS_PER_WORD 32\n#define UNITS_PER_WORD 4\n',encoding='ascii')
    for n in ['libgcc_tm.h','md-unwind-support.h']:(gen/n).write_text('',encoding='ascii')
    inc=[gen,gcc/'gcc',gcc/'include',gcc/'libgcc',gcc/'libgcc/config/xtensa',TC.parent/'xtensa-esp-elf/include/c++/15.2.0/xtensa-esp-elf/esp32s3/bits']
    files=[gcc/'libgcc/config/xtensa/unwind-dw2-xtensa.c',gcc/'libgcc/unwind-dw2-fde.c']
    objects=[]
    for f in files:
        cmd=[TC/'xtensa-esp32s3-elf-gcc.exe','-std=gnu11','-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-DIN_LIBGCC2',*['-I'+str(i) for i in inc],'-c',f,'-o',OUT/(f.stem+'.o')]
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(OUT/(f.stem+'.log')).write_bytes(r.stdout+r.stderr)
        print(f.name,r.returncode,r.stderr.decode(errors='replace')[-2500:],flush=True)
        if not r.returncode:objects.append(OUT/(f.stem+'.o'))
    if len(objects)==len(files):subprocess.run(list(map(str,[TC/'xtensa-esp32s3-elf-ar.exe','rcs',OUT/'libunwind_pic.a',*objects])),check=True)
if __name__=='__main__':main()
