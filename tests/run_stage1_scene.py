"""Codex: проверка исходных игровых сцен; все артефакты остаются в tmp на E:."""
from pathlib import Path
import os,subprocess,sys
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def main():
    packed='--packed' in sys.argv
    x64='--x64' in sys.argv
    suffix='_allmaps' if '--allmaps' in sys.argv else '_save' if '--save' in sys.argv else ''
    out=BASE/(('runs/scene64' if x64 else 'runs/scene_packed' if packed else 'runs/scene')+suffix);out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_TEST_MAP='maps/KINGDTH.MX2')
    (out/'states').mkdir(exist_ok=True);env['HMM2_STATE_OUT']=str(out/'states')
    if '--allmaps' in sys.argv:env['HMM2_TEST_ALLMAPS']='1'
    if '--save' in sys.argv:env['HMM2_TEST_SAVE']='1'
    with (out/'run.log').open('wb') as log:
        r=subprocess.run([str(BASE/('host64' if x64 else 'host32_packed' if packed else 'host32')/'hmm2_host.exe'),str(out),'20000'],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=900)
    t=(out/'run.log').read_text(encoding='utf-8',errors='replace');print(t[-4000:]);print('Код:',r.returncode)
    return r.returncode or (0 if 'SCENARIO COMPLETE' in t else 3)
if __name__=='__main__':raise SystemExit(main())
