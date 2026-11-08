"""Codex: тот же ELF, реальные PS/2 клавиши; для проверки памяти выбираем карту каталогом тестового носителя."""
from pathlib import Path
import struct,sys,shutil,os
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'));out=BASE/'qemu_stage1'
package=Path(os.environ.get('HMM2_QEMU_DAT',str(BASE/'data/HMM2.DAT')))
(out/'run').mkdir(parents=True,exist_ok=True)
# Codex: каждый прогон получает чистый временный носитель; старый SAV не доказывает новую запись.
for old in (out/'run').iterdir():
    if old.is_file():old.unlink()
shutil.copy2(Path(__file__).resolve().parents[1]/'main/fheroes2.cfg',out/'run/fheroes2.cfg')
# Клод: HMM2_QEMU_CFG — настройки пользователя с карты: сетка и тени боя меняют то, что рисуется.
if os.environ.get('HMM2_QEMU_CFG'):shutil.copy2(os.environ['HMM2_QEMU_CFG'],out/'run/fheroes2.cfg')
mode=sys.argv[1] if len(sys.argv)>1 else 'menu'
(out/'internal_limit.bin').write_bytes(struct.pack('<I',0 if '--unlimited-internal' in sys.argv else 1))
# Codex: обычный запуск не наследует закреплённый seed и интерактивный ввод прошлого опыта.
for p in [out/'catalogue.bin',out/'ui_test.bin',out/'ui_keys.bin',out/'ui_times.bin',out/'exit_test.bin',out/'ui_mouse.bin',out/'data_path.bin',out/'rng_fixture.bin',out/'ui_live.bin',out/'ui_pointer_direct.bin']:
    if p.exists():p.unlink()
(out/'ft_fault.bin').unlink(missing_ok=True)
(out/'cmd_torn.bin').unlink(missing_ok=True)
(out/'alloc_fault.bin').unlink(missing_ok=True)
if os.environ.get('HMM2_QEMU_DAT'):(out/'data_path.bin').write_bytes(str(package.resolve()).replace('\\','/').encode('utf-8')+b'\0')
# Codex: задержка задаётся явно, обычные сцены не наследуют прежний стресс-режим.
latency=out/'sd_latency.bin'
if latency.exists():latency.unlink()
if os.environ.get('HMM2_QEMU_SD_LATENCY'):
    values=list(map(int,os.environ['HMM2_QEMU_SD_LATENCY'].split(',')));assert len(values)==2 and values[1]>0
    latency.write_bytes(struct.pack('<2I',*values))
if mode!='menu':
    (out/'ui_test.bin').write_bytes(struct.pack('<I',400))
    if mode=='battle':
        (out/'ui_keys.bin').write_bytes(bytes([0x31,0x32,0x5a,0x1c,0x5a]))
        (out/'ui_times.bin').write_bytes(struct.pack('<5I',2500,5000,7500,22000,25000))
    elif mode not in ('ui','load'):
        with package.open('rb') as f:
            header=f.read(16);n=struct.unpack_from('<I',header,8)[0]
            # Codex: каталог FS3 лежит в хвосте; подмена сохраняет его адрес и не перекрывает AGG в начале DAT.
            if struct.unpack_from('<I',header,4)[0]>=3:
                f.seek(struct.unpack_from('<I',header,12)[0])
            entries=[f.read(72) for _ in range(n)]
        selected=[e for e in entries if not e.startswith(b'maps/') or e.split(b'\0',1)[0].decode().lower()=='maps/'+mode.lower()]
        assert sum(e.startswith(b'maps/') for e in selected)==1,(mode,len(selected))
        (out/'catalogue.bin').write_bytes(header[:8]+struct.pack('<I',len(selected))+header[12:16]+b''.join(selected))
        e=next(e for e in selected if e.startswith(b'maps/'));at=struct.unpack_from('<I',e,64)[0]
        with package.open('rb') as f:f.seek(at);h=f.read(26)
        required=sum(bool(h[14+i]) and not h[20+i] for i in range(6))
        if required>1:
            key={2:0x1e,3:0x26,4:0x25,5:0x2e,6:0x36}[required]
            (out/'ui_keys.bin').write_bytes(bytes([0x31,0x3a,0x33,key,0x5a,0x5a,0x5a,0x5a,0x2c,0x5a]))
        if '--extended' in sys.argv:
            # Codex: первый Enter запускает карту, следующие двадцать попарно
            # открывают/закрывают фокус. Нечётный остаток оставлял окно героя,
            # и поздний Esc вместо закрытия окна вызывал Quit на карте.
            keys=([0x31,0x3a,0x33,key] if required>1 else [0x31,0x1b])+[0x5a]*21+[0x2c,0x5a,0x76,0x06,0x5a]
            times=[2500,5000,7500,10000][:4 if required>1 else 2]+list(range(16000,79000,3000))+[80000,83000,87000,90000,95000]
            (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<I',900))
        if mode.lower()=='gatehell.mp2' and '--extended' in sys.argv:
            # Codex: следующий Enter только после окончания загрузки; иначе два Enter
            # попадают в один HandleEvents, меняют чётность окон, а Esc открывает выход.
            times=[2500,5000,7500,10000][:4 if required>1 else 2]+[16000]+list(range(22000,82000,3000))+[86000,89000,93000,96000,101000]
            assert len(times)==len(keys)
            (out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times))
        if mode.lower()=='ghostplt.mx2' and '--extended' in sys.argv:
            # Codex: у GHOSTPLT нет вступительных диалогов GateHell. Поздний Esc
            # после парных Enter попадал на карту и штатно завершал игру до 900 кадров.
            # Оставляем повторные открытия исходного героя и вход в город, затем ожидание.
            keys=([0x31,0x3a,0x33,key] if required>1 else [0x31,0x1b])+[0x5a]*21+[0x2c,0x5a]
            times=[2500,5000,7500,10000][:4 if required>1 else 2]+list(range(16000,79000,3000))+[80000,83000]
            (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times))
        if '--town' in sys.argv:
            keys=[0x31,0x1b,0x5a,0x2c,0x5a,0x76,0x33,0x72,0x72]
            times=[2500,5000,7500,16000,19000,45000,50000,52000,55000]
            (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<I',700))
        if '--week' in sys.argv:
            assert required<=1
            # Codex: семь обычных завершений хода клавишей E, затем F2; ИИ исполняет исходную игру.
            keys=[0x31,0x1b,0x5a];times=[2500,5000,7500]
            for day in range(7):
                start=16000+day*16000
                keys += [0x24,0x5a];times += [start,start+1000]
            keys += [0x5a,0x06];times += [132000,140000]
            (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<II',1900,155000))
            config=(out/'run/fheroes2.cfg').read_text(encoding='utf-8').replace('auto resolve battles = off','auto resolve battles = on')
            (out/'run/fheroes2.cfg').write_text(config,encoding='utf-8')
        if '--deep-town' in sys.argv:
            assert required<=1
            # Codex: строительство и герой; дополнительные клавиши зданий действуют только при их наличии.
            keys=[0x31,0x1b,0x5a,0x2c,0x5a,0x32,0x76,0x1b,0x76,0x3a,0x76,0x1d,0x76,0x76,0x33,0x5a,0x76]
            times=[2500,5000,7500,16000,19000,24000,30000,35000,40000,45000,50000,55000,60000,65000,70000,75000,90000]
            (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<I',1100))
if mode=='load':
    # Codex: единственное сохранение стенда — GateHell, созданное выпускаемым ELF в предыдущем тесте.
    source=Path(os.environ.get('HMM2_QEMU_SAVE',str(BASE/'runs/target_save64/GateHell.savh')));assert source.exists()
    shutil.copy2(source,out/'run/H2S00000.SAV')
    # Клод: расширение задаёт вид игры в окне загрузки: .savh — hot seat, сохранение обычной игры в нём не видно.
    index=bytearray(8192);name=os.environ.get('HMM2_QEMU_SAVE_NAME','quicksave.savh').encode();index[:len(name)]=name
    assert len(index)==8192
    (out/'run/HMM2.IDX').write_bytes(index)
    keys=[0x4b,0x3a,0x33,0x5a,0x5a,0x2c,0x5a,0x76,0x06]
    times=[2500,5000,7500,12000,25000,50000,55000,70000,75000]
    (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<I',1000))
if '--recruit' in sys.argv:
    # Codex: оригинальный пользовательский SAV; после входа в город двенадцать повторов жилища1.
    index=bytearray(8192);name=b'AUTOSAVE.sav';index[:len(name)]=name;(out/'run/HMM2.IDX').write_bytes(index)
    keys=[0x4b,0x1b,0x5a,0x2c,0x5a];times=[2500,5000,7500,30000,33000]
    for i in range(12):keys += [0x16,0x76];times += [39000+i*6000,42000+i*6000]
    keys += [0x76];times += [114000]
    (out/'ui_keys.bin').write_bytes(bytes(keys));(out/'ui_times.bin').write_bytes(struct.pack('<'+'I'*len(times),*times));(out/'ui_test.bin').write_bytes(struct.pack('<II',30,120000))
shutil.copy2(out/'run/fheroes2.cfg',out/'initial.cfg')
if os.environ.get('HMM2_QEMU_MOUSE'):
    (out/'ui_mouse.bin').write_bytes(struct.pack('<II',3,1000))
print('Стенд подготовлен:',mode)
