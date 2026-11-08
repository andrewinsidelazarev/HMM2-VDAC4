"""Codex: x64-эталон меню с исходным декодером; кадры без сценарной подмены часов."""
from pathlib import Path
import os,subprocess
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def main():
    out=BASE/'runs/menu64';out.mkdir(parents=True,exist_ok=True);env=os.environ.copy()
    for key in list(env):
        if key.startswith('HMM2_TEST_') or key=='HMM2_PACK_OUT':env.pop(key)
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    with (out/'run.log').open('wb') as log:r=subprocess.run([str(BASE/'host64/hmm2_host.exe'),str(out),'60'],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=90)
    assert not r.returncode;print('Эталон x64:',out)
if __name__=='__main__':main()
