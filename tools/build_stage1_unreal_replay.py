"""Codex: собственный стенд HMM2; готовые библиотеки Unreal-ng копируются только для чтения."""
from pathlib import Path
import sys,subprocess,shutil,json,hashlib
from build_stage1 import VC,KIT,ROOT
U=Path('E:/zx/unreal-ng/src/scratch/wt-vdac4');OUT=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/unreal_replay')
def main():
 OUT.mkdir(exist_ok=True);libs=[];evidence={}
 for name in ['core.lib','eve-emu.lib','vdac4-firmware.lib']:
  source=U/'scratch/build/lib'/name;target=OUT/name;shutil.copy2(source,target);libs.append(target);evidence[str(source)]=hashlib.sha256(source.read_bytes()).hexdigest()
 inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],U/'core/src']
 obj=OUT/'replay.obj';exe=OUT/'hmm2_unreal_replay.exe'
 cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MD','/DNDEBUG','/D_ITERATOR_DEBUG_LEVEL=0','/std:c++20','/EHsc','/utf-8',*['/I'+str(p) for p in inc],'/c',str(ROOT/'tests/stage1_unreal_replay.cpp'),'/Fo'+str(obj)]
 p=subprocess.run(cmd,cwd=OUT,capture_output=True);(OUT/'compile.log').write_bytes(p.stdout+p.stderr)
 if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-5000:])
 paths=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
 cmd=[str(VC/'bin/Hostx64/x64/link.exe'),'/NOLOGO','/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),str(obj),*map(str,libs),*['/LIBPATH:'+str(p) for p in paths],'user32.lib','advapi32.lib','shell32.lib','ole32.lib']
 p=subprocess.run(cmd,cwd=OUT,capture_output=True);(OUT/'link.log').write_bytes(p.stdout+p.stderr)
 if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-6000:])
 (OUT/'build.json').write_text(json.dumps(dict(libraries=evidence,executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),graphics_replay_only=True),indent=2),encoding='utf-8');print(exe)
if __name__=='__main__':main()
