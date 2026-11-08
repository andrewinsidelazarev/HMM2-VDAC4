"""Codex: выход настоящего ELF из загрузки без дальнейших команд Z80."""
from pathlib import Path
import subprocess,struct,json,shutil,sys,os
R=Path(__file__).resolve().parents[1];B=Path('E:/zx/cache/tmp/hmm2_esp/stage1/qemu_stage1');O=Path(os.environ.get('HMM2_EXIT_EVIDENCE','E:/zx/cache/tmp/hmm2_esp/slot0_autounload'))
def main():
    O.mkdir(parents=True,exist_ok=True);rows=[];os.environ['HMM2_QEMU_SAVE']='E:/zx/cache/tmp/hmm2_esp/fast_frame/loading_v11f5/qemu_suite/gatehell/run/H2S00000.SAV'
    for label,args,mode in [('map',['GHOSTPLT.MX2','--extended'],3),('save',['load'],4)]:
        subprocess.run([sys.executable,str(R/'tests/prepare_qemu_scene.py'),*args],check=True,cwd=R)
        (B/'exit_test.bin').write_bytes(struct.pack('<I',mode))
        with (O/('qemu_'+label+'_run.log')).open('wb') as log:p=subprocess.run([sys.executable,str(R/'tools/run_qemu_stage1.py'),'--exit-test','--timeout','180'],stdout=log,stderr=subprocess.STDOUT,cwd=R)
        shutil.copy2(B/'qemu.log',O/('qemu_'+label+'.log'))
        text=(B/'qemu.log').read_text(encoding='utf-8');marker='QEMU: M_SEL=0, Tang SLOT0 during load; Z80 stopped'
        assert marker in text and 'LOAD CURSOR begin' in text and text.index('LOAD CURSOR begin')<text.index(marker),text[-1800:]
        assert 'QEMU: exit verified no_status_query=1 core=1 host=Evo task_finished=1' in text and p.returncode==0,text[-1800:]
        rows.append(dict(scenario=label,exit=p.returncode,no_status_query=True,loading_scope_entered=True));print(rows[-1],flush=True)
        (O/'qemu_results.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':main()
