"""Codex: пакет исправления запуска выдаётся только после новых проверок при 7 МиБ."""
from pathlib import Path
import hashlib,json,shutil,zipfile,sys,struct
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
RUN=Path('E:/zx/cache/tmp/hmm2_esp/fast_frame/loading_v11f5')
OUT=ROOT/'dist/HMM2_fast_frame_v11f_test'
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
    for folder,count in [(RUN/'host',370),(RUN/'cursor_full',13),(RUN/'cursor_cached',13),(BASE/'joint/loading_v11f5',370)]:
        d=json.loads((folder/'fix5_ft_check.json').read_text());assert d['bad']==0 and d['frames']==count
        checks.append(dict(path=str(folder),frames=count,bad=0))
    joint=json.loads((BASE/'joint/loading_v11f5/result.json').read_text());spg=BASE/'launcher/_HMM2.SPG'
    assert joint['elf_sha256']==digest and joint['spg_sha256']==sha(spg)
    copro=json.loads((RUN/'copro_result.json').read_text());assert len(copro)==3 and all(d['equal_dl'] and not d['fault'] for d in copro)
    assert not OUT.exists();OUT.mkdir(parents=True)
    for src,name in [(elf,'HMM2.ELF'),(spg,'_HMM2.SPG'),(ROOT/'LICENSE','LICENSE'),(ROOT/'THIRD_PARTY.md','THIRD_PARTY.md')]:shutil.copy2(src,OUT/name)
    shutil.copytree(ROOT/'licenses',OUT/'licenses')
    cursor=json.loads((RUN/'cursor_cached/cursor_trace_check.json').read_text());assert cursor['background_unchanged'] and cursor['no_texture_uploads']
    mouse=json.loads((RUN/'evidence/stage1_fix5_qemu_loading_mouse.json').read_text());assert mouse['elf_sha256']==digest and mouse['frames']==450 and mouse['no_visible_overwrites'] and mouse['psram_budget']==7340032
    phases=json.loads((RUN/'qemu_mouse/cursor_result.json').read_text());assert len(phases)==3 and all(d['moves']>0 for d in phases)
    readme='''# HMM2 ESP — живой курсор при загрузке v11f, 29.10.2026

Заменить HMM2.ELF в существующей папке игры. Запускать _HMM2.SPG.
SPG и HMM2.DAT не менялись. DAT в обновление не включён.
Настройки и сохранения сохраняются.

При загрузке новой карты и сохранения, подготовке интерфейса и первого
игрового экрана показываются оригинальные песочные часы. Мышь продолжает
двигать курсор; повторно передаются только его координаты, затем CMD_APPEND.
Изображение курсора и аппаратная тень остаются последним слоем поверх экрана.
После загрузки восстанавливаются прежний вид и видимость курсора.
Обслуживание выполняется потоком игры: отдельной задачи и стека не добавлено.
Точки обслуживания есть в ожидании SD, проходах мира, построении регионов,
создании тайлов, инициализации героев и загрузке текстур первого экрана.

Проверки: ПК KINGDTH 370/370; совместный стенд 370/370; оригинальные часы
и перенос готового DL — по 13/13 кадров, включая края и возврат указателя.
При переносе фон неизменен, текстуры не передаются, на координаты нужно 8 байт.
Исходный CPU-рендер используется как эталон; штатная сверка исключает пиксели
аппаратных альфа-теней. В QEMU с непрерывной мышью пройдено 450 кадров,
часы двигались во всех трёх фазах загрузки. Шесть обычных сцен прошли при
7 МиБ PSRAM. Трассы исполнены реальным кодом сопроцессора BL616: fault=0.

Обновление курсора кооперативное. Проверка срока — каждые 16 мс в точках
обслуживания; это не гарантия периода 16 мс. В QEMU наибольший промежуток
между вызовами при загрузке карты составил 104 мс, на первом экране — 32 мс.
Это результаты модели, не замеры на плате.

Сохранены исправления памяти v11e: освобождение восстановимых пикселей перед
новой игрой и исходные CPU-операции над небольшими участками/полосами.
Правила игры, порядок слоёв и выбор кадров исходного OpenHMM2 сохранены.
Цель скорости Z80 ещё не достигнута. Прежняя оценка 38,5 мс относится к v11.
Прошивки, ABI, Tang и BL616 не менялись. Агент на плате игру не запускал.

Материалы: E:/zx/cache/tmp/hmm2_esp/fast_frame/loading_v11f5.
Отчёт: E:/zx/HMM2 ESP/Docs/CODEX_FAST_FRAME_REPORT.md.
'''
    (OUT/'README.md').write_text(readme,encoding='utf-8')
    files={p.relative_to(OUT).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(OUT.rglob('*')) if p.is_file()}
    manifest=dict(version='fast_frame_v11f_test',files=files,unchanged_dat_sha256=datahash,qemu=proof,max_dl_words=dl_limits,native=checks,copro_verified=True,negative_test=negative,loading_cursor=cursor,mouse_qemu={k:mouse[k] for k in ['frames','psram_budget','minimum_free','stack']},loading_phases=phases)
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'SHA256SUMS.txt').write_text(''.join(v['sha256']+'  '+k+'\n' for k,v in files.items()),encoding='ascii')
    archive=ROOT/'dist/HMM2_fast_frame_v11f_test_update.zip'
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