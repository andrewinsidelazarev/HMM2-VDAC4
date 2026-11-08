"""Codex: исходные сцены и анимации с артефактами только в рабочей копии."""
from pathlib import Path
import argparse,json,os,shutil,subprocess

def main():
    p=argparse.ArgumentParser();p.add_argument('name');p.add_argument('--scene',default='alteris.mp2')
    p.add_argument('--animation',action='store_true');p.add_argument('--scroll-palette',action='store_true')
    p.add_argument('--focus-hero',action='store_true')
    p.add_argument('--hero',type=int,default=0)
    p.add_argument('--routes',action='store_true')
    p.add_argument('--whole-tiles',action='store_true')
    p.add_argument('--play',action='store_true');p.add_argument('--frames',type=int,default=20000)
    p.add_argument('--exe',type=Path);p.add_argument('--decode',action='store_true');a=p.parse_args()
    base=Path(os.environ['HMM2_STAGE1_BASE']).resolve();out=base.parent/'runs'/a.name
    (out/'run').mkdir(parents=True,exist_ok=True)
    shutil.copy2('E:/zx/cache/tmp/hmm2_esp/board63_crash/save/fheroes2.cfg',out/'run/fheroes2.cfg')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_TEST_MAP='maps/'+a.scene,HMM2_HOST_DAT=str(base/'data/HMM2.DAT'),HMM2_IO_TRACE='1')
    if a.animation:env['HMM2_TEST_ANIMATION']='1'
    if a.focus_hero:env['HMM2_TEST_HERO_FOCUS']='1'
    env['HMM2_TEST_HERO_INDEX']=str(a.hero)
    if a.routes:env['HMM2_TEST_ROUTES']='1'
    if a.whole_tiles:env['HMM2_TEST_WHOLE_TILES']='1'
    if a.play:env['HMM2_TEST_PLAY']='1'
    if a.scroll_palette:env['HMM2_TEST_SCROLL_PALETTE']='1'
    if a.decode:env['HMM2_DECODE_TRACE']='1'
    exe=a.exe.resolve() if a.exe else base/'host64_packed/hmm2_host.exe'
    with (out/'run.log').open('wb') as log:
        r=subprocess.run([str(exe),str(out),str(a.frames)],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
    text=(out/'run.log').read_text(encoding='utf8',errors='replace')
    # Codex: у настоящего бесконечного цикла карты штатное завершение теста — заданный предел кадров.
    reached=a.play and ('достигнут предел кадров='+str(a.frames)) in text
    result=dict(exit=r.returncode,complete='SCENARIO COMPLETE' in text or reached)
    (out/'result.json').write_text(json.dumps(result));print(a.name,result,flush=True)
    return int(r.returncode!=0 or not result['complete'])

if __name__=='__main__':raise SystemExit(main())
