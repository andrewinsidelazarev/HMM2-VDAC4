"""Codex: проверяем действующий код кучи и ввода, не копии алгоритмов."""
from pathlib import Path
import os
import sys,subprocess,json,tempfile
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT,SDK,FW,BASE
def main():
    out=BASE/'platform_tests';out.mkdir(parents=True,exist_ok=True)
    cc=VC/'bin/Hostx64/x86/cl.exe';link=cc.with_name('link.exe')
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/p for p in ['ucrt','shared','um']],ROOT/'main',SDK,SDK/'SDL2',FW/'modules/sdk/include']
    libs=[VC/'lib/x86',*[KIT/'Lib/10.0.26100.0'/p/'x86' for p in ['ucrt','um']]]
    reports={}
    for test,sources in [('heap',['main/hmm_heap.c','tests/stage1_heap.c']),('nodes',['tests/stage1_nodes.cpp']),('input',['main/hmm_io.c','main/hmm_input.c','tests/stage1_input.c']),('env_audio',['tests/env_audio_math.cpp']),('files',['main/hmm_files.c','tests/stage1_files_test.c'])]:
        if '--only' in sys.argv and test!=sys.argv[sys.argv.index('--only')+1]:continue
        objects=[]
        for source in sources:
            obj=out/(Path(source).stem+'.obj');extra=[]
            if source=='main/hmm_heap.c':extra=['/D'+s+'=hmm_'+s for s in ['malloc','free','realloc','calloc']]
            if source=='main/hmm_io.c':extra=['/FI'+str(ROOT/'tests/stage1_atomic.h')]
            if test=='input' and '--input-only' in sys.argv:extra+=['/DHMM2_INPUT_ONLY']
            language=['/TP','/std:c++17','/EHsc'] if source.endswith('.cpp') else ['/TC','/std:c11']
            origin=Path(os.environ['HMM2_INPUT_BASE']) if test=='input' and source.startswith('main/') and os.environ.get('HMM2_INPUT_BASE') else ROOT
            cmd=[str(cc),'/nologo',*language,'/Od','/MT','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DSDL_MAIN_HANDLED',*extra,*['/I'+str(p) for p in inc],'/c',str(origin/source),'/Fo'+str(obj)]
            r=subprocess.run(cmd,capture_output=True,cwd=out);assert not r.returncode,(source,(r.stdout+r.stderr).decode(errors='replace'));objects.append(str(obj))
        cmd=[str(link),'/NOLOGO','/SUBSYSTEM:CONSOLE','/OUT:'+str(out/(test+'.exe')),*objects,*['/LIBPATH:'+str(p) for p in libs]]
        r=subprocess.run(cmd,capture_output=True,cwd=out);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
        # Codex: первая запись и повторный процесс используют один новый носитель;
        # остаток прерванного старого теста не должен подменять начальное состояние.
        run_dir=Path(tempfile.mkdtemp(prefix='files_',dir=out)) if test=='files' else out
        r=subprocess.run([str(out/(test+'.exe'))],capture_output=True,cwd=run_dir,timeout=30);s=(r.stdout+r.stderr).decode(errors='replace');print(s);assert not r.returncode,r.returncode;reports[test]=s.strip()
        if test=='files':
            r=subprocess.run([str(out/'files.exe'),'reopen'],capture_output=True,cwd=run_dir,timeout=30);s=(r.stdout+r.stderr).decode(errors='replace');print(s);assert not r.returncode,r.returncode;reports['files_restart']=s.strip()
    (ROOT/'Docs/evidence/stage1_platform.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
