"""Codex: итоговые короткие регрессии одного ELF, без обращения к плате."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ['HMM2_STAGE1_BASE']).resolve();STAND=BASE/'qemu_stage1'
SCENES={'menu':['menu'],'gatehell':['GateHell.mp2','--extended'],
        'ghostplt':['GHOSTPLT.MX2','--extended'],'town':['KINGDTH.MX2','--town'],
        'battle':['battle'],'load':['load']}

def main():
    p=argparse.ArgumentParser();p.add_argument('scenes',nargs='*',choices=list(SCENES));a=p.parse_args()
    out=BASE.parent/'runs/final_suite';out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.pop('HMM2_QEMU_SD_LATENCY',None)
    env['HMM2_QEMU_CFG']='E:/zx/cache/tmp/hmm2_esp/board63_crash/save/fheroes2.cfg'
    # Codex: отдельный запуск оставшихся сцен использует сохранение уже проверенного GateHell.
    save=out/'gatehell/run/H2S00000.SAV'
    if save.exists():env['HMM2_QEMU_SAVE']=str(save)
    results=json.loads((out/'results.json').read_text()) if (out/'results.json').exists() else []
    failed=False
    for scene in a.scenes or SCENES:
        folder=out/scene
        if folder.exists():
            # Codex: предыдущий прогон сохраняется отдельно; старый SAV не доказывает новую запись.
            i=1
            while (out/(scene+'.previous'+str(i))).exists():i+=1
            target=out/(scene+'.previous'+str(i))
            assert folder.resolve().parent==out.resolve() and target.resolve().parent==out.resolve()
            folder.rename(target)
        folder.mkdir()
        subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),*SCENES[scene]],env=env,check=True)
        (STAND/'ui_clicks.bin').write_bytes(b'')
        (STAND/'psram_budget.bin').write_bytes(struct.pack('<I',7340032))
        if scene=='menu':(STAND/'ui_test.bin').write_bytes(struct.pack('<I',100))
        elf=(STAND/'main/HMM2.ELF').read_bytes()
        (folder/'build.json').write_text(json.dumps(dict(elf_bytes=len(elf),elf_sha256=hashlib.sha256(elf).hexdigest()),indent=2))
        with (folder/'runner.log').open('wb') as log:
            r=subprocess.run([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),'--timeout','300'],env=env,stdout=log,stderr=subprocess.STDOUT)
        for name in ['qemu.log','reads.log','menu.ft','command.json','initial.cfg','ui_keys.bin','ui_times.bin','ui_clicks.bin','ui_test.bin','psram_budget.bin']:
            if (STAND/name).exists():shutil.copy2(STAND/name,folder/name)
        shutil.copytree(STAND/'run',folder/'run',dirs_exist_ok=True)
        subprocess.run([sys.executable,str(ROOT/'tests/summarize_walk.py'),str(folder)],check=True)
        summary=json.loads((folder/'summary.json').read_text())
        result=dict(scene=scene,exit=r.returncode,complete=summary['complete'],minimum_free=summary['minimum_free'],failures=summary['failures'])
        results=[old for old in results if old['scene']!=scene]+[result]
        print(result,flush=True);(out/'results.json').write_text(json.dumps(results,indent=2))
        # Codex: отказ одной сцены записывается; независимые сцены всё равно проверяются.
        if r.returncode or not summary['complete'] or any(summary['failures'][s] for s in ['bad_alloc','аварийное завершение runtime','layer skipped','rejected DL','Exception']):failed=True
        if scene=='gatehell':
            save=folder/'run/H2S00000.SAV'
            if save.exists():env['HMM2_QEMU_SAVE']=str(save)
    return int(failed)

if __name__=='__main__':raise SystemExit(main())
