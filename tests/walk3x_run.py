"""Codex: локальный запуск проверок; все результаты и временные файлы внутри задания."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent
STAGE = Path(os.environ['HMM2_STAGE1_BASE'])
OUT = Path(os.environ['HMM2_TMP'])

def call(args, log, cwd=ROOT, env=None):
    with Path(log).open('wb') as f:
        p = subprocess.run(list(map(str,args)), cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT)
    print(str(log), p.returncode, flush=True)
    if p.returncode:
        print(Path(log).read_text(encoding='utf-8', errors='replace')[-5000:], flush=True)
        raise SystemExit(p.returncode)

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def build(tag):
    dest=OUT/tag; dest.mkdir(exist_ok=True)
    call([sys.executable,ROOT/'tools/build_stage1.py','--packed','--compile-only'],dest/'compile.log')
    call([sys.executable,ROOT/'tools/link_stage1.py'],dest/'link.log')
    call(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',ROOT/'tools/build_qemu_stage1.ps1','-DirectBuild'],dest/'qemu_build.log')
    elf=STAGE/'module/HMM2.ELF'
    assert elf.read_bytes()==(STAGE/'qemu_stage1/main/HMM2.ELF').read_bytes()
    shutil.copy2(elf,dest/'HMM2.ELF')
    (dest/'build.json').write_text(json.dumps(dict(sha256=digest(elf),size=elf.stat().st_size),indent=2))
    for name in ['hmm_canvas.inc','hmm_gpu.cpp']:
        shutil.copy2(ROOT/'main'/name,dest/name)

def qemu(tag,scene):
    dest=OUT/tag/scene; dest.mkdir(parents=True,exist_ok=True)
    q=STAGE/'qemu_stage1'
    env=os.environ.copy()
    env.update(HMM2_QEMU_SHIFT='2',HMM2_QEMU_SD_LATENCY='2,500',HMM2_QEMU_CFG=os.environ.get('HMM2_WALK_CFG',str(OUT/'board_scroll2.cfg')))
    mode={'walk':'alteris.mp2','edge':'alteris.mp2','book':'alteris.mp2','town':'KINGDTH.MX2','ghost':'GHOSTPLT.MX2'}.get(scene,scene)
    for name in ['psram_budget.bin','ui_clicks.bin','ui_click_hold.bin','ui_live.bin','ui_pointer_direct.bin']:
        (q/name).unlink(missing_ok=True)
    extra=['--town'] if scene=='town' else ['--extended']
    call([sys.executable,ROOT/'tests/prepare_qemu_scene.py',mode,*extra],dest/'prepare.log',env=env)
    def words(name,values): (q/name).write_bytes(struct.pack('<'+'I'*len(values),*values))
    if scene in ['walk','book','edge']:
        keys='311b5a' if scene!='book' else '311b5a5a5a5a5a76765a7676'
        times=[10000,20000,64000] if scene!='book' else [10000,20000,64000,100000,110000,120000,135000,170000,185000,200000,235000,250000]
        clicks=[80000,305,378,1,112000,318,353,1,144000,318,343,1,176000,135,125,1,188000,135,125,1,240000,400,280,1,252000,400,280,1] if scene!='book' else [150000,73,330,1,215000,73,330,1]
        if scene=='edge':
            clicks=clicks[:12]
            for j in range(48):clicks += [168000+j*1000,638 if j%2==0 else 2,240,0]
            clicks += [216000,320,240,0]
        (q/'ui_keys.bin').write_bytes(bytes.fromhex(keys));words('ui_times.bin',times)
        words('ui_clicks.bin',clicks);words('ui_click_hold.bin',[8]);words('ui_test.bin',[2400]);words('ui_pointer_direct.bin',[1])
    elif scene in ['town','battle','ghost']:
        # Codex: с задержкой SD ранний ввод prepare_qemu_scene попадает в заставку,
        # и формально завершённый захват остаётся в меню. Ждём загрузку как на плате;
        # конец задаётся временем, чтобы успеть открыть сам город/бой/героя.
        keys=(q/'ui_keys.bin').read_bytes()
        if scene=='town':
            times=[10000,20000,64000,100000,110000,150000,160000,170000,180000];finish=210000
        elif scene=='battle':
            times=[10000,20000,64000,100000,110000];finish=180000
        else:
            prefix=4 if keys[1]==0x3a else 2
            times=[10000,20000,30000,40000][:prefix]+[64000]+list(range(90000,210000,6000))+[214000,218000]
            finish=260000
        assert len(times)==len(keys)
        words('ui_times.bin',times);words('ui_test.bin',[0,finish])
    words('psram_budget.bin',[8289796 if scene=='ghost' else 6815744])
    elf=Path(os.environ.get('HMM2_WALK_ELF',str(STAGE/'module/HMM2.ELF')));assert elf.read_bytes()==(q/'main/HMM2.ELF').read_bytes()
    (dest/'inputs.json').write_text(json.dumps(dict(elf_sha256=digest(elf),cfg_sha256=digest(Path(env['HMM2_QEMU_CFG'])),shift=2,latency=[2,500],scene=scene,inputs={p.name:p.read_bytes().hex() for p in q.glob('*.bin') if p.name.startswith(('ui_','psram_','sd_latency','rng_'))}),indent=2))
    started=time.monotonic()
    try: call([sys.executable,ROOT/'tools/run_qemu_stage1.py','--icount','--timeout','3600'],dest/'driver.log',env=env)
    finally:
        for name in ['qemu.log','qemu_exit.json','command.json','reads.log','menu.ft']:
            if (q/name).exists():shutil.copy2(q/name,dest/name)
        (dest/'elapsed.json').write_text(json.dumps(dict(seconds=time.monotonic()-started)))

def host(tag,mode,off=False):
    dest=OUT/tag/mode;dest.mkdir(parents=True,exist_ok=True);(dest/'states').mkdir(exist_ok=True)
    env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_HOST_DAT=str(STAGE/'data/HMM2.DAT'),HMM2_TEST_MAP='maps/alteris.mp2',HMM2_TEST_HERO_FOCUS='1',HMM2_MAPDL_STATS='1',HMM2_STATE_OUT=str(dest/'states'),HMM2_BIG_LOG='1',HMM2_RASTER_LOG='1')
    if mode.startswith('anim'):env['HMM2_TEST_ANIMATION']='1'
    if mode=='scroll':env.update(HMM2_TEST_ANIMATION='1',HMM2_TEST_SCROLL_PALETTE='1')
    if mode=='sweep':env['HMM2_TEST_SWEEP']='6'
    if off:env['HMM2_NO_MERGE_CACHE']='1'
    env['HMM2_TEST_MAP']=os.environ.get('HMM2_WALK_MAP','maps/alteris.mp2')
    exe=Path(os.environ.get('HMM2_WALK_HOST_EXE',str(STAGE/'host64_packed/hmm2_host.exe')))
    call([exe,dest,100000 if mode=='sweep' else 400],dest/'run.log',cwd=dest,env=env)
    (dest/'exe.sha256').write_text(digest(exe))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['build','qemu','host']);p.add_argument('tag');p.add_argument('scene',nargs='?',default='walk');p.add_argument('--off',action='store_true');a=p.parse_args()
    if a.action=='build':build(a.tag)
    elif a.action=='qemu':qemu(a.tag,a.scene)
    else:host(a.tag,a.scene,a.off)
