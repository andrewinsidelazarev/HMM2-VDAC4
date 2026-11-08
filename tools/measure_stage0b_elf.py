"""Codex: размеры реальных файлов после strip и отрицательный результат штатного валидатора игры."""
from pathlib import Path
import importlib.util,hashlib,json,shutil,subprocess
from compile_probe import ROOT,TC,OUT as OLD
from link_probe import FW
TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')
OUT=TMP/'elf_metrics';OUT.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('checker',FW.parent/'Sound/tools/check_module_symbols.py');check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
sources={'lto_no_runtime':TMP/'lto/all.elf','full_pic':TMP/'pic_runtime/engine_pic.elf','no_editor_no_runtime':TMP/'editor/no_runtime.elf','no_editor_pic':TMP/'editor/pic_runtime.elf','runtime_proof':TMP/'pic_runtime/HRT.ELF','sd_bench':TMP/'sd_bench/HSD.ELF'}
rows={}
for name,src in sources.items():
    path=OUT/(name+'.elf');shutil.copy2(src,path)
    subprocess.run([str(TC/'xtensa-esp32s3-elf-strip.exe'),'--strip-unneeded',*['--remove-section='+s for s in ['.comment','.got.loc','.dynamic','.xt.lit','.xt.prop','.xtensa.info']],str(path)],check=True,capture_output=True)
    sizes=subprocess.check_output([str(TC/'xtensa-esp32s3-elf-size.exe'),'-A',str(path)],text=True)
    sections={p[0]:int(p[1]) for line in sizes.splitlines() if len(p:=line.split())==3 and p[0].startswith('.')}
    imports=check.undefined(str(path),check.readelf());missing=sorted(n for n in imports if n not in check.allowed())
    rows[name]=dict(file_bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),sections=sections,imports=len(imports),missing=missing,unloaded_sections=check.unloaded_sections(str(path),check.readelf()),relative_outside=check.relative_outside(str(path),check.readelf()))
(ROOT/'Docs/evidence/stage0b_elf.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
for name,row in rows.items():print(name,row['file_bytes'],'missing',len(row['missing']),'unloaded',row['unloaded_sections'])
