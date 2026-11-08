"""Codex: отдельный процесс на карту даёт холодный кэш и независимый пик кучи."""
from pathlib import Path
import argparse
import concurrent.futures
import json
import os
import subprocess
import time
import hashlib

ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--limit',type=int);ap.add_argument('--jobs',type=int,default=3);ap.add_argument('--map');ap.add_argument('--timeout',type=int,default=180);ap.add_argument('--extra',action='store_true');ap.add_argument('--resolution',type=int,default=640,choices=[640,1024]);a=ap.parse_args()
    maps=sorted(p for p in Path('E:/zx/HMM2/Assets/Original/MAPS').iterdir() if p.suffix.lower() in ('.mp2','.mx2'))
    if a.map:maps=[p for p in maps if p.name.lower()==a.map.lower()]
    if a.limit:maps=maps[:a.limit]
    def one(p):
        label=f'runs_extra_{a.resolution}' if a.extra else 'runs'
        out=TMP/label/p.stem;out.mkdir(parents=True,exist_ok=True)
        env=os.environ.copy()
        for key,part in [('APPDATA','appdata'),('LOCALAPPDATA','localappdata'),('USERPROFILE','profile'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data')]:
            path=out/part;path.mkdir(exist_ok=True);env[key]=str(path)
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        if a.extra:env['HMM2_VISUAL_EXTRA']='1'
        env['HMM2_RESOLUTION']=str(a.resolution)
        start=time.monotonic()
        try:
            with (out/'run.log').open('wb') as log:
                proc=subprocess.run([str(TMP/'host32/profile.exe'),'--vdac4-profile',str(p)],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout)
            code=proc.returncode
        except subprocess.TimeoutExpired:code='timeout'
        result={'map':p.name,'code':code,'seconds':round(time.monotonic()-start,3),'log':str(out/'run.log'),'exe_sha256':hashlib.sha256((TMP/'host32/profile.exe').read_bytes()).hexdigest()}
        print(result,flush=True)
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:results=list(pool.map(one,maps))
    report=f'profile_extra_{a.resolution}.json' if a.extra else 'profile_runs.json'
    (ROOT/'Docs/evidence'/report).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    return int(any(r['code']!=0 for r in results))
if __name__=='__main__':raise SystemExit(main())
