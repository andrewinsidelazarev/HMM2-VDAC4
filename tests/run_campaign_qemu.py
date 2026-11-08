"""Codex: расписание QEMU как в board.sh; все входы, кадры и сохранения архивируются в рабочей папке."""
from pathlib import Path
import argparse, os, subprocess, struct, shutil, json, hashlib
root=Path(__file__).resolve().parents[1]
stage=Path(os.environ.get('HMM2_STAGE1_BASE',str(root.parent/'stage'))).resolve()
base=stage.parent
out=stage/'qemu_stage1'
parser=argparse.ArgumentParser()
parser.add_argument('name')
parser.add_argument('--mode',default='ui')
parser.add_argument('--dat',type=Path,default=stage/'data/HMM2.DAT')
parser.add_argument('--keys')
parser.add_argument('--times')
parser.add_argument('--clicks',default=os.environ.get('CLICKS',''))
parser.add_argument('--frames',type=int,default=300)
parser.add_argument('--end',type=int,default=0)
parser.add_argument('--timeout',type=int,default=1800)
parser.add_argument('--extended',action='store_true')
parser.add_argument('--key-delay',type=int,default=0)
parser.add_argument('--psram',type=int,default=6815744)
args=parser.parse_args()
env=os.environ.copy()
env.update(HMM2_STAGE1_BASE=str(stage),HMM2_QEMU_DAT=str(args.dat.resolve()),HMM2_QEMU_SD_LATENCY='2,500',HMM2_QEMU_SHIFT='2',
    HMM2_QEMU_EXE=env.get('HMM2_QEMU_EXE','E:/zx/cache/tmp/hmm2_splash/qemu_tool/qemu/bin/qemu-system-xtensa.exe'))
subprocess.run([os.sys.executable,str(root/'tests/prepare_qemu_scene.py'),args.mode,*(['--extended'] if args.extended else [])],env=env,check=True)
if args.keys is not None:
    keys=bytes.fromhex(args.keys)
    times=[int(v) for v in args.times.split(',')] if args.times else []
    assert len(keys)==len(times)
    (out/'ui_keys.bin').write_bytes(keys)
    (out/'ui_times.bin').write_bytes(struct.pack('<%dI'%len(times),*times))
elif args.key_delay and (out/'ui_times.bin').exists():
    times=[v[0]+args.key_delay for v in struct.iter_unpack('<I',(out/'ui_times.bin').read_bytes())]
    (out/'ui_times.bin').write_bytes(struct.pack('<%dI'%len(times),*times))
(out/'ui_test.bin').write_bytes(struct.pack('<II',args.frames,args.end))
(out/'psram_budget.bin').write_bytes(struct.pack('<I',args.psram))
(out/'ui_click_hold.bin').write_bytes(struct.pack('<I',8))
clicks=[int(v) for row in args.clicks.split(';') if row for v in row.split(',')]
assert len(clicks)%4==0 and len(clicks)<=256
(out/'ui_clicks.bin').write_bytes(struct.pack('<%dI'%len(clicks),*clicks))
dest=base/'runs'/args.name
# Codex: повторный прогон не наследует сохранения и кадры прежней попытки.
if dest.exists():
    serial=1
    while dest.with_name(dest.name+f'.previous{serial}').exists():serial+=1
    dest.rename(dest.with_name(dest.name+f'.previous{serial}'))
dest.mkdir(parents=True,exist_ok=True)
with (dest/'run.log').open('wb') as log:
    result=subprocess.run([os.sys.executable,str(root/'tools/run_qemu_stage1.py'),'--icount','--timeout',str(args.timeout)],env=env,stdout=log,stderr=subprocess.STDOUT)
for name in ('qemu.log','menu.ft','reads.log','sd.log','command.json','ui_keys.bin','ui_times.bin','ui_test.bin','ui_clicks.bin','ui_click_hold.bin','psram_budget.bin','catalogue.bin','data_path.bin','initial.cfg','sd_latency.bin'):
    if (out/name).exists():shutil.copy2(out/name,dest/name)
shutil.copytree(out/'run',dest/'save',dirs_exist_ok=True)
with args.dat.open('rb') as f:dat_sha=hashlib.file_digest(f,'sha256').hexdigest()
with (out/'main/HMM2.ELF').open('rb') as f:elf_sha=hashlib.file_digest(f,'sha256').hexdigest()
scene=dict(arguments={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},returncode=result.returncode,dat_sha256=dat_sha,elf_sha256=elf_sha,psram=args.psram)
if env.get('HMM2_QEMU_SAVE'):
    source=Path(env['HMM2_QEMU_SAVE'])
    scene['source_save']=dict(path=str(source),bytes=source.stat().st_size,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),name=env.get('HMM2_QEMU_SAVE_NAME','quicksave.savh'))
(dest/'scene.json').write_text(json.dumps(scene,indent=2),encoding='utf-8')
print(args.name,'returncode',result.returncode)
print((out/'qemu.log').read_text(encoding='utf-8',errors='replace')[-1600:])
raise SystemExit(result.returncode)
