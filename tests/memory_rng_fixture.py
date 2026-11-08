"""Codex: проверка начального состояния стенда по неизменённому классу игры."""
from pathlib import Path
import hashlib,json,os,subprocess
from memory_unit import ROOT,VC,KIT

out=ROOT.parent/'temp/memory_rng_fixture'
out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy()
env.update(TEMP=str(out),TMP=str(out),PYTHONDONTWRITEBYTECODE='1')
source=ROOT/'tests/memory_rng_fixture_test.cpp'
command=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',
         '/I'+str(VC/'include'),'/I'+str(KIT/'Include/10.0.26100.0/ucrt'),str(source),
         '/Fo'+str(out/'fixture.obj'),'/Fe'+str(out/'fixture.exe'),'/link',
         '/LIBPATH:'+str(VC/'lib/x64'),'/LIBPATH:'+str(KIT/'Lib/10.0.26100.0/ucrt/x64'),
         '/LIBPATH:'+str(KIT/'Lib/10.0.26100.0/um/x64')]
built=subprocess.run(command,cwd=out,env=env,capture_output=True)
(out/'build.log').write_bytes(built.stdout+built.stderr)
built.check_returncode()
result=subprocess.run([str(out/'fixture.exe')],cwd=out,env=env,capture_output=True)
result.check_returncode()
record=dict(passed=True,result=result.stdout.decode().strip(),
            original_rand_h_sha256=hashlib.sha256((ROOT/'third_party/openhmm2/src/engine/rand.h').read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps(record))
