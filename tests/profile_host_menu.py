"""Codex: одинаковые настоящие нажатия меню; время x64 не выдаётся за время ESP."""
from pathlib import Path
import json,os,re,subprocess,sys,time
exe,out,inputs=map(Path,sys.argv[1:4]);out.mkdir(exist_ok=True)
env=os.environ.copy()
for k in list(env):
 if k.startswith('HMM2_TEST_') or k.startswith('HMM2_AUDIO_'):env.pop(k)
env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_TEST_UI=str(inputs),HMM2_TEST_RENDER_WORKER='1',HMM2_IO_TRACE='1')
start=time.perf_counter();previous=start;rows=[];keys=[];io=[];current=-1
with (out/'host.log').open('w',encoding='utf-8') as log:
 p=subprocess.Popen([str(exe),str(out),'300'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
 for line in p.stdout:
  now=time.perf_counter();log.write(line)
  m=re.search(r'HOST UI PS2=([0-9a-f]+) at=(\d+)',line)
  if m:current+=1;keys.append(dict(key=m[1],virtual_ms=int(m[2]),wall_ms=(now-start)*1000))
  stats=re.search(r'HOST IO frame=(\d+) requests=(\d+) bytes=(\d+)',line)
  if stats:io.append(dict(frame=int(stats[1]),requests=int(stats[2]),bytes=int(stats[3]),phase=current))
  m=re.search(r'HMM2 frame=(\d+)',line)
  if m:rows.append(dict(frame=int(m[1]),phase=current,wall_ms=(now-start)*1000,interval_ms=(now-previous)*1000));previous=now
 code=p.wait()
assert code==0 and len(rows)==300,(code,len(rows))
result=dict(exe=str(exe),frames=len(rows),elapsed_s=time.perf_counter()-start,keys=keys,io=io,frames_timing=rows,host_measurement=True,board_measurement=False)
(out/'host_timing.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print({k:v for k,v in result.items() if k not in ('keys','frames_timing','io')})
for i,key in enumerate(keys):print(key,'max_interval_ms',max((r['interval_ms'] for r in rows if r['phase']==i),default=0))
