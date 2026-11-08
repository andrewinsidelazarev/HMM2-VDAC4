"""Codex: проверенный тестовый комплект v12 и выдача на указанную карту; настройки/сохранения сохраняются."""
from pathlib import Path
import hashlib,json,re,shutil,os,sys
ROOT=Path(__file__).resolve().parents[1];R=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio');B=Path('E:/zx/cache/tmp/hmm2_esp/stage1');OUT=ROOT/'dist/HMM2_integrated_v12_test'
CARD=Path('G:/games/VDAC4/HMM2 VDAC4')
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def main():
 module=R/'module22/HMM2.ELF';spg=B/'launcher/_HMM2.SPG';dat=B/'data/HMM2.DAT';digest=sha(module)
 assert module.stat().st_size<=1572864 and digest=='2e204d64db8055af9b54418626e3c745549a72dfcd73560e0deb3c7ba277a6c7'
 assert sha(dat)=='c29a843864833e78a39211afcc3391c87a1c1b60d8d0e26e5792de80b5620140'
 assert sha(spg)=='6d63d90df317da3d94afad288913e23eb8a8e3cc9c94949c3542f1a3b50e0eca'
 native=read(R/'native22/fix5_ft_check.json');joint=read(B/'joint/integration_audio_final22/fix5_ft_check.json');launch=read(B/'joint/integration_audio_final22/result.json')
 assert native['frames']==joint['frames']==370 and native['bad']==joint['bad']==0
 assert launch['elf_sha256']==digest and launch['spg_sha256']==sha(spg) and launch['exit']==0 and launch['commands']['72']==1 and launch['commands']['69']==launch['commands']['6B']==2
 pack=read(R/'module22/packing_check.json');assert pack['elf_sha256']==digest and pack['constructors_and_unwind_preserved']
 audio=read(R/'audio_test8.check.json');assert audio['tracks']==42 and audio['sfx_max_error']==0 and audio['music_resume']['exact_match_offsets']
 alog=(R/'audio_test8.log').read_text(encoding='utf-8');assert 'interrupted_resume=ok' in alog and 'cancel_headers=ok cancel_body=ok' in alog
 rtc=read(R/'clock_checks/result.json');rz=read(R/'clock_checks/z80_result.json');assert rtc['calendar_cases']==1020 and len(rz)==3
 rlog=(R/'rtc_save22.log').read_text(encoding='utf-8');assert 'AUTOSAVE RTC epoch=1793282400 before=1793282400 after=1793282400' in rlog and 'SAVE LOAD COMPLETE' in rlog
 qlog=(R/'menu22/qemu.log').read_text(encoding='utf-8');assert 'RTC Evo synchronized' in qlog and 'QEMU: OGG loaded entry=1' in qlog and '30 кадров записаны' in qlog and 'QEMU: PCM frames=' in qlog
 for failure in ['аварийное завершение runtime','Guru Meditation','AUDIO OGG decode error']:assert failure not in qlog
 suite=read(R/'qemu_suite19/results.json');assert len(suite)==6 and all(x['exit']==x['archive_exit']==0 for x in suite)
 baseline=[]
 for name in ['ghostplt','gatehell','load','battle','town','menu']:
  row=read(R/f'evidence19/stage1_fix5_qemu_{name}.json');assert row['psram_budget']==7340032 and row['no_visible_overwrites'];baseline.append({k:v for k,v in row.items() if k!='frame_hashes'})
 data_check=read(R/'audio_data_check.json');assert data_check['tracks']==42 and data_check['sfx_aliases']==309 and data_check['original_audio_bytes_exact']
 repack=read(B/'data/ft_repack_check.json');assert repack['checked_cell_references']==41872 and repack['all_cell_bytes_exact']
 recruit=(R/'recruit_user20.log').read_text(encoding='utf-8');assert 'name=Fortress Isle epoch=991' in recruit and 'count=12' in recruit and 'SCENARIO COMPLETE' in recruit
 OUT.mkdir(parents=True,exist_ok=True)
 for source,name in [(module,'HMM2.ELF'),(spg,'_HMM2.SPG'),(dat,'HMM2.DAT')]:shutil.copy2(source,OUT/name)
 for name in ['LICENSE','THIRD_PARTY.md']:
  if (ROOT/name).exists():shutil.copy2(ROOT/name,OUT/name)
 licenses=ROOT/'dist/HMM2_fast_frame_v11_test/licenses'
 if licenses.is_dir():shutil.copytree(licenses,OUT/'licenses',dirs_exist_ok=True)
 readme='''HMM2 integrated v12 — тестовая сборка 29.10.2026

Три файла этого комплекта используются вместе: HMM2.ELF, _HMM2.SPG, HMM2.DAT.
Новый DAT содержит 42 исходных OGG-трека и 309 ссылок на исходные SFX из AGG.
Плеер /WC/ESP/OGG.ELF уже есть на карте; загрузчик находит его от корня SD.
Декодер, прошивки ESP/Tang/BL616 и ABI не менялись. Агент игру на плате не запускал.

Подключены исходный audio_manager, выбор музыки, возврат к треку, боевые и
позиционные SFX, циклический фон. Игровые анимации продолжают обновляться.
Курсор и его тень остаются последними слоями; WAIT обслуживается при загрузке.
Дата и время читаются из RTC Ево перед main, затем идут по монотонным часам.
Новое автосохранение проверено через исходный LoadSAV2FileInfo: дата RTC совпала.
Для RTC поддерживаются BCD/двоичный режим, 12/24 часа и годы 2000..2099;
часы/NVRAM не изменяются. Старые ошибочные даты внутри SAV не переписываются.

Настройки и сохранения сохранены. В установленном FHEROES2.CFG музыка и SFX
имеют громкость 0: включить их через исходное меню настроек игры.

Проверки последнего ELF: настоящий запуск QEMU с OGG/PCM/RTC; ПК и совместный
стенд 370/370 без расхождений вне аппаратных полупрозрачных теней; автосохранение
с датой RTC; 42 трека и контрольный PCM SFX; возврат музыки, остановка чтения OGG;
упаковка на 64 раскладках адресов и все импорты ядра. Базовая интеграция до RTC
и диагностических строк прошла шесть сцен QEMU при 7 МиБ, анимацию/прокрутку/
палитру 122/122, WAIT 13/13, реальные команды сопроцессора и проверки выгрузки.
Точные версии проверок разделены в manifest.json.

На прежнем v11 пользователь сообщил RUNTIME ABORT при повторном входе в жилище.
Скачан его AUTOSAVE (Fortress Isle); на новой графической части 12 повторных входов
в Alamar на ПК прошли. Сам аппаратный abort пока не подтверждён устранённым:
в новую сборку добавлены метки входа/выхода найма, тип исключения и адрес abort.
Включены ранее проверенные исправления памяти, распаковка нужных областей больших
фонов, атласы 16 КиБ и повторное использование геометрии FT812.

Это этап общей интеграции. Цель Z80 10,5 мс / перерисовка <=20 мс ещё не достигнута.
Трасса интеграции выявляет высокую общую стоимость рендера и аудио; это следующий
этап оптимизации. Возврат OGG к позиции пока требует декодирования от начала.
MIDI/SF2 и SMK-видео остаются за пределами этой сборки.

Резервная копия прежнего комплекта и сохранений:
E:/zx/cache/tmp/hmm2_esp/integration_audio/card_before_v12_20261029
Исходники проекта и сборки находятся на E:, не на C:.
'''
 (OUT/'README.txt').write_text(readme,encoding='utf-8')
 files={name:dict(bytes=(OUT/name).stat().st_size,sha256=sha(OUT/name)) for name in ['HMM2.ELF','_HMM2.SPG','HMM2.DAT']}
 manifest=dict(version='integrated_v12_test',candidate='module22; LTO6',files=files,elf_headroom=1572864-module.stat().st_size,
  final=dict(native={k:v for k,v in native.items() if k!='rows'},joint={k:v for k,v in joint.items() if k!='rows'},launch={k:v for k,v in launch.items() if k!='native'},packing=pack,audio=audio,rtc=rtc,z80_rtc=rz,rtc_autosave_epoch=1793282400,qemu_menu_frames=30),
  regression_baseline=dict(candidate='module19, before RTC/diagnostics/LTO6; renderer and audio unchanged',qemu=baseline,animation=read(R/'animation_gpu19/fix5_ft_check.json')['frames'],cursor=read(R/'cursor19/cursor_trace_check.json')),
  data_audio=data_check,atlas_repack=repack,hardware_abort_root_cause_confirmed=False,board_launched_by_agent=False)
 (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
 (OUT/'SHA256SUMS.txt').write_text(''.join(x['sha256']+'  '+name+'\n' for name,x in files.items()),encoding='ascii')
 report='# Интеграция OGG/SFX и RTC, v12 — 29.10.2026\n\n'+readme+'\n\nELF: '+str(module.stat().st_size)+' байт; SHA256 `'+digest+'`.\n\n'
 report+='Полные параметры и доказательства: `dist/HMM2_integrated_v12_test/manifest.json`.\n\n'
 report+='Разделение проверок: последний ELF22 прошёл QEMU menu/RTC/OGG, ПК и совместный стенд; шесть сцен QEMU относятся к базовой интеграции19 перед изменением календаря/диагностики и LTO. Их нельзя выдавать за полный прогон ELF22.\n\n'
 report+='Трасса19: один полный межкадровый интервал, 5610540 команд без стендовой проверки стека; этого мало для устойчивого замера. Новый физический замер не выполнен. Для следующей оптимизации проверить вынесение неизменных коэффициентов mixer/fade из цикла отсчётов и профиль OGG.\n\n'
 report+='По примеру локального DOOM VDAC4 применены отдельная выдача PCM и окно чтения4КиБ. [Deki ESP32FileSystem.cpp](https://github.com/dekiengine/deki-esp32-integration/blob/master/ESP32FileSystem.cpp) рассмотрен как пример разделения неизменных ресурсов и пользовательских файлов. Общий I-кэш ESP32-S3 сохранён в модели согласно [datasheet,4.1.2.3](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf).\n'
 (ROOT/'Docs/CODEX_AUDIO_INTEGRATION_REPORT.md').write_text(report,encoding='utf-8')
 # Codex: перед заменой проверяем полный старый комплект и сохранённые данные карты.
 backup=R/'card_before_v12_20261029';old=read(backup/'manifest.json')
 for name,row in old.items():assert sha(CARD/name)==row['sha256'],('карта изменилась после backup',name)
 assert sha(CARD/'H2AV12.DAT')==files['HMM2.DAT']['sha256']
 for name,stage in [('HMM2.ELF','H2AV12.ELF'),('_HMM2.SPG','H2AV12.SPG')]:
  dest=CARD/stage
  if dest.exists():assert sha(dest)==files[name]['sha256']
  else:shutil.copy2(OUT/name,dest)
  assert sha(dest)==files[name]['sha256']
 for name,stage in [('HMM2.DAT','H2AV12.DAT'),('HMM2.ELF','H2AV12.ELF'),('_HMM2.SPG','H2AV12.SPG')]:os.replace(CARD/stage,CARD/name)
 for name,row in files.items():assert (CARD/name).stat().st_size==row['bytes'] and sha(CARD/name)==row['sha256']
 for name,row in old.items():
  if name not in files:assert sha(CARD/name)==row['sha256']
 delivery=dict(path=str(CARD),files=files,readback_verified=True,settings_and_saves_unchanged=True,backup=str(backup))
 (OUT/'card_delivery.json').write_text(json.dumps(delivery,ensure_ascii=False,indent=2),encoding='utf-8');(R/'card_delivery22.json').write_text(json.dumps(delivery,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps(delivery,ensure_ascii=False,indent=2),flush=True)
if __name__=='__main__':main()
