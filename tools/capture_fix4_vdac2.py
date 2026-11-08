"""Codex: воспроизводимые записи R9 unreal-ng SPI28; исходники VDAC2 только читаются.

Запуск из PowerShell после . tools/env.ps1:
  & $Python tools/capture_fix4_vdac2.py setup
  & $Python tools/capture_fix4_vdac2.py timing_all
  & $Python tools/capture_fix4_vdac2.py speed10
  & $Python tools/capture_fix4_vdac2.py capture --scene menu --frames 240 --click 522 232

Для town/battle ставится та же сцена и координаты, что у VDAC4, затем
capture --scene town|battle --frames 300. Приложить --notes с картой,
героями/армиями, городом, исходной позицией и действием. Запись содержит
реальные показанные DL/RAM_G (EVR), метрики и снимки начала/конца.
"""
from pathlib import Path
import argparse,os,sys,hashlib,json,shutil
ROOT=Path('E:/zx/HMM2')
HERE=ROOT/'Diagnostics/claude/ramg_audit'
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/vdac2')
R9='5ee9ad2db2d81d164b81b479a58a8f7f3f49173c059908a2dd0198888b1b192d'

def reference():
    # Codex: текущий Build может принадлежать следующему исправлению; R9 берём из архива.
    work=OUT/'reference';build=work/'Build';build.mkdir(parents=True,exist_ok=True)
    archive=ROOT/'Diagnostics/codex_glitch_backup9/r9_checked_5ee9ad2d/Build'
    for name in ['hmm2_vdac2.spg','hmm2.sym']:shutil.copy2(archive/name,build/name)
    manifest=json.loads((HERE/'r9_ng/sd_manifest.json').read_text(encoding='utf-8'))
    for item in manifest:
        if not item['name'].upper().endswith('.PAK'):continue
        src=HERE/'r9_runtime/hmm2-sd'/item['name'];dst=build/item['name']
        assert hashlib.file_digest(src.open('rb'),'sha256').hexdigest()==item['sha256'],src
        if not dst.exists():shutil.copy2(src,dst)
        assert hashlib.file_digest(dst.open('rb'),'sha256').hexdigest()==item['sha256'],dst
    return work

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode',choices=['setup','timing_all','speed10','capture'])
    ap.add_argument('--id');ap.add_argument('--scene',choices=['menu','transition','load','scroll','hero','battle','town'])
    ap.add_argument('--frames',type=int,default=300);ap.add_argument('--click',nargs=2,type=int);ap.add_argument('--notes',default='')
    a=ap.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    ref=reference()
    digest=hashlib.sha256((ref/'Build/hmm2_vdac2.spg').read_bytes()).hexdigest()
    if digest!=R9:raise SystemExit('Требуется именно R9: '+R9+'; найден '+digest)
    os.environ.update(HMM2_RAMG_OUT=str(OUT),HMM2_RAMG_NG_ROOT=str(OUT/'runtime'),HMM2_RAMG_BACKUP_ROOT=str(OUT/'backup'))
    if a.id:os.environ['HMM2_RAMG_NG_ID']=a.id
    sys.path.insert(0,str(ROOT/'Source/Tools'))
    sys.path.insert(0,str(HERE));import ng_audit as n
    n.ROOT=ref
    if a.mode=='setup':
        current=n.api('GET','/emulator',global_=True)
        if current.get('count',0):raise SystemExit('Сначала завершить существующий экземпляр unreal-ng: '+str(current))
        d=n.api('POST','/emulator/create',{'model':'TSL-VDAC2','ram_power_on':'zero'},True)
        (OUT/'id.txt').write_text(d.get('id',d.get('emulator_id')))
        n.api('PUT','/feature/sound',{'enabled':False})
        n.api('POST','/start',{});n.boot();n.frames(700);n.capture('menu_ready');return
    if a.mode in ('timing_all','speed10'):
        path=HERE/('r9_timing.py' if a.mode=='timing_all' else 'r9_speed10.py')
        source=path.read_text(encoding='utf-8')
        # Codex: прежний сценарий, но все его результаты и SD эмулятора — только tmp задачи.
        source=source.replace("str(p/'r9_ng')",repr(str(OUT))).replace("str(p/'r9_runtime')",repr(str(OUT/'runtime')))
        source=source.replace("str(HERE/'r9_ng')",repr(str(OUT))).replace("str(HERE/'r9_runtime')",repr(str(OUT/'runtime')))
        # Codex: оригинальный сценарий трактует argv как номера карт, а не режим обёртки.
        sys.argv=[str(path)]
        exec(compile(source,str(path),'exec'),{'__file__':str(path),'__name__':'__main__'});return
    if not a.scene:ap.error('capture требует --scene')
    if a.scene in ('battle','town','hero','load','scroll') and not a.notes:ap.error('Нужны --notes для одинаковой фикстуры двух портов')
    tag=a.scene;path=OUT/(tag+'.evr')
    if path.exists():raise SystemExit('Запись уже существует, сначала сохраните предыдущий результат: '+str(path))
    if a.click:
        # Codex: движение курсора закончено до начала отсчёта нажатия.
        n.ensure_core();symbols=n.symbols();x,y=a.click
        for key,value in [('Input.Mouse.PositionX',x),('Input.Mouse.PositionY',y),('CursorPixelX',x),('CursorPixelY',y),('CursorLastMouseX',x),('CursorLastMouseY',y)]:
            n.api('POST','/memory/write',{'address':symbols[key],'data':list(value.to_bytes(2,'little'))})
        n.frames(3)
    n.capture(tag+'_before');before=n.api('GET','/state/tsconf')
    capture=n.api('POST','/vdac2/capture/start',{'path':str(path)})
    try:
        if a.click:n.api('POST','/mouse/click',{'button':'left','frames':5})
        n.frames(a.frames)
    finally:n.api('POST','/vdac2/capture/stop',{})
    n.capture(tag+'_after')
    n.save(tag+'_scenario.json',dict(scene=tag,frames=a.frames,click=a.click,notes=a.notes,spg_sha256=digest,
        video_period_ms=16.926,click_clock=capture['start_clock'] if a.click else None,tsconf_before=before,tsconf_after=n.api('GET','/state/tsconf'),
        status='CAPTURED_REQUIRES_MATCHED_VDAC4_REPLAY',evr_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    print(path)
if __name__=='__main__':main()
