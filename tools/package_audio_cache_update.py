"""Codex: парное обновление ELF/SPG после проверок аудио и всех 4 МиБ ОЗУ Ево."""
from pathlib import Path
import datetime,hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio')
STAGE=BASE.parent/'stage1'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def brief(d):return {k:v for k,v in d.items() if k not in ('rows','frame_hashes','pages')}
def main():
    n=56;module=BASE/f'module{n}/HMM2.ELF';spg=STAGE/'launcher/_HMM2.SPG';digest=sha(module)
    assert module.stat().st_size<=1572864 and sha(BASE/f'module{n}_repro/HMM2.ELF')==digest
    spg_repro=read(BASE/'launcher56_repro.json');assert spg_repro['payload_equal'] and spg_repro['sha256']==sha(spg)
    results=read(BASE/f'qemu_suite{n}/results.json')
    assert len(results)==6 and all(r['exit']==r['archive_exit']==0 for r in results)
    qemu=[]
    for label,frames in [('menu',30),('ghostplt',900),('gatehell',900),('town',700),('battle',400),('load',1000),('audio56_gatehell',400)]:
        j=read(BASE/f'qemu_evidence{n}/stage1_fix5_qemu_{label}.json')
        assert j['elf_sha256']==digest and j['frames']==frames and j['psram_budget']==7340032 and j['max_dl_words']<=2048
        folder=BASE/('qemu56b_sd_gatehell' if label=='audio56_gatehell' else f'qemu_suite{n}/{label}')
        dl=read(folder/'dl_validation/dl_state.json')
        assert dl['repeated_scanout_equal'] and dl['max_words']==j['max_dl_words']
        assert j['stack']['minimum_free_bytes']>=2048 and j['audio']['minimum_free_stack']>=1024
        qemu.append(brief(j))
    assert qemu[-1]['audio']['pcm_gaps']==qemu[-1]['audio']['max_gap_samples']==0
    native=read(BASE/f'native{n}/fix5_ft_check.json');newgame=read(BASE/f'newgame{n}/comparison.json')
    joint=read(STAGE/f'joint/integrated{n}/result.json');picture=read(STAGE/f'joint/integrated{n}/fix5_ft_check.json')
    assert native['frames']==picture['frames']==370 and newgame['frames']==439
    assert native['bad']==picture['bad']==newgame['bad']==0 and joint['exit']==0
    assert joint['elf_sha256']==digest and joint['spg_sha256']==sha(spg)
    audio=read(BASE/f'audio{n}.check.json');ring=read(BASE/f'audio{n}_ring.check.json');sfx=read(BASE/f'audio{n}_cache.cachecheck.json')
    assert audio['tracks']==42 and audio['sfx_max_error']<1e-6 and audio['music_resume']
    assert ring['pcm_equal'] and ring['wraps']>=10 and sfx['exact_pcm']
    z80=read(BASE/f'music_cache{n}_z80/result.json')
    assert all(r['all_4mib_accounted'] for r in z80['layout']) and z80['working_page_restored'] and z80['invalid_hints_rejected']
    exits=read(STAGE/'exit_checks/qemu_results.json');assert len(exits)==2 and all(r['exit']==0 for r in exits)
    assert sha(STAGE/'qemu_stage1/main/HMM2.ELF')==digest
    snapshot=BASE/f'source_tree{n}';snapshot.mkdir(exist_ok=True)
    names=set(read(BASE/'source_tree49/sources.json'))|{'launcher/music_cache.asm','tests/test_stage1_music_cache_z80.py','tests/check_audio_ring.py','tests/check_stage1_audio_cache.py','tools/deploy_audio_cache56.ps1','tools/package_audio_cache_update.py','Docs/CODEX_AUDIO_CACHE_REPORT.md'}
    hashes={}
    for name in sorted(names):
        src=ROOT/name;dst=snapshot/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);hashes[name]=sha(dst)
    (snapshot/'sources.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2),encoding='utf-8')
    out=ROOT/'dist/HMM2_build56_audio_cache_test_update';out.mkdir(exist_ok=True)
    for src in (module,spg,ROOT/'LICENSE'):shutil.copy2(src,out/src.name)
    manifest=dict(build=n,test_update=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in (module,spg)},
        requires=dict(dat_bytes=291675244,dat_sha256='c29a843864833e78a39211afcc3391c87a1c1b60d8d0e26e5792de80b5620140'),
        qemu=qemu,spg_reproduction=spg_repro,stress_comparison=read(BASE/'audio56_stress_comparison.json'),kingdth=brief(native),newgame=brief(newgame),joint=brief(picture),joint_elf_sha256=joint['elf_sha256'],joint_spg_sha256=joint['spg_sha256'],
        audio=audio,music_ring=ring,sfx_cache=sfx,z80_cache=z80,exit_checks=exits,source_snapshot=str(snapshot),source_manifest_sha256=sha(snapshot/'sources.json'),
        psram_compressed_ring_bytes=131072,evo_ram_bytes=4194304,graphics_cache_bytes=2097152,
        render_baseline_build=49,new_render_timing_measured=False,z80_speed_target_achieved=False,physical_game_started=False,board_flashed=False,delivered=False)
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    text=f"""HMM2 — промежуточная тестовая сборка 56 (аудиокэш и загрузчик)

Заменить вместе HMM2.ELF и _HMM2.SPG. DAT остаётся прежним: 291675244 байта. Настройки, сохранения и OGG.ELF не заменять.

ZX Evolution: все 4 МиБ (256 банков) учтены. 2 МиБ остаются графике; музыка получает все свободные банки после кода, FAT и рабочих буферов. Размер зависит от разметки SD: тест SPC8 дал 105 банков, 1720320 байт. Z80 независимо предвыбирает по одному блоку 16 КиБ между запросами ESP.

ESP: сжатое кольцо OGG 128 КиБ, PCM 32 КиБ и SFX 16 КиБ размещены в уже ненужных упакованных инициализаторах ELF. Декодер читает PSRAM; SD обслуживает игровой поток. Подача PCM core0/priority5 выше worker рендера core0/priority3; логика на core1.

Проверки этого ELF: KINGDTH 370/370, новая игра 439/439, совместный Z80/FAT32/SD/ESP стенд 370/370 без расхождений. 42 OGG, восстановление позиции, отмена чтения, точные SFX 32/8 каналов; минутный PCM после 10 оборотов кольца побайтно равен прямому источнику. Шесть сцен QEMU плюс стресс SD (400 кадров, задержка 4+ceil(bytes/256) мс): 0 разрывов PCM в стресс-прогоне. Выход Q и SLOT0 проверены. В обычных сценах QEMU остаются другие паузы PCM, включая длинную паузу рядом с пробным выделением памяти в бою, наблюдавшуюся и в49; см. счётчики каждой сцены в manifest.json. Отсутствие хрипа на физической плате ещё не подтверждено.

ELF {module.stat().st_size} байт; SHA256 {digest}. Максимум DL {max(j['max_dl_words'] for j in qemu)} слов; минимум PSRAM {min(j['minimum_free'] for j in qemu)} байт.

Это изменение аудиопотока и чтения SD. Новое время рендера не измерялось; достижение 20 мс не заявляется. В стресс-модели последняя загрузка заняла 17,265 с против 17,680 с у49; переносить эти секунды на плату нельзя. Исходная логика, кадры анимации, палитра, PAL8 VDAC4, RAM_G 3 МиБ сохранены. Прошивка ESP, ABI, Tang и BL616 не менялись.
"""
    (out/'README.txt').write_text(text,encoding='utf-8')
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name in ['HMM2.ELF','_HMM2.SPG','LICENSE','README.txt','manifest.json']:z.write(out/name,name)
    print(json.dumps(dict(folder=str(out),archive=str(archive),elf_sha256=digest),ensure_ascii=False))
if __name__=='__main__':main()
