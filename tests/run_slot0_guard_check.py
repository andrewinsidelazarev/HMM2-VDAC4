"""Codex: проверка тех же заголовков сторожа, которые входят в патч прошивки."""
from pathlib import Path
import subprocess,sys
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'tools'))
from build_stage1 import VC,KIT
O=Path('E:/zx/cache/tmp/hmm2_esp/slot0_autounload')
def main():
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']]]
    libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
    args=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(R/'tests/test_slot0_guard.cpp'),'/Fo'+str(O/'guard.obj'),'/Fe'+str(O/'guard.exe'),'/link',*['/LIBPATH:'+str(p) for p in libs]]
    p=subprocess.run(args,cwd=O,capture_output=True);(O/'guard_build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,(p.stdout+p.stderr).decode(errors='replace')
    p=subprocess.run([str(O/'guard.exe')],cwd=O,capture_output=True);(O/'guard_result.txt').write_bytes(p.stdout+p.stderr);print(p.stdout.decode());assert p.returncode==0
if __name__=='__main__':main()
