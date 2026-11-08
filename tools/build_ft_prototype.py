"""Codex: один генератор DL компилируется и для Xtensa, и для независимого воспроизведения на ПК."""
from pathlib import Path
import subprocess,sys
from compile_probe import TC,ROOT
sys.path.insert(0,str(ROOT/'tests'))
from build_profile import VC,KIT
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/ft')
def main():
    OUT.mkdir(exist_ok=True,parents=True)
    src=ROOT/'prototype/ft_sprite.c'
    subprocess.run([str(TC/'xtensa-esp32s3-elf-gcc.exe'),'-std=gnu11','-Os','-fPIC','-mlongcalls','-c',str(src),'-o',str(OUT/'ft_sprite.o')],check=True)
    includes=[VC/'include',KIT/'Include/10.0.26100.0/ucrt',KIT/'Include/10.0.26100.0/shared',KIT/'Include/10.0.26100.0/um']
    subprocess.run([str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/utf-8','/DHFT_HOST',*['/I'+str(p) for p in includes],'/c',str(src),'/Fo'+str(OUT/'ft_sprite.obj')],check=True)
    libs=[VC/'lib/x64',KIT/'Lib/10.0.26100.0/ucrt/x64',KIT/'Lib/10.0.26100.0/um/x64']
    subprocess.run([str(VC/'bin/Hostx64/x64/link.exe'),'/NOLOGO','/DLL','/OUT:'+str(OUT/'ft_sprite.dll'),*['/LIBPATH:'+str(p) for p in libs],str(OUT/'ft_sprite.obj')],check=True,cwd=OUT)
if __name__=='__main__':main()
