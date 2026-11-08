"""Codex: проверяем завершение, память и блоки по 100 кадров без изменения журналов."""
from pathlib import Path
import json,re,sys

for arg in sys.argv[1:]:
    folder=Path(arg);log=(folder/'qemu.log').read_text(encoding='utf8',errors='replace')
    rows={};lines=log.splitlines()
    for i,line in enumerate(lines):
        # Codex: hmm_log режет длинную строку GPU на части по 120 символов.
        if line.startswith('HMM2 GPU:') and len(line)==120 and i+1<len(lines):line+=lines[i+1]
        if not line.startswith(('HMM2 PERF ','HMM2 IO ','HMM2 GPU:')):continue
        pairs={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
        frame=pairs.pop('frame',0)
        rows.setdefault(frame,{})['gpu' if line.startswith('HMM2 GPU') else 'io' if line.startswith('HMM2 IO') else 'perf']=pairs
    memory=re.findall(r'кадр=(\d+).*?PSRAM свободно=(\d+) минимум=(\d+)',log)
    result=dict(complete='захват кадров завершён' in log,frames=int(memory[-1][0]) if memory else 0,
                minimum_free=min(int(v[2]) for v in memory) if memory else None,
                failures={s:log.count(s) for s in ['bad_alloc','аварийное завершение runtime','layer skipped','rejected DL','Exception','QEMU: OOM']},
                blocks=rows)
    (folder/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(folder,{k:v for k,v in result.items() if k!='blocks'})
