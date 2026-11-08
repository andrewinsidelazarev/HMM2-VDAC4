"""Codex: сравнение одной записи ввода; PERF учитывается только целыми окнами внутри интервала."""
from pathlib import Path
import argparse
import hashlib
import json
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before', type=Path)
    ap.add_argument('after', type=Path)
    ap.add_argument('--begin', type=int, default=1460000)
    ap.add_argument('--end', type=int, default=1730000)
    args = ap.parse_args()
    controls = ['initial.cfg', 'ui_keys.bin', 'ui_times.bin', 'ui_clicks.bin', 'ui_test.bin', 'psram_budget.bin',
                'sd_latency.bin', 'internal_limit.bin', 'catalogue.bin', 'ui_click_hold.bin', 'main.c']
    hashes = {}
    for name in controls:
        a, b = (args.before/name).read_bytes(), (args.after/name).read_bytes()
        assert a == b, name
        hashes[name] = hashlib.sha256(a).hexdigest()
    assert json.loads((args.before/'scenario.json').read_text()) == json.loads((args.after/'scenario.json').read_text())
    # Codex: проверяем фактический icount команды QEMU, а не только запись сценария.
    for folder in (args.before,args.after):
        command=json.loads((folder/'command.json').read_text())
        assert command[command.index('-icount')+1]=='shift=2,align=off,sleep=off'
        config=json.loads((folder/'sdkconfig.json').read_text())
        assert config['ESP_DEFAULT_CPU_FREQ_MHZ']==160 and config['SPIRAM_SPEED']==40
        assert config['FREERTOS_HZ']==1000 and config['FREERTOS_CHECK_STACKOVERFLOW_CANARY']
    result = dict(input_sha256=hashes, begin=args.begin, end=args.end)
    # Codex: сверяем журнал отправленных событий с записью ввода. Это не заменяет
    # снимки: игра может проигнорировать верно отправленный щелчок во время движения.
    recordings = []
    audits = []
    for folder in (args.before, args.after):
        recording = folder/('replay.json' if (folder/'replay.json').exists() else 'scenario.json')
        recorded = json.loads(recording.read_text(encoding='utf-8'))
        log = (folder/'qemu.log').read_text(encoding='utf-8')
        clicks = [tuple(map(int, match)) for match in re.findall(r'QEMU: click (\d+) at (\d+),(\d+) input_ms=(\d+)', log)]
        assert len(clicks) == len(recorded['clicks']), 'Missing mouse events'
        delays = []
        for index, (sent, expected) in enumerate(zip(clicks, recorded['clicks'])):
            number, x, y, actual_time = sent
            requested_time, expected_x, expected_y, _ = expected
            assert (number, x, y) == (index, expected_x, expected_y)
            assert actual_time >= requested_time
            delays.append(actual_time-requested_time)
        keys = bytes.fromhex(''.join(re.findall(r'QEMU: PS2=([0-9a-f]{2})', log)))
        assert keys == bytes.fromhex(recorded['keys']), 'Missing keyboard events'
        recordings.append(recorded)
        audits.append(dict(clicks=len(clicks), keys=len(keys), max_click_dispatch_delay_ms=max(delays, default=0)))
    assert recordings[0] == recordings[1], 'Different executed recordings'
    result['input_audit'] = audits
    if 'rng_seed' in recordings[0]:
        fixtures = []
        for folder in (args.before, args.after):
            fixture = json.loads((folder/'rng_fixture.json').read_text(encoding='utf-8'))
            assert fixture['elf_sha256'] == hashlib.sha256((folder/'HMM2.ELF').read_bytes()).hexdigest()
            log = (folder/'qemu.log').read_text(encoding='utf-8')
            resets = [tuple(map(int, match)) for match in re.findall(
                r'QEMU: RNG world_reset=(\d+) seed=(\d+) state=(\d+) stream=(\d+)', log)]
            expected = (fixture['seed'], fixture['state'], fixture['stream'])
            assert resets == [(1, *expected)], 'Expected one seeded world load'
            fixtures.append(expected)
        assert fixtures[0] == fixtures[1]
        result['rng_fixture'] = dict(zip(['seed', 'state', 'stream'], fixtures[0]))
    # Codex: одинаковое время ввода не гарантирует ту же сцену: окно на дороге
    # может поглотить конец хода. Требуем отдельную проверку изображений обеих трасс.
    observations = []
    for folder in (args.before,args.after):
        observation = json.loads((folder/'scene_evidence.json').read_text(encoding='utf-8'))
        assert observation['visually_verified'] and observation['battle_modal'] is False
        for shot in observation['shots']:
            assert hashlib.sha256((folder/shot['file']).read_bytes()).hexdigest() == shot['sha256']
        observations.append(observation)
    for key in ['day', 'hero', 'enemy_tile', 'enemy_type', 'enemy_types', 'enemy_stacks', 'army_types', 'army_counts']:
        assert observations[0][key] == observations[1][key], 'Different game scene: '+key
    result['scene_evidence'] = observations
    for label, folder in [('before', args.before), ('after', args.after)]:
        m = json.loads((folder/f'metrics_{args.begin}_{args.end}.json').read_text(encoding='utf-8'))
        perf = [p for p in m['PERF'] if p['time']-p['wall'] >= args.begin and p['time'] < args.end]
        assert perf, 'No complete PERF windows'
        frames = sum(p['N'] for p in perf)
        ids = {p['frame'] for p in perf}
        io = [p for p in m['IO'] if p['frame'] in ids]
        reads = {name: row for name, row in m['reads'].items() if not name.startswith('music/')}
        result[label] = dict(elf=json.loads((folder/'result.json').read_text()), complete=m['complete'],
            total_frames=m['frames'], perf_frames=frames, ms_per_frame=sum(p['wall'] for p in perf)/frames,
            max_window_ms=max(p['wall']/p['N'] for p in perf), dec_bytes=sum(p['dec'] for p in io),
            non_music_reads=reads, music_reads={k:v for k,v in m['reads'].items() if k.startswith('music/')},
            minimum_psram=m['minimum_psram'], last_free=m['last_free'],
            heap_failures=m['heap_failures'], trims=m['trims'], last_resort=m['last_resort'],
            allocation_failures=m.get('oom_messages',len(m['oom'])), audio_allocation_failures=m.get('audio_allocation_failures',0),
            last_heap=m['heap_samples'][-1], last_memf=m['MEMF'][-1])
    a = result['after']
    result['passed'] = bool(a['complete'] and not (a['heap_failures'] or a['trims'] or a['last_resort'] or a['allocation_failures'] or a['audio_allocation_failures']
                           or a['dec_bytes'] or a['non_music_reads']) and a['max_window_ms'] <= 60)
    (args.after/'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    assert result['passed'], 'Long-session targets not met'


if __name__ == '__main__':
    main()
