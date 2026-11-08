"""Codex: измеряем исходную загрузку и декодирование каждого предоставленного SMK отдельным процессом Win32."""
from pathlib import Path
import concurrent.futures
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stream',action='store_true');args=ap.parse_args()
    suffix='_stream' if args.stream else ''
    executable=TMP/f'host32/profile_smk{suffix}.exe'
    exe_hash=hashlib.sha256(executable.read_bytes()).hexdigest()
    logs=ROOT/f'Docs/evidence/smk{suffix}_logs';logs.mkdir(exist_ok=True)
    video_dir=next((p for p in [ROOT/'Assets/Original/ANIM',Path('E:/zx/HMM2/Assets/Original/ANIM'),ROOT/'Assets/ANIM'] if p.exists() and list(p.glob('*.SMK'))),None)
    if video_dir is None:raise FileNotFoundError('Не найден каталог предоставленных SMK')
    def one(path):
        out=TMP/f'runs_smk{suffix}'/path.stem;out.mkdir(parents=True,exist_ok=True)
        env=os.environ.copy()
        for key,part in [('APPDATA','appdata'),('LOCALAPPDATA','localappdata'),('USERPROFILE','profile'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data')]:
            p=out/part;p.mkdir(exist_ok=True);env[key]=str(p)
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        start=time.monotonic()
        try:
            with (out/'run.log').open('wb') as log:
                proc=subprocess.run([str(executable),'--vdac4-profile',str(path)],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
            code=proc.returncode
        except subprocess.TimeoutExpired:code='timeout'
        text=(out/'run.log').read_text(encoding='utf-8',errors='replace');phases={};info={}
        for line in text.splitlines():
            f=line.split('\t')
            if f[0]=='PHASE':phases[f[1]]=dict(zip(('live','peak','cache','pc_ms','frames','crt_live'),map(int,f[2:])))
            elif f[0]=='SMK':info=dict(frames=int(f[1]),width=int(f[2]),height=int(f[3]),us_per_frame=float(f[4]),audio_bytes=int(f[5]))
            elif f[0]=='SMK_HASH':info.update(pixels_palette_fnv1a=f[1],decoded_frames=int(f[2]))
        result=dict(name=path.name,code=code,seconds=round(time.monotonic()-start,3),exe_sha256=exe_hash,
                    file_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size,phases=phases,info=info,
                    complete='\nCOMPLETE\t' in text,log_sha256=hashlib.sha256((out/'run.log').read_bytes()).hexdigest())
        shutil.copy2(out/'run.log',logs/(path.stem+'.log'))
        print(path.name,code,max((p['peak'] for p in phases.values()),default=0),flush=True)
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results=list(pool.map(one,sorted(video_dir.glob('*.SMK'))))
    (ROOT/f'Docs/evidence/smk{suffix}_profile.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(any(p['code']!=0 or not p['complete'] for p in results))

if __name__=='__main__':raise SystemExit(main())
