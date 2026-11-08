"""Клод: собирает tests/test_hft_cache.c с prototype/ft_sprite.c компилятором MSVC стенда и запускает. Код возврата — отказы кэша RAM_G."""
from pathlib import Path
import os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
OUT=Path(os.environ.get('HMM2_HFT_OUT','E:/zx/cache/tmp/hmm2_esp/hft_cache_test'));OUT.mkdir(parents=True,exist_ok=True)
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
KIT=Path('C:/Program Files (x86)/Windows Kits/10');SDK='10.0.26100.0'
inc=[VC/'include',KIT/'Include'/SDK/'ucrt',KIT/'Include'/SDK/'um',KIT/'Include'/SDK/'shared',ROOT/'prototype']
libs=[VC/'lib/x64',KIT/'Lib'/SDK/'ucrt/x64',KIT/'Lib'/SDK/'um/x64']
exe=OUT/'test_hft_cache.exe'
# Codex: тот же тест можно исполнить с сохранённой исходной политикой для сравнения.
source=Path(os.environ.get('HMM2_HFT_SOURCE',str(ROOT/'prototype/ft_sprite.c')))
cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/utf-8','/std:c11','/TC',*['/I'+str(p) for p in inc],
     str(ROOT/'tests/test_hft_cache.c'),str(source),'/Fe'+str(exe),'/Fo'+str(OUT)+'\\','/link',*['/LIBPATH:'+str(p) for p in libs]]
p=subprocess.run(cmd,cwd=OUT,capture_output=True)
if p.returncode:sys.stdout.write((p.stdout+p.stderr).decode('cp866','replace'));sys.exit(2)
r=subprocess.run([str(exe)],capture_output=True);sys.stdout.write(r.stdout.decode('utf-8','replace'));sys.exit(r.returncode)
