# Codex: выдача ELF разрешена только после проверок того же бинарного файла.
from pathlib import Path
import argparse, datetime, hashlib, json, shutil, zipfile
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio')
def read(p): return json.loads(p.read_text(encoding='utf-8'))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def brief(j): return {k:v for k,v in j.items() if k not in ('rows','frame_hashes','source_sha256')}
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--build',type=int,required=True); ap.add_argument('--name',required=True); ap.add_argument('--intermediate',action='store_true'); a=ap.parse_args(); n=a.build
 assert a.name.startswith('HMM2_') and all(c.isalnum() or c=='_' for c in a.name)
 module=BASE/f'module{n}/HMM2.ELF'; digest=sha(module); assert module.stat().st_size<=1572864
 results=read(BASE/f'qemu_suite{n}/results.json'); assert len(results)==6 and all(r['exit']==r['archive_exit']==0 for r in results)
 qemu=[]
 scenes=[('menu',30),('ghostplt',900),('gatehell',900),('town',700),('battle',400),('load',1000)]
 if not a.intermediate:scenes.append((f'alteris{n}',500))
 for label,frames in scenes:
  j=read(BASE/f'qemu_evidence{n}/stage1_fix5_qemu_{label}.json'); assert j['elf_sha256']==digest and j['psram_budget']==7340032 and j['frames']==frames and j['max_dl_words']<=2048
  dl=read(BASE/f'qemu_suite{n}/{label}/dl_validation/dl_state.json'); assert dl['swaps']==frames and dl['max_words']==j['max_dl_words'] and dl['repeated_scanout_equal']
  qemu.append(brief(j))
 tests={}
 for label,p,frames in [('kingdth',BASE/f'native{n}/fix5_ft_check.json',370),('animation_scroll_palette',BASE/f'animation_gpu{n}/fix5_ft_check.json',122),('new_game',BASE/f'newgame{n}/comparison.json',439),('alteris',BASE/f'alteris_native{n}/fix5_ft_check.json',343),('joint',BASE.parent/f'stage1/joint/integrated{n}/fix5_ft_check.json',370),('waiting_cursor',BASE/f'cursor{n}/cursor_trace_check.json',13)]:
  j=read(p); assert j['frames']==frames and j.get('bad',0)==0; tests[label]=brief(j)
 rgb=read(BASE/f'final{n}_rgb_equivalence.json'); assert sum(r['frames'] for r in rgb)==931 and all(not r['different_rgb_frames'] for r in rgb)
 native_dl=[]
 for label,frames in [(f'native{n}',370),(f'animation_gpu{n}',122),(f'newgame{n}',500),(f'cursor{n}',13)]:
  j=read(BASE/f'{label}/dl_state.json'); assert j['frames']==frames and j['repeated_scanout_equal']; native_dl.append(dict(scene=label,**j))
 copro=read(BASE/f'copro{n}/result.json'); assert len(copro)==3 and all(j['equal_dl'] and j['fault']==0 for j in copro)
 source=BASE/f'source_tree{n}'; hashes=read(source/'sources.json')
 for name,d in hashes.items(): assert sha(source/name)==d,name
 profiles={}
 for label in (['scroll'] if a.intermediate else ['full','scroll']):
  folder=BASE/f'trace{n}_{label}/gatehell'; assert sha(folder/'HMM2.ELF')==digest
  rows=read(folder/'pipeline.json'); assert rows and all(sum(r['functions'].values())==r['instructions_raw'] for r in rows)
  profiles[label]=[{k:v for k,v in row.items() if k!='functions'} for row in rows]
 out=ROOT/'dist'/a.name; out.mkdir(parents=True,exist_ok=True); shutil.copy2(module,out/'HMM2.ELF'); shutil.copy2(ROOT/'LICENSE',out/'LICENSE')
 audio_check=read(BASE/f'audio{n}.check.json') if n>=49 else None
 if audio_check:assert audio_check['tracks']==42 and audio_check['sfx_max_error']<1e-6 and audio_check['music_resume']
 manifest=dict(version='v12b test update',build=n,intermediate=a.intermediate,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),elf=dict(bytes=module.stat().st_size,sha256=digest,budget=1572864),requires=dict(dat_bytes=291675244,dat_sha256='c29a843864833e78a39211afcc3391c87a1c1b60d8d0e26e5792de80b5620140',spg_sha256='6d63d90df317da3d94afad288913e23eb8a8e3cc9c94949c3542f1a3b50e0eca'),tests=tests,audio_pcm_validation=audio_check,full_rgb_comparison=rgb,dl_repeat_checks=native_dl,qemu=qemu,copro=[brief(j) for j in copro],render_profiles=profiles,source_snapshot=str(source),source_manifest_sha256=sha(source/'sources.json'),z80_target_achieved=False,board_flashed=False,physical_game_started=False,delivered=False,notes=['Промежуточная оптимизация; достижение 20 мс не заявляется.','Палитра и кадры анимации обновляются; кэш хранит неизменную геометрию.','Настройки и сохранения не заменяются; в прежнем пользовательском CFG обе громкости были 0.','Unreal-ng использован только для replay графики; HMM2 ELF в нём не запускался.','Цветной PAL8 проверен в отдельном профиле VDAC4 модели Unreal-ng; общий режим FT812 сохранён.'])
 if a.intermediate:
  exits=read(BASE.parent/'stage1/exit_checks/qemu_results.json'); assert len(exits)==2 and all(r['exit']==0 for r in exits)
  assert sha(BASE.parent/'stage1/qemu_stage1/main/HMM2.ELF')==digest
  assert sha(BASE/f'module{n}_repro/HMM2.ELF')==digest
  manifest.update(exit_checks=exits,parallel_render=dict(game_core=1,worker_core=0,worker_priority=3,worker_stack=3072,pcm_core=0,pcm_priority=5,pcm_stack=4096,sd_owned_by_game=True),parallel_profile=read(BASE/f'trace{n}_scroll/gatehell/parallel_pipeline.json'),audio_control_comparison=read(BASE/f'parallel{n}_audio_control.json'))
  assert all(j.get('graphic_worker',{}).get('priority')==3 and j['audio']['minimum_free_stack']>=1024 for j in qemu)
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
 text=f'''HMM2 v12b, build {n} — промежуточное тестовое обновление

Заменить только HMM2.ELF. DAT (291675244 байта) и _HMM2.SPG остаются от выданного v12. Большой DAT повторно переносить не нужно. Настройки и сохранения не заменять.

ELF {module.stat().st_size} байт, SHA256 {digest}.

Сохранены исходные кадры анимации, порядок слоёв, музыка, SFX, RTC Ево и Map loading... с WAIT. Ускорены статические участки Redraw, вывод 32x32 через CELL, склейка тумана, полное копирование кадра и обновление палитры. Мышь с тенью остаётся последним слоем.

Проверки: KINGDTH 370/370, Alteris 343/343, анимация/прокрутка/палитра 122/122, новая игра 439/439, совместный стенд 370/370, WAIT 13/13. Полная RGB-картинка 931 кадра, включая тени, совпала с предыдущей проверенной сборкой. Настоящий BL616 исполнил потоки без ошибок.

Сцен QEMU при 7 МиБ: {len(qemu)}; максимум DL {max(j['max_dl_words'] for j in qemu)} слов; минимум PSRAM {min(j['minimum_free'] for j in qemu)} байт; свободный стек {min(j['stack']['minimum_free_bytes'] for j in qemu)}/16384 байта. Все DL проверены при повторном scanout.

Цель полной перерисовки за 20 мс ещё не подтверждена. Измерения в manifest.json, render_profiles: весь путь подготовки кадра без стендовой проверки стека. При 15 тактах/команду и 240 МГц 20 мс соответствуют 320000 команд; порог 1,3 млн из задания — 81,25 мс. Время ожидания MAPS_DELAY не считается работой рендера.

Физическая плата этой сборкой не запускалась и не прошивалась. Обновление приготовлено локально на E:.
'''
 if a.intermediate:
  text+='\nCore1 сохраняет логику игры, SD и подготовку DL. Core0 считает геометрию/перевод PAL8 (priority3), пока core1 готовит звук или отправляет предыдущий блок 8 КиБ. Подача PCM на core0 имеет priority5; worker не обращается к SD, аллокатору или изменяемым игровым объектам. Однопроходный PAL8 VDAC4, RAM_G 3 МиБ.\n'
  text+='Звук: 42 OGG-трека, точные отсчёты SFX и восстановление позиции музыки проверены. В контрольной сцене до/после по 2 паузы PCM при начальной загрузке, максимум около423/422 мс по часам модели; отсутствие всех пауз не заявляется. Громкости в пользовательском CFG могут оставаться0.\n'
  text+='Это промежуточная проверенная сборка. Повторный холодный полный кадр отдельно не измерялся; профиль scroll включает обычные кадры и полные перерисовки при пересечении тайла. Проверены выход Q и возврат SLOT0 с остановкой вспомогательных задач.\n'
 (out/'README.txt').write_text(text,encoding='utf-8')
 archive=out.with_suffix('.zip')
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
  for name in ['HMM2.ELF','LICENSE','README.txt','manifest.json']: z.write(out/name,name)
 print(json.dumps(dict(folder=str(out),archive=str(archive),elf_bytes=module.stat().st_size,sha256=digest),ensure_ascii=False))
if __name__=='__main__': main()
