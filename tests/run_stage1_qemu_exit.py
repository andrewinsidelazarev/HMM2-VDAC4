"""Codex: закрытие видео и C++ runtime в настоящем Xtensa ELF, Q и переход SLOT0."""
from pathlib import Path
import json,os,shutil,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))

def main():
    results=[];work=BASE/'qemu_stage1'
    for label,mode in [('q',1),('reset',2)]:
        out=BASE/'exit_checks'/('qemu_'+label)
        if out.exists():
            i=1
            while out.with_name(out.name+'.old'+str(i)).exists():i+=1
            out.rename(out.with_name(out.name+'.old'+str(i)))
        out.mkdir(parents=True)
        subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),'menu'],check=True)
        (work/'exit_test.bin').write_bytes(struct.pack('<I',mode))
        with (out/'run.log').open('wb') as log:
            result=subprocess.run([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),'--exit-test','--timeout','180'],stdout=log,stderr=subprocess.STDOUT)
        shutil.copy2(work/'qemu.log',out/'qemu.log')
        results.append(dict(scenario=label,exit=result.returncode));print(results[-1],flush=True)
        (BASE/'exit_checks/qemu_results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        if result.returncode:return result.returncode
    return 0

if __name__=='__main__':raise SystemExit(main())
