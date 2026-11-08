"""Codex: локализуем расхождения сравнения миров до признания оптимизации эквивалентной."""
from pathlib import Path
TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')
for name in ['BELTWAY','GateHell','KINGDTH']:
    a=(TMP/'world_baseline'/name/'world.state').read_bytes();b=(TMP/'world_stream'/name/'world.state').read_bytes()
    diff=[i for i,(x,y) in enumerate(zip(a,b)) if x!=y]
    print(name,len(a),len(b),'diff',len(diff),'first',diff[:30],'last',diff[-10:])
    for i in diff[:4]:print(i,a[max(0,i-8):i+16].hex(),b[max(0,i-8):i+16].hex())
    print('seed',a[-4:].hex(),b[-4:].hex())
