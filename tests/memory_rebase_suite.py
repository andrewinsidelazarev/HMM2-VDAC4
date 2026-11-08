"""Codex: архивируем четыре одинаковых сценария отдельно для исходного и нового ELF."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('output',type=Path)
    ap.add_argument('--cases',nargs='+',choices=['loaddlg','battle','gatehell','long'],default=['loaddlg','battle','gatehell','long'])
    args=ap.parse_args()
    stage=Path(os.environ['HMM2_STAGE1_BASE'])
    args.output.mkdir(parents=True,exist_ok=True)
    results=[]
    for name in args.cases:
        elf=(stage/'module/HMM2.ELF').read_bytes()
        assert elf==(stage/'qemu_stage1/main/HMM2.ELF').read_bytes(),'Rebuild QEMU for current ELF'
        scenario=ROOT/'tests'/('memory_long_board.json' if name=='long' else f'memory_rebase_{name}.json')
        folder=args.output/name
        with (args.output/f'{name}.driver.log').open('wb') as log:
            run=subprocess.run([sys.executable,str(ROOT/'tests/memory_session.py'),str(scenario),str(folder)],stdout=log,stderr=subprocess.STDOUT)
        row=dict(scene=name,returncode=run.returncode,elf_sha256=hashlib.sha256(elf).hexdigest())
        if (folder/'qemu.log').exists():
            dat=json.loads(scenario.read_text(encoding='utf-8')).get('dat',str(stage/'data/HMM2.DAT'))
            with (folder/'metrics.log').open('wb') as log:
                measured=subprocess.run([sys.executable,str(ROOT/'tests/memory_metrics.py'),str(folder),'--dat',dat],stdout=log,stderr=subprocess.STDOUT)
            row['metrics_returncode']=measured.returncode
        results.append(row)
        (args.output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print(json.dumps(row),flush=True)
    return int(any(r['returncode'] or r.get('metrics_returncode',1) for r in results))

if __name__=='__main__':raise SystemExit(main())
