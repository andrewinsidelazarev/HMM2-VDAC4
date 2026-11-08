"""Codex: сверяем готовые пакеты, первичные доказательства и явно не закрытые пункты этапа 0б."""
from pathlib import Path
import ast,hashlib,json,re,zipfile,importlib.util
from compile_probe import ROOT
from link_probe import FW
E=ROOT/'Docs/evidence'
def load(name):return json.loads((E/name).read_text(encoding='utf-8'))
spec=importlib.util.spec_from_file_location('checker',FW.parent/'Sound/tools/check_module_symbols.py');check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
def main():
    for folder in ['tools','tests']:
        for p in (ROOT/folder).glob('*.py'):ast.parse(p.read_text(encoding='utf-8'),filename=str(p))
    for label in ['HSD','HRT']:
        manifest=json.loads((ROOT/'dist'/label/'SHA256.json').read_text())
        for name,row in manifest.items():
            p=ROOT/'dist'/label/name;assert p.stat().st_size==row['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
            if p.suffix=='.ELF':
                assert not set(check.undefined(str(p),check.readelf()))-check.allowed()
                assert not check.unloaded_sections(str(p),check.readelf()) and not check.relative_outside(str(p),check.readelf())
    q=(E/'runtime_qemu.log').read_text(encoding='utf-8');assert 'QEMU: start=0' in q and 'контрольного объекта=1' in q
    world=load('world_variants.json');assert len(world)==97 and all(r['identical'] and r['sha256_baseline']==r['sha256_stream'] for r in world)
    memory=load('stage0b_memory.json');assert len(memory['maps'])==97
    with zipfile.ZipFile(E/'stage0b_logs.zip') as archive:
        for row in memory['maps']:assert hashlib.sha256(archive.read(row['map']+'/run.log')).hexdigest()==row['log_sha256']
    exact=[r for r in load('ft_prototype/result.json')['scenes'] if r['variant']=='precompose']
    assert len(exact)==4 and all(r['equal']==307200 and r['compact_dl_fits'] and r['cache_fits'] for r in exact)
    sd=load('sd_launcher_test.json');assert len(sd)==3 and all(r['sd_writes']==0 for r in sd)
    assert len(load('stage0b_elf.json')['no_editor_pic']['missing'])==285
    report=(ROOT/'Docs/STAGE0B_REPORT.md').read_text(encoding='utf-8');assert 'Этап 0б не закрыт' in report
    for name in ['STAGE0B_REPORT.md','PLAN.md']:
        p=ROOT/'Docs'/name
        for href in re.findall(r'\]\(([^)]+)\)',p.read_text(encoding='utf-8')):
            if not href.startswith(('http','<')):assert (p.parent/href.split('#')[0]).exists(),href
    result=dict(ok=True,accepted=['HRT загрузка Xtensa','HSD пакет и два раздельных host-стенда','97 побайтовых состояний','4 точных кадра модели FT812'],not_accepted=['Полный HMM2.ELF/main','Полный TLS/newlib/платформа','Измеренная куча всей игры Xtensa','Бесшовность/FPS/скорость платы'],sd_writes=0)
    (E/'stage0b_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
