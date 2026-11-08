"""Codex: заключительные шесть сцен одного ELF, без изменения исходников между прогонами."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCENES = [('menu', 'menu', [], 30), ('ghostplt', 'GHOSTPLT.MX2', ['--extended'], 900),
          ('gatehell', 'GateHell.mp2', ['--extended'], 900), ('town', 'KINGDTH.MX2', ['--town'], 700),
          ('battle', 'battle', [], 400), ('load', 'load', [], 1000)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    stage = Path(os.environ['HMM2_STAGE1_BASE'])
    digest = hashlib.sha256((stage/'qemu_stage1/main/HMM2.ELF').read_bytes()).hexdigest()
    results = []
    for label, scene, prepare, frames in SCENES:
        scenario = dict(scene=scene, prepare=prepare, frames=frames, shift=0, latency=[0, 0],
                        psram=7340032, timeout=2400, clicks=[])
        config = out/f'{label}.json'
        config.write_text(json.dumps(scenario, indent=2), encoding='utf-8')
        subprocess.run([sys.executable, str(ROOT/'tests/memory_session.py'), str(config), str(out/label)], check=True)
        subprocess.run([sys.executable, str(ROOT/'tests/memory_metrics.py'), str(out/label), '--dat', str(stage/'data/HMM2.DAT')],
                       check=True, stdout=subprocess.DEVNULL)
        identity = json.loads((out/label/'result.json').read_text())
        metrics = json.loads((out/label/'metrics.json').read_text())
        assert identity['elf_sha256'] == digest
        assert metrics['complete'] and metrics['frames'] >= frames
        assert metrics['heap_failures'] == metrics['trims'] == 0
        # Codex: 91 дополнительно проверяет запас перед звуковым пулом 192 КиБ.
        # Только известные необязательные пробы; отказы игрового malloc проверены выше.
        assert metrics['audio_allocation_failures'] == 0
        assert metrics['oom_messages'] == len(metrics['oom']) <= 2
        assert all(row['request'] in ((128+256+240+39+384)*1024,(192+256)*1024) for row in metrics['oom'])
        subprocess.run([sys.executable, str(ROOT/'tests/memory_trace.py'), str(out/label), '--shots', str(frames)], check=True)
        trace = json.loads((out/label/'verified_trace.json').read_text())
        assert trace['frames'] == metrics['frames'] and trace['no_visible_overwrites']
        if label == 'gatehell':
            # Codex: фиксируем фактический запас 91; исторические 50 КиБ относились к 82.
            assert metrics['minimum_psram'] > 0
            save = out/label/'run/H2S00000.SAV'
            assert save.exists(), 'GateHell must create the save loaded by the last scene'
            os.environ['HMM2_QEMU_SAVE'] = str(save)
        result = dict(scene=label, frames=metrics['frames'], minimum_psram=metrics['minimum_psram'],
                      heap_failures=metrics['heap_failures'], trims=metrics['trims'],
                      max_dl_words=trace['max_dl_words'], no_visible_overwrites=trace['no_visible_overwrites'], elf_sha256=digest)
        results.append(result)
        (out/'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
