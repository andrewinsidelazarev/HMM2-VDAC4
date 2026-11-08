"""Codex: запуск ограничен стендом; автоматический выход не входит в ELF."""
from pathlib import Path
import os,subprocess,sys
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def main():
    packed='--packed' in sys.argv
    out=BASE/('runs/menu_packed' if packed else 'runs/menu');out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['SDL_VIDEODRIVER']='dummy';env['SDL_AUDIODRIVER']='dummy'
    with (out/'run.log').open('wb') as log:
        try:r=subprocess.run([str(BASE/('host32_packed' if packed else 'host32')/'hmm2_host.exe'),str(out),'30'],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        except subprocess.TimeoutExpired:print('Стенд: превышено время. Журнал:',out/'run.log');return 2
    print((out/'run.log').read_text(encoding='utf-8',errors='replace')[-5000:]);print('Код выхода:',r.returncode)
    if r.returncode==0:
        from test_ft_trace import png
        i=(out/'frame_0030.idx').read_bytes();c=(out/'frame_0030.pal').read_bytes()
        png(out/'menu.png',b''.join(c[k*3:k*3+3] for k in i),640,480)
    return r.returncode
if __name__=='__main__':raise SystemExit(main())
