"""Codex: упаковываем только локальные стенды и уведомления; карту и прошивки не трогаем."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1]
TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b');DOOM=Path('E:/zx/DOOM VDAC4')
licenses=ROOT/'licenses';licenses.mkdir(exist_ok=True)
for name in ['VDAC4-MIT.txt','R-Type-MIT.txt']:shutil.copy2(DOOM/'licenses'/name,licenses/name)
shutil.copy2(DOOM/'third_party/fat32/LICENSE',licenses/'FAT32-MIT.txt')
gcc=TMP/'runtime_sources/gcc-15.2.0'
for src,dst in [('COPYING3','GCC-GPL-3.0.txt'),('COPYING.RUNTIME','GCC-Runtime-Exception.txt')]:shutil.copy2(gcc/src,licenses/dst)
for label,source,names in [('HSD',TMP/'sd_bench',['HSD.ELF','_HSD.SPG','_HSDPIO.SPG']),('HRT',TMP/'pic_runtime',['HRT.ELF'])]:
    out=ROOT/'dist'/label;out.mkdir(parents=True,exist_ok=True)
    for name in names:shutil.copy2(source/name,out/name)
    shutil.copy2(ROOT/'bench/README.md',out/'README.md')
    shutil.copy2(ROOT/'LICENSE',out/'LICENSE')
    shutil.copy2(ROOT/'THIRD_PARTY.md',out/'THIRD_PARTY.md')
    shutil.copytree(licenses,out/'licenses',dirs_exist_ok=True)
    manifest={name:dict(bytes=(out/name).stat().st_size,sha256=hashlib.sha256((out/name).read_bytes()).hexdigest()) for name in names}
    (out/'SHA256.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(label,manifest)
