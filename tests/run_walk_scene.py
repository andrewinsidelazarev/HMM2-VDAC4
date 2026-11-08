"""Codex: короткая воспроизводимая сцена с настройками и задержками платы."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, subprocess, sys

ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ['HMM2_STAGE1_BASE']).resolve()
OUT=BASE/'qemu_stage1'

def main():
    p=argparse.ArgumentParser()
    p.add_argument('name');p.add_argument('--scene',default='alteris.mp2')
    p.add_argument('--frames',type=int,default=1800);p.add_argument('--shift',type=int,default=2)
    p.add_argument('--latency',default='2,500');p.add_argument('--budget',type=int,default=6815744)
    p.add_argument('--keys',default='311b5a');p.add_argument('--times',default='10000,20000,64000')
    p.add_argument('--clicks',default='80000,305,378,1;112000,318,353,1;144000,318,343,1')
    p.add_argument('--click-hold',type=int,default=8)
    p.add_argument('--extended',action='store_true');p.add_argument('--timeout',type=int,default=300)
    p.add_argument('--realtime',action='store_true')
    a=p.parse_args();dest=BASE.parent/'runs'/a.name;dest.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(HMM2_QEMU_SHIFT=str(a.shift),HMM2_QEMU_CFG='E:/zx/cache/tmp/hmm2_esp/board63_crash/save/fheroes2.cfg')
    if a.latency!='0,0':env['HMM2_QEMU_SD_LATENCY']=a.latency
    else:env.pop('HMM2_QEMU_SD_LATENCY',None)
    subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),a.scene,*(['--extended'] if a.extended else [])],env=env,check=True)
    def words(name,values):
        (OUT/name).write_bytes(struct.pack('<%dI'%len(values),*values))
    if not a.extended:
        (OUT/'ui_keys.bin').write_bytes(bytes.fromhex(a.keys));words('ui_times.bin',list(map(int,a.times.split(','))))
    words('ui_test.bin',[a.frames]);words('psram_budget.bin',[a.budget])
    clicks=[int(v) for item in a.clicks.split(';') if item for v in item.split(',')]
    words('ui_clicks.bin',clicks)
    words('ui_click_hold.bin',[a.click_hold])
    (dest/'scenario.json').write_text(json.dumps(vars(a),indent=2))
    # Codex: журнал однозначно связан с проверенным ELF и исходными настройками.
    elf=(OUT/'main/HMM2.ELF').read_bytes()
    (dest/'build.json').write_text(json.dumps(dict(elf_bytes=len(elf),elf_sha256=hashlib.sha256(elf).hexdigest()),indent=2))
    with (dest/'runner.log').open('wb') as log:
        r=subprocess.run([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),*([] if a.realtime else ['--icount']),'--timeout',str(a.timeout)],env=env,stdout=log,stderr=subprocess.STDOUT)
    for name in ['qemu.log','reads.log','menu.ft','command.json','initial.cfg','ui_keys.bin','ui_times.bin','ui_clicks.bin','ui_click_hold.bin','ui_test.bin','sd_latency.bin','psram_budget.bin']:
        if (OUT/name).exists():shutil.copy2(OUT/name,dest/name)
    if (OUT/'run').exists():shutil.copytree(OUT/'run',dest/'run',dirs_exist_ok=True)
    lines=(dest/'qemu.log').read_text(encoding='utf8',errors='replace').splitlines()
    print('\n'.join(s for s in lines if any(t in s for t in ['HMM2 PERF','HMM2 IO','HMM2 GPU','failure','Exception','skipped','click','захват кадров завершён'])))
    return r.returncode

if __name__=='__main__':raise SystemExit(main())
