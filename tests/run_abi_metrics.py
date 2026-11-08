"""Codex: контроль контейнеров на Win32 с теми же параметрами STL, что у профилировщика."""
from pathlib import Path
import subprocess
from build_profile import ROOT,SRC,VC,KIT
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/abi')
OUT.mkdir(parents=True,exist_ok=True)
includes=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ('ucrt','shared','um')],SRC/'engine',*sorted(p for p in (SRC/'fheroes2').iterdir() if p.is_dir())]
libs=[VC/'lib/x86',KIT/'Lib/10.0.26100.0/ucrt/x86',KIT/'Lib/10.0.26100.0/um/x86']
cmd=[str(VC/'bin/Hostx64/x86/cl.exe'),'/nologo','/MTd','/EHsc','/std:c++17','/utf-8','/D_ITERATOR_DEBUG_LEVEL=0','/DHMM2_ABI_PC',*['/I'+str(p) for p in includes],str(ROOT/'tests/abi_metrics.cpp'),'/Fe'+str(OUT/'abi.exe'),'/Fo'+str(OUT/'abi.obj'),'/link',*['/LIBPATH:'+str(p) for p in libs]]
r=subprocess.run(cmd,cwd=OUT,capture_output=True);assert r.returncode==0,(r.stdout+r.stderr).decode(errors='replace')
r=subprocess.run([str(OUT/'abi.exe')],cwd=OUT,capture_output=True);assert r.returncode==0
(ROOT/'Docs/evidence/abi_win32.log').write_bytes(r.stdout);print(r.stdout.decode())
