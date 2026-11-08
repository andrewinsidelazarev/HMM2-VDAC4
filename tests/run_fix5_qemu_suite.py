"""Codex: последовательные сцены одного ELF, отдельные журналы и проверка каждого результата."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))/'qemu_stage1'
OUT=Path(os.environ.get('HMM2_QEMU_SUITE','E:/zx/cache/tmp/hmm2_esp/fix5/qemu_suite'))
SCENES={'menu':['menu'],'ghostplt':['GHOSTPLT.MX2','--extended'],'gatehell':['GateHell.mp2','--extended'],'town':['KINGDTH.MX2','--town'],
        'battle':['battle'],'week':['BELTWAY.MP2','--week'],'load':['load'],'deep_town':['KINGDTH.MX2','--deep-town']}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('scenes',nargs='*',choices=list(SCENES));ap.add_argument('--icount',action='store_true');a=ap.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);results=[]
    for label in a.scenes or SCENES:
        tag=label+('_icount' if a.icount else '');folder=OUT/tag
        # Codex: предыдущий результат сохраняем целиком, без смешивания кадров разных ELF.
        if folder.exists():
            i=1
            while (OUT/(tag+'.old'+str(i))).exists():i+=1
            folder.rename(OUT/(tag+'.old'+str(i)))
        folder.mkdir()
        subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),*SCENES[label]],cwd=ROOT,check=True)
        with (folder/'run.log').open('wb') as log:
            p=subprocess.run([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),'--timeout','900',*(['--icount'] if a.icount else [])],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        for name in ['qemu.log','sd.log','menu.ft','command.json','initial.cfg','psram_budget.bin','catalogue.bin','ui_keys.bin','ui_times.bin','ui_test.bin','data_path.bin','sd_latency.bin']:
            if (BASE/name).exists():shutil.copy2(BASE/name,folder/name)
        if (folder/'run').exists():raise RuntimeError('Каталог результата уже существует: '+str(folder))
        shutil.copytree(BASE/'run',folder/'run');shutil.copy2(BASE/'main/HMM2.ELF',folder/'HMM2.ELF')
        result=dict(scene=tag,exit=p.returncode);results.append(result)
        if p.returncode==0:
            with (folder/'archive.log').open('wb') as log:
                p=subprocess.run([sys.executable,str(ROOT/'tests/archive_fix5_qemu.py'),tag,'--keys'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            result['archive_exit']=p.returncode
            if label=='gatehell' and (folder/'run/H2S00000.SAV').exists():
                os.environ['HMM2_QEMU_SAVE']=str(folder/'run/H2S00000.SAV')
        print(result,flush=True)
        (OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        if any(result.get(k,0) for k in ['exit','archive_exit']):return 1
    return 0

if __name__=='__main__':raise SystemExit(main())
