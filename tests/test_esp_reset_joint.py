"""Codex: однократное восстановление старта на совместном ESP/Z80-стенде."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = Path(os.environ.get('HMM2_STAGE1_BASE', 'E:/zx/cache/tmp/hmm2_esp/stage1'))
# Codex: тот же каталог результатов передаётся дочерним совместным прогонам.
RUNS = Path(os.environ.get('HMM2_JOINT_RUNS', str(BASE/'joint')))
CASES = {
    'normal': (0, 255, 1500, False),
    'once': (1, 255, 1500, True),
    'twice': (2, 255, 1500, False),
    'memory_twice': (2, 254, 1500, True),
    'slow_warm': (1, 255, 4000, False),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    ap.add_argument('--data', type=Path)
    ap.add_argument('--verify-only', action='store_true')
    args = ap.parse_args()
    rows = []
    expected_sha = hashlib.sha256((BASE/'launcher/_HMM2.SPG').read_bytes()).hexdigest()
    for name in args.cases:
        failures, status, boot_ms, nested = CASES[name]
        out = RUNS/('esp_reset_'+name)
        out.mkdir(parents=True, exist_ok=True)
        if not args.verify_only:
            env = os.environ.copy()
            if args.data:
                env['HMM2_JOINT_DAT'] = str(args.data.resolve())
            if name == 'slow_warm':
                env['HMM2_JOINT_CACHE_POLICY'] = 'warm'
            command = [sys.executable, str(ROOT/'tests/test_stage1_joint.py'),
                       '--name', out.name, '--timeout', '180', '--scene-map', 'KINGDTH.MX2', '--start-failures', str(failures),
                       '--start-status', str(status), '--reboot-ms', str(boot_ms)]
            if nested:
                command.append('--nested')
            with (out/'run.log').open('wb') as log:
                run = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240)
            assert run.returncode == 0, (name, out/'run.log')
        report = json.loads((out/'result.json').read_text(encoding='utf-8'))
        reset = report['esp_reset']
        assert report['spg_sha256'] == expected_sha
        assert reset['failures'] == failures and reset['status'] == status and reset['boot_ms'] == boot_ms
        assert reset['attempts'] == (2 if failures else 1)
        assert reset['commands'] == reset['reboots'] == reset['sessions'] == int(bool(failures))
        assert reset['requests_without_session'] == 0
        if failures == 2:
            assert report['frames'] == 0 and report['stage'] == 10 and report['code'] == status
            assert report['lifecycle']['starts'] == 0 and not report['lifecycle']['loaded']
        else:
            assert report['frames'] == 370 and report['exit'] == 0
            assert report['stage'] == 12 and report['code'] == 0
            assert report['lifecycle']['starts'] == 1
        if name == 'slow_warm':
            # Codex: первый ELF взят из кэша; после сброса оба модуля загружены заново.
            assert not any(e['command'] == '6B' and e['reset'] == 0 for e in reset['controls'])
            assert reset['offline_polls'] > 0
            assert report['cache_policy']['ogg_present'] and report['cache_policy']['game_present']
        rows.append(dict(case=name, status='PASS', frames=report['frames'],
                         starts=reset['attempts'], resets=reset['commands'], result=str(out/'result.json')))
        print(rows[-1], flush=True)
    (RUNS/'esp_reset_results.json').write_text(
        json.dumps(dict(spg_sha256=expected_sha, tests=rows), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
