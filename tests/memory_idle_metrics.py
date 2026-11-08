"""Codex: рабочий набор CPU-спрайтов и промахи фона в длительном исходном ожидании боя."""
from pathlib import Path
from collections import Counter
import argparse
import hashlib
import json
import re

parser = argparse.ArgumentParser()
parser.add_argument('folder', type=Path)
args = parser.parse_args()
frame = 0
decodes = []
backgrounds = []
lines = (args.folder/'run.log').read_text(encoding='utf-8').splitlines()
for line in lines:
    match = re.search(r'HMM2 frame=(\d+)', line)
    if match:
        frame = int(match[1])
    match = re.fullmatch(r'DECODE group=(\d+) frame=(\d+) bytes=(\d+) roi=([\d,]+)', line)
    if match:
        group, index, size = map(int, match.groups()[:3])
        decodes.append(dict(frame=frame+1, group=group, index=index, bytes=size, roi=match[4]))
    if line.startswith('BG '):
        backgrounds.append(dict(frame=frame+1, miss=line.endswith('hit=0'), text=line))
counts = Counter((r['group'], r['index'], r['bytes']) for r in decodes)
small = {key: count for key, count in counts.items() if key[2] <= 128*1024}
result = dict(frames=frame, complete='SCENARIO COMPLETE' in lines,
              decode_calls=len(decodes), last_decode_frame=max((r['frame'] for r in decodes), default=0),
              unique_small_sprites=len(small), small_bytes=sum(k[2] for k in small),
              small_page_bytes=sum((k[2]+255)//256*256 for k in small),
              repeated_small=[dict(group=k[0], index=k[1], bytes=k[2], calls=v) for k, v in small.items() if v>1],
              bg_calls=len(backgrounds), bg_misses=sum(r['miss'] for r in backgrounds),
              after_300=[r for r in decodes if r['frame']>300], decodes=decodes)
(args.folder/'analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('decodes','after_300')}, indent=2))
print('after_300=', len(result['after_300']))
# Codex: сброс посреди прогретой сцены не должен отзывать постоянные области.
forced = [i for i, line in enumerate(lines) if line.startswith('FORCED TRIM time=')]
if forced:
    assert len(forced)==1
    tail = lines[forced[0]:]
    validation = dict(complete=result['complete'], frames=frame, forced_trim_time=int(tail[0].split('=')[1]),
                      decode_after_trim=sum(line.startswith('DECODE ') for line in tail),
                      bg_misses_after_trim=sum(line.startswith('BG ') and line.endswith('hit=0') for line in tail),
                      arena_at_trim=re.search(r'arena=(\d+\+\d+)', '\n'.join(tail))[1],
                      log_sha256=hashlib.sha256((args.folder/'run.log').read_bytes()).hexdigest())
    (args.folder/'trim_validation.json').write_text(json.dumps(validation, indent=2), encoding='utf-8')
    assert validation['complete'] and validation['arena_at_trim']=='131072+262144'
    assert not validation['decode_after_trim'] and not validation['bg_misses_after_trim']
