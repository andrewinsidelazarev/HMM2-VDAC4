"""Compile production helpers and exercise faults with synthetic in-memory data."""
from pathlib import Path
import os,sys,subprocess,json
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT,FW,SDK
out=Path(os.environ.get('HMM2_STAGE1_BASE',str(ROOT/'stage')))/'build138_units'
out.mkdir(parents=True,exist_ok=True)
inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ('ucrt','shared','um')],ROOT/'main',FW/'modules/sdk/include',SDK,SDK/'SDL2']
libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ('ucrt','um')]]
rows=[]
for name,lang in [('save_transactions_test','c'),('swap_stop_test','cpp'),('ring_merge','cpp'),('monster_recipe_pair','cpp'),('map_queue_sort','cpp')]:
    cmd=[VC/'bin/Hostx64/x64/cl.exe','/nologo','/O2','/MT','/utf-8','/DHMM2_HOST','/D_CRT_SECURE_NO_WARNINGS',*['/I'+str(p) for p in inc],'/TC' if lang=='c' else '/EHsc',*(['/std:c++17'] if lang=='cpp' else []),ROOT/'tests'/(name+'.'+lang),'/Fo'+str(out/(name+'.obj')),'/Fe'+str(out/(name+'.exe')),'/link',*['/LIBPATH:'+str(p) for p in libs]]
    (out/(name+'_command.json')).write_text(json.dumps(list(map(str,cmd)),indent=2))
    r=subprocess.run(list(map(str,cmd)),cwd=out,capture_output=True);(out/(name+'_build.log')).write_bytes(r.stdout+r.stderr)
    assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
    args=[str(out/(name+'.exe'))]
    if lang=='c':args.append(str(out/'protocol.txt'))
    r=subprocess.run(args,cwd=out,capture_output=True,timeout=60)
    text=(r.stdout+r.stderr).decode(errors='replace');(out/(name+'.log')).write_text(text,encoding='utf-8')
    print(text,flush=True);assert not r.returncode,r.returncode
    rows.append(dict(name=name,exit=r.returncode,result=text))
gpu=(ROOT/'main/hmm_gpu.cpp').read_text(encoding='utf-8')
assert 'Loader DLSWAP read' not in gpu and 'Clip DLSWAP read' not in gpu
(out/'result.json').write_text(json.dumps(dict(pass_all=True,tests=rows),indent=2))
