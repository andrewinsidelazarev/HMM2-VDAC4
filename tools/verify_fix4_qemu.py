"""Codex: повтор прежних сцен QEMU последовательно, без записи на плату."""
from pathlib import Path
import subprocess,sys,json,time,shutil
ROOT=Path(__file__).resolve().parents[1]
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix4/qemu_checks');OUT.mkdir(exist_ok=True)
def run(script,*args):
    name=Path(script).stem+'_'+('_'.join(args) or 'default')
    with (OUT/(name+'.log')).open('wb') as f:r=subprocess.run([sys.executable,script,*args],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    print(name,'exit',r.returncode,flush=True)
    if r.returncode:raise RuntimeError(str(OUT/(name+'.log')))
def main():
    scenes=[('GateHell.mp2',['--extended'],'gatehell',['200','600','900']),
            ('KINGDTH.MX2',['--town'],'kingdth',['300','440','600','700']),
            ('battle',[],'battle',['80','200','400']),
            ('menu',[],'menu',['1','30']),
            ('BELTWAY.MP2',['--week'],'week',['300','1000','1900']),
            ('load',[],'loaded',['300','800','1000']),
            ('KINGDTH.MX2',['--deep-town'],'deep_town',['300','500','630','700','1000','--keys'])]
    completed=[]
    for scene,args,label,shots in scenes:
        run('tests/prepare_qemu_scene.py',scene,*args)
        run('tools/run_qemu_stage1.py')
        run('tests/archive_stage1_qemu.py',label,*shots)
        if label in ('gatehell','week','loaded'):run('tests/check_stage1_target_save.py',label)
        if label=='menu':
            run('tests/check_stage1_qemu.py')
            shutil.copy2(OUT.parents[1]/'stage1/qemu_stage1/compare.json',ROOT/'Docs/evidence/stage1_qemu_menu_compare.json')
        completed.append(label);(OUT/'completed.json').write_text(json.dumps(completed))
    run('tests/check_stage1_stack.py')
if __name__=='__main__':main()
