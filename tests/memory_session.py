"""Codex: воспроизводимый ввод и архив измерений памяти настоящего ELF в QEMU."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenario', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    scenario = json.loads(args.scenario.read_text(encoding='utf-8-sig'))
    stage = Path(os.environ['HMM2_STAGE1_BASE'])
    out = stage / 'qemu_stage1'
    # Codex: журнал относится к текущему модулю, а не к прежнему ELF в прошивке.
    assert (stage/'module/HMM2.ELF').read_bytes() == (out/'main/HMM2.ELF').read_bytes(), 'Rebuild QEMU for current ELF'
    # Codex: не запускаем старую прошивку одновременно с незавершённой пересборкой стенда.
    build = out / 'build'
    flash = json.loads((build / 'flasher_args.json').read_text())
    firmware = build / flash['app']['file']
    assert firmware.stat().st_mtime_ns >= max((out/'main'/name).stat().st_mtime_ns
                                            for name in ['HMM2.ELF', 'main.c', 'test_paths.h']), 'Rebuild QEMU first'
    archive = args.output.resolve()
    archive.mkdir(parents=True, exist_ok=False)
    os.environ['HMM2_QEMU_SHIFT'] = str(scenario.get('shift', 2))
    latency = scenario.get('latency', [2, 500])
    os.environ['HMM2_QEMU_CFG'] = scenario.get('config', str(ROOT / 'tests/memory_board.cfg'))
    for setting, variable in [('dat', 'HMM2_QEMU_DAT'), ('save', 'HMM2_QEMU_SAVE'), ('save_name', 'HMM2_QEMU_SAVE_NAME')]:
        if setting in scenario:
            os.environ[variable] = scenario[setting]
    if latency[1]:
        os.environ['HMM2_QEMU_SD_LATENCY'] = ','.join(map(str, latency))
    else:
        os.environ.pop('HMM2_QEMU_SD_LATENCY', None)
    subprocess.run([sys.executable, str(ROOT / 'tests/prepare_qemu_scene.py'), scenario.get('scene', 'alteris.mp2'), *scenario.get('prepare', ['--extended'])], check=True)
    def integers(name, values):
        (out / name).write_bytes(struct.pack('<' + 'I' * len(values), *values))
    if 'keys' in scenario:
        (out / 'ui_keys.bin').write_bytes(bytes.fromhex(scenario['keys']))
        integers('ui_times.bin', scenario['key_times'])
    integers('ui_test.bin', [scenario.get('frames', 1000), scenario.get('finish_after', 0)])
    integers('psram_budget.bin', [scenario.get('psram', 6815744)])
    integers('ui_clicks.bin', [n for click in scenario.get('clicks', []) for n in click])
    # Codex: длинный сценарий 82 использовал удержание восемь опросов; обычные сцены 91 — один.
    integers('ui_click_hold.bin', [scenario.get('click_hold', 1)])
    live = out / 'ui_live.bin'
    if scenario.get('live'):
        live.write_bytes(b'1')
    elif live.exists():
        live.unlink()
    rng_file = out/'rng_fixture.bin'
    if rng_file.exists():
        rng_file.unlink()
    (archive / 'scenario.json').write_text(json.dumps(scenario, ensure_ascii=False, indent=2), encoding='utf-8')
    (archive / 'firmware.json').write_text(json.dumps({'bytes': firmware.stat().st_size,
        'sha256': hashlib.sha256(firmware.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
    shutil.copy2(build/'config/sdkconfig.json', archive/'sdkconfig.json')
    for path in out.glob('*.bin'):
        if path.name != 'flash.bin':
            shutil.copy2(path, archive / path.name)
    for name in ['HMM2.ELF', 'test_paths.h', 'main.c']:
        shutil.copy2(out / 'main' / name, archive / name)
    # Codex: символы сохраняются до запуска и только для совпадающего ELF;
    # параллельная следующая сборка не должна подменить их после длинного прогона.
    elf_hash = hashlib.sha256((archive/'HMM2.ELF').read_bytes()).digest()
    for module in [stage/'module', ROOT.parent/'stage/module']:
        packed = module/'HMM2.ELF'
        if packed.exists() and hashlib.sha256(packed.read_bytes()).digest() == elf_hash:
            for name in ['HMM2.raw.ELF', 'nm.txt', 'HMM2.map']:
                if (module/name).exists():
                    shutil.copy2(module/name, archive/name)
            break
    if 'rng_seed' in scenario:
        # Codex: getter и адрес объекта проверяются по символам совпадающего ELF.
        # Числа конструктора сверены отдельным тестом с исходным rand.h.
        symbols = {name: int(address, 16) for address, _, name in
                   (line.split(maxsplit=2) for line in (archive/'nm.txt').read_text().splitlines() if len(line.split()) == 3)}
        getter = symbols['_ZN4Rand25CurrentThreadRandomDeviceEv']
        generator = symbols['_ZZN4Rand25CurrentThreadRandomDeviceEvE3gen']
        assert symbols['_ZGVZN4Rand25CurrentThreadRandomDeviceEvE3gen']-generator == 16
        app_address = symbols['app$0']
        seed = int(scenario['rng_seed'])
        assert 0 <= seed <= 0xffffffff
        stream = 54
        state = ((stream+1+seed)*6364136223846793005+stream+1) & ((1 << 64)-1)
        getter_delta = getter-symbols['hmm_command']
        data_delta = generator-app_address
        assert -0x400000 < getter_delta < 0x400000 and -0x400000 < data_delta < 0x400000
        payload = struct.pack('<IiiIQQ', 0x474e5248, getter_delta, data_delta, seed, state, stream)
        rng_file.write_bytes(payload)
        (archive/'rng_fixture.bin').write_bytes(payload)
        (archive/'rng_fixture.json').write_text(json.dumps(dict(seed=seed, state=state, stream=stream,
            getter_delta=getter_delta, data_delta=data_delta, elf_sha256=elf_hash.hex()), indent=2), encoding='utf-8')
    shutil.copy2(out / 'initial.cfg', archive / 'initial.cfg')
    # Codex: отказ до открытия трассы не должен выдать кадры предыдущего прогона за новые.
    for name in ['menu.ft', 'reads.log', 'sd.log']:
        if (out / name).exists():
            (out / name).unlink()
    try:
        with (archive / 'run.log').open('wb') as log:
            result = subprocess.run([sys.executable, str(ROOT / 'tools/run_qemu_stage1.py'), '--icount', '--timeout', str(scenario.get('timeout', 3600))], stdout=log, stderr=subprocess.STDOUT)
    finally:
        # Codex: путь DAT не должен перейти в следующий опыт после завершения или ошибки.
        (out/'data_path.bin').unlink(missing_ok=True)
    for name in ['qemu.log', 'reads.log', 'menu.ft', 'command.json', 'qemu_exit.json']:
        if (out / name).exists():
            shutil.copy2(out / name, archive / name)
    shutil.copytree(out / 'run', archive / 'run')
    if scenario.get('live'):
        # Codex: полный исполненный ввод пригоден для обычного повторного прогона без интерактивного режима.
        scenario['keys'] = (out / 'ui_keys.bin').read_bytes().hex()
        scenario['key_times'] = [x[0] for x in struct.iter_unpack('<I', (out / 'ui_times.bin').read_bytes())]
        scenario['clicks'] = [list(x) for x in struct.iter_unpack('<4I', (out / 'ui_clicks.bin').read_bytes())]
        scenario['frames'], scenario['finish_after'] = struct.unpack('<2I', (out / 'ui_test.bin').read_bytes())
        scenario['live'] = False
        (archive / 'replay.json').write_text(json.dumps(scenario, ensure_ascii=False, indent=2), encoding='utf-8')
        for name in ['ui_keys.bin', 'ui_times.bin', 'ui_clicks.bin', 'ui_test.bin']:
            shutil.copy2(out / name, archive / name)
    metadata = {'returncode': result.returncode, 'elf_bytes': (archive / 'HMM2.ELF').stat().st_size,
                'elf_sha256': hashlib.sha256((archive / 'HMM2.ELF').read_bytes()).hexdigest()}
    (archive / 'result.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(json.dumps(metadata), flush=True)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
