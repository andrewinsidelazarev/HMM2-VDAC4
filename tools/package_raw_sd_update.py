"""Codex: проверенный тестовый комплект raw SD, без копирования большого DAT по SMB."""
from pathlib import Path
import datetime,hashlib,json,re,shutil
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio');STAGE=BASE.parent/'stage1'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def brief(j):return {k:v for k,v in j.items() if k not in ('rows','frame_hashes','pages')}
def main():
 module=BASE/'module59/HMM2.ELF';spg=STAGE/'launcher/_HMM2.SPG';dat=BASE/'raw_final_data/HMM2.DAT'
 digest=sha(module);data_digest=sha(dat)
 assert digest=='b5b4f14b247757fd0688f3efe2c2507a005da767fe8779f94436cb73eea26b27'
 assert module.stat().st_size<=1572864 and sha(BASE/'module59_repro/HMM2.ELF')==digest
 assert sha(spg)=='b9faf4a966ae26cc618ff2ef71a14893723f4fa46f06d4b55e1b95431e6cddf7'
 assert data_digest=='fa26866949ac90bed05acb1b7a411a3e7c65d7affed729ee337856a4b2e4132b'
 assert sha(STAGE/'qemu_stage1/main/HMM2.ELF')==digest
 data=read(BASE/'raw_final_data/check.json');assert data['sha256']==data_digest and data['frames']==20938 and data['unchanged_files']==451
 results=read(BASE/'qemu_suite59_raw/results.json');assert len(results)==6 and all(r['exit']==r['archive_exit']==0 for r in results)
 qemu=[]
 for label,count in [('menu',30),('ghostplt',900),('gatehell',900),('town',700),('battle',400),('load',1000),('sdstress_map',None)]:
  j=read(BASE/f'qemu_evidence59_raw/stage1_fix5_qemu_{label}.json')
  assert j['elf_sha256']==digest and j['data_sha256']==data_digest and j['psram_budget']==7340032
  assert j['frames']==count if count else j['frames']>=700
  assert j['max_dl_words']<=2048 and j['stack']['minimum_free_bytes']>=2048 and j['audio']['minimum_free_stack']>=1024
  folder=BASE/('qemu_stress59_kingdth' if label=='sdstress_map' else 'qemu_suite59_raw/'+label)
  dl=read(folder/'dl_state.json');assert dl['repeated_scanout_equal'] and dl['max_words']==j['max_dl_words']
  qemu.append(brief(j))
 stress=qemu[-1];assert stress['audio']['pcm_gaps']==stress['audio']['max_gap_samples']==0
 log=(BASE/'qemu_stress59_kingdth/qemu.log').read_text(encoding='utf-8');capture,post=log.split('QEMU: захват кадров завершён',1)
 assert 'QEMU: mouse started mode=2' in capture and 'PS2=72' in capture and 'PS2=74' in capture
 assert 'AUDIO underrun count=' not in capture
 stress['audio_during_capture']=dict(pcm_gaps=0,underruns=0)
 stress['visual_check']=dict(map_visible_before_and_after=True,hero_movement_and_scroll_visible=True,frames=[300,450,700],evidence_folder=str(BASE/'qemu_stress59_kingdth'))
 stress['post_capture_note']='ft_write deliberately stops the game producer at capture completion; later underruns are outside the measured interval'
 native=read(BASE/'native59_final/fix5_ft_check.json');newgame=read(BASE/'newgame59_final/comparison.json');menu=read(BASE/'menu59_final/full_rgb_comparison.json');background=read(BASE/'newgame59_final/loading_background_check.json')
 assert native['frames']==370 and newgame['frames']==439 and menu['frames']==300 and native['bad']==newgame['bad']==menu['bad']==0 and background['full_background']
 joint=read(STAGE/'joint/integrated59_final/result.json');picture=read(STAGE/'joint/integrated59_final/fix5_ft_check.json');oldjoint=read(STAGE/'joint/integrated59_compressed_ab/result.json')
 assert picture['frames']==370 and picture['bad']==joint['exit']==0 and joint['elf_sha256']==oldjoint['elf_sha256']==digest and joint['data_sha256']==data_digest
 assert read(STAGE/'joint/integrated59_compressed_ab/fix5_ft_check.json')['bad']==0
 audio=read(BASE/'audio59_raw.check.json');assert audio['tracks']==42 and audio['sfx_max_error']==0 and audio['music_resume']
 lba=read(BASE/'lba59_final_z80/result.json');assert lba['file_bytes']==dat.stat().st_size and all(r['fat_rewalks']==r['sd_reads_for_lookup']==0 for r in lba['checks'])
 exits=read(BASE/'qemu_exit59_raw/module.json');assert exits['elf_sha256']==digest and len(exits['tests'])==2 and all(r['exit']==0 for r in exits['tests'])
 snapshot=BASE/'source_tree59';snapshot.mkdir(exist_ok=True)
 names=set(read(BASE/'source_tree56/sources.json'))|{
 'AGENTS.md','tools/deploy_raw_sd59.py','main/hmm_audio.h','main/hmm_render_worker.inc','main/hmm_screen_prefix.cpp','tests/stage1_files.cpp',
 'tools/build_raw_graphics_data.py','tests/check_raw_graphics_data.py','tests/native_ft_frames.py','tests/compare_ft_streams.py',
 'tests/check_loading_background.py','tests/profile_host_menu.py','tests/test_lba_cache_z80.py','tools/package_raw_sd_update.py',
 'tests/prepare_qemu_scene.py','tests/run_fix5_qemu_suite.py','tests/archive_fix5_qemu.py','tests/test_stage1_joint.py',
 'tests/qemu_stage1/main/main.c','tools/qemu_trace.py','Docs/CODEX_SD_LAYOUT_REPORT.md'}
 hashes={}
 for name in sorted(names):
  src=ROOT/name;dst=snapshot/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);hashes[name]=sha(dst)
 (snapshot/'sources.json').write_text(json.dumps(hashes,ensure_ascii=False,indent=2),encoding='utf-8')
 out=ROOT/'dist/HMM2_build59_raw_sd_test';out.mkdir(exist_ok=True)
 for src in [module,spg,dat,ROOT/'LICENSE']:
  dst=out/src.name
  if not dst.exists() or sha(dst)!=sha(src):shutil.copy2(src,dst)
  assert sha(dst)==sha(src)
 manifest=dict(build=59,test_build=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
  files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in [module,spg,dat]},
  data_validation=data,qemu=qemu,native=brief(native),newgame=brief(newgame),menu_rgb=brief(menu),loading_background=background,
  joint=brief(picture),joint_lba_index=joint['lba_index'],joint_io_model_ab=dict(compressed_s=oldjoint['elapsed_s'],raw_s=joint['elapsed_s'],native_engine_omits_esp_decode_cost=True),
  menu_io=read(BASE/'menu59_io_comparison.json'),lba=lba,audio=audio,audio_dataset='raw59_data: same 451 non-graphics file payloads independently verified in final DAT',exit_checks=exits,
  source_snapshot=str(snapshot),source_manifest_sha256=sha(snapshot/'sources.json'),
  elf_spare_bytes=1572864-module.stat().st_size,render_full_20ms_achieved=False,physical_game_started=False,board_flashed=False,delivered=False)
 delivery=out/'card_delivery.json'
 if delivery.exists():
  prior=read(delivery)
  if prior.get('delivered') and prior.get('files')==manifest['files']:manifest.update(delivered=True,delivery_receipt='card_delivery.json')
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
 text="""HMM2 — тестовая сборка 59: raw SD и кэш адресов LBA

Комплект: HMM2.ELF, HMM2.DAT, _HMM2.SPG. Для испытания несжатой графики заменить все три вместе. SPG идентичен проверенной сборке 56. CFG, IDX, SAV и OGG.ELF не заменять. Перед записью сохранить прежние игровые файлы на E:, после записи сверить SHA256 из manifest.json.

DAT — 397928824 байта (~379,5 МиБ). Исходные индексы и маски без сжатия, кадры выровнены по 512 байтам. Первые сектора 97 карт собраны подряд для быстрого списка. Кэш адресов LBA работает в ОЗУ Ево; повторных обходов FAT при позиционировании нет. Все 4 МиБ Ево учтены. PAL8, исходные цвета, анимации, музыка и SFX сохранены.

Исправлены обрезанный фон Map loading и повторная распаковка больших фонов меню. Звук обслуживается во время долгих CPU-операций, ожидания рендера и между блоками SD по 8 КиБ. Логика — core1; PCM — core0/priority5; рендер — core0/priority3.

ПК: KINGDTH 370/370, новая игра 439/439, полное RGB меню 300/300, фон — 17088 пикселей без ошибок. Совместный Z80/SD — 370/370. Шесть сцен QEMU при 7 МиБ и отдельный стресс с задержками E4/E6 и прокруткой; точные счётчики в manifest.json. Выход Q/SLOT0 и воспроизводимость ELF проверены. Физическая проверка звука ещё нужна.

Список карт читает в 8,87 раза меньше байтов. Весь запуск ускоренным не объявляется: больший DAT увеличил однократное построение таблицы LBA в модели с 9,889 до 13,414 с. В полном совместном сценарии — 48,063 → 55,054 с без учёта затрат ESP на распаковку. Нужен аппаратный замер полного баланса. Цель полной перерисовки за 20 мс ещё не достигнута.

Большой DAT по SMB не передавать; комплект подготовлен на E: для кардридера. Отчёт: Docs/CODEX_SD_LAYOUT_REPORT.md. Плата агентом не запускалась и не прошивалась.
"""
 (out/'README.txt').write_text(text,encoding='utf-8')
 shutil.copy2(ROOT/'Docs/CODEX_SD_LAYOUT_REPORT.md',out/'REPORT.md')
 for frame in [300,450,700]:shutil.copy2(BASE/f'qemu_stress59_kingdth/visual_map_{frame}.png',out/f'verification_map_{frame}.png')
 print(json.dumps(dict(folder=str(out),files=manifest['files'],source_files=len(hashes)),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
