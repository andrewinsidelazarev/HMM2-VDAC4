"""Codex: регрессии отмены и определения SLOT0 без платы, сети и внешних временных файлов."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import BASE,VC,KIT,SDK,FW
OUT=BASE/'exit_checks'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],ROOT/'main',SDK,SDK/'SDL2',FW/'modules/sdk/include']
    sources=[ROOT/'main'/n for n in ['hmm_io.c','hmm_video.c','hmm_session.c']]+[ROOT/'tests/stage1_exit_checks.cpp']
    objects=[]
    def run(command,name):
        result=subprocess.run(list(map(str,command)),cwd=OUT,capture_output=True)
        (OUT/name).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
        return result.stdout.decode(errors='replace')
    for src in sources:
        obj=OUT/(src.name+'.obj');objects.append(obj)
        flags=['/TC','/std:c11','/FI'+str(ROOT/'tests/stage1_atomic.h')] if src.suffix=='.c' else ['/TP','/std:c++17','/EHsc']
        run([VC/'bin/Hostx64/x64/cl.exe','/nologo','/O2','/MT','/utf-8','/D_CRT_SECURE_NO_WARNINGS',*flags,*['/I'+str(p) for p in inc],'/c',src,'/Fo'+str(obj)],src.name+'.log')
    libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
    exe=OUT/'exit_checks.exe'
    run([VC/'bin/Hostx64/x64/link.exe','/NOLOGO','/OUT:'+str(exe),*objects,*['/LIBPATH:'+str(p) for p in libs]],'link.log')
    print(run([exe],'result.txt'),end='')

if __name__=='__main__':main()
