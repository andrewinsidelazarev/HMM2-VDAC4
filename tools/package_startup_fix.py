"""Codex: пакет исправления запуска выдаётся только после новых проверок при 7 МиБ."""
from pathlib import Path
import hashlib,json,shutil,zipfile,sys,struct
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
RUN=Path('E:/zx/cache/tmp/hmm2_esp/fast_frame/startup_v11e')
OUT=ROOT/'dist/HMM2_fast_frame_v11e_test'
sys.path.insert(0,str(ROOT/'tests'))
from ft_command_fifo import CommandFifo

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    elf=RUN/'module/HMM2.ELF';digest=sha(elf);datahash=sha(BASE/'data/HMM2.DAT')
    negative=json.loads((RUN.parent/'hardware_failure_v11/negative_test.json').read_text(encoding='utf-8'));assert negative['failed_as_expected'] and negative['psram_budget']==7340032
    assert elf.stat().st_size<=1572864
    assert '34 external symbols, all given by the core' in (RUN/'module/check.log').read_text()
    suite=json.loads((RUN/'qemu_suite/results.json').read_text());scenes={'ghostplt':900,'gatehell':900,'load':1000,'battle':400,'town':700,'menu':30}
    assert len(suite)==len(scenes) and all(x['exit']==0 and x['archive_exit']==0 for x in suite)
    proof=[];dl_limits={}
    for scene,count in scenes.items():
        d=json.loads((RUN/f'evidence/stage1_fix5_qemu_{scene}.json').read_text())
        assert d['elf_sha256']==digest and d['data_sha256']==datahash and d['psram_budget']==7340032
        assert d['frames']==count and d['no_visible_overwrites'] and d['stack']['minimum_free_bytes']>=2048
        assert d['stack']['allocated_bytes']==16384
        proof.append({k:d[k] for k in ['label','frames','minimum_free','stack','psram_budget']})
        raw=(RUN/f'qemu_suite/{scene}/menu.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=largest=0
        while at<len(raw):
            addr,n=struct.unpack_from('<II',raw,at);at+=8;chunk=raw[at:at+n*4];at+=n*4;addr&=0x7fffffff
            if addr<0x300000:ram[addr:addr+len(chunk)]=chunk
            elif addr<0x302000:dl[addr-0x300000:addr-0x300000+len(chunk)]=chunk
            elif addr==0x302054:
                for i,(w,) in enumerate(struct.iter_unpack('<I',dl)):
                    if not w:largest=max(largest,i+1);break
                else:raise AssertionError('DL without DISPLAY')
            else:fifo.write(addr,chunk,ram,dl)
        assert largest<=2048;dl_limits[scene]=largest
    checks=[]
    for folder,count in [(RUN/'host',370),(RUN/'hero_dialog',22),(RUN/'buildings',56),(BASE/'joint/fast_startup_v11e',370)]:
        d=json.loads((folder/'fix5_ft_check.json').read_text());assert d['bad']==0 and d['frames']==count
        checks.append(dict(path=str(folder),frames=count,bad=0))
    joint=json.loads((BASE/'joint/fast_startup_v11e/result.json').read_text());spg=BASE/'launcher/_HMM2.SPG'
    assert joint['elf_sha256']==digest and joint['spg_sha256']==sha(spg)
    copro=json.loads((RUN/'copro_result.json').read_text());assert len(copro)==4 and all(d['equal_dl'] and not d['fault'] for d in copro)
    assert not OUT.exists();OUT.mkdir(parents=True)
    for src,name in [(elf,'HMM2.ELF'),(spg,'_HMM2.SPG'),(ROOT/'LICENSE','LICENSE'),(ROOT/'THIRD_PARTY.md','THIRD_PARTY.md')]:shutil.copy2(src,OUT/name)
    shutil.copytree(ROOT/'licenses',OUT/'licenses')
    readme='''# HMM2 ESP — исправление запуска v11e, 29.10.2026

Заменить HMM2.ELF в существующей папке игры. _HMM2.SPG приложен из того же
проверенного комплекта и не менялся. HMM2.DAT прежний; в этот пакет не включён.
Запускать _HMM2.SPG. Пользовательские настройки и сохранения сохраняются.

Исправлены два воспроизведённых отказа памяти: загрузка GHOSTPLT и открытие
окна героя. Перед новой игрой освобождаются восстановимые пиксели меню;
оригинальный алгоритм градиентной тени исполняется на малой копии нужного участка.
Редкие Blit/AlphaBlit с CPU-преобразованиями выполняются тем же кодом полосами
по 16 строк, без полного пиксельного буфера Display.
Алгоритм тени, игровые правила и рендер v11 сохранены.

ПК KINGDTH: 370/370; окно героя: 22/22; здания и альфа: 56/56;
совместный стенд: 370/370. Использован исходный CPU-эталон.
Шесть сцен QEMU прошли с ограничением PSRAM 7 МиБ. Прежняя v11 с тем же
бюджетом воспроизводит std::bad_alloc. Штатная проверка картинки исключает
пиксели аппаратных альфа-теней. Журналы ПК, героя и совместного стенда
исполнены неизменённым кодом сопроцессора BL616, RAM_DL совпадает, fault=0.

Цель скорости Z80 ещё не достигнута. Оценка 38,5 мс относится к прежней
трассе v11. Аппаратного замера v11e пока нет. Эта версия — промежуточная.
Прошивки и ABI платы не меняются. На физической плате агент её не запускал.

Проверить новую игру GHOSTPLT, окно героя, город и бой, сохранение/загрузку.
Отчёт и материалы: E:/zx/HMM2 ESP/Docs/CODEX_STARTUP_FIX_REPORT.md и
E:/zx/cache/tmp/hmm2_esp/fast_frame/startup_v11e. Снимок исходников — source_tree
в том же каталоге; неизменённые third_party находятся в исходном проекте.
'''
    (OUT/'README.md').write_text(readme,encoding='utf-8')
    files={p.relative_to(OUT).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(OUT.rglob('*')) if p.is_file()}
    manifest=dict(version='fast_frame_v11e_test',files=files,unchanged_dat_sha256=datahash,qemu=proof,max_dl_words=dl_limits,native=checks,copro_verified=True,negative_test=negative)
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'SHA256SUMS.txt').write_text(''.join(v['sha256']+'  '+k+'\n' for k,v in files.items()),encoding='ascii')
    archive=ROOT/'dist/HMM2_fast_frame_v11e_test_update.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in OUT.rglob('*'):
            if p.is_file():z.write(p,p.relative_to(OUT))
    with zipfile.ZipFile(archive) as z:assert z.testzip() is None
    snapshot=RUN/'source_tree';snapshot.mkdir()
    for name in ['main','prototype','tools','tests','launcher','bench']:
        for p in (ROOT/name).rglob('*'):
            if p.is_file() and p.suffix in ['.c','.cpp','.h','.inc','.asm','.ld','.py','.ps1','.cfg','.txt']:
                dest=snapshot/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
    print(json.dumps(dict(folder=str(OUT),zip=str(archive),elf_sha256=digest,dl=dl_limits),ensure_ascii=False,indent=2))
if __name__=='__main__':main()