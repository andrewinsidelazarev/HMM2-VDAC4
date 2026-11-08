"""Codex: исходные сцены игры и CPU-оракул в изолированной папке этого задания."""
from pathlib import Path
import argparse
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('output', type=Path)
    ap.add_argument('--map', default='KINGDTH.MX2')
    ap.add_argument('--dat', type=Path, help='Codex: отдельный DAT, включая сырой GFX карты пользователя')
    ap.add_argument('--no-oracle', action='store_true', help='Codex: замер запросов игры без дополнительных чтений CPU-оракула')
    args = ap.parse_args()
    stage = Path(os.environ['HMM2_STAGE1_BASE'])
    executable = stage/'host64_packed/hmm2_host.exe'
    sources = list((ROOT/'main').glob('*.cpp'))+list((ROOT/'main').glob('*.c'))+list((ROOT/'main').glob('*.inc'))+[ROOT/'tests/stage1_scene.cpp', ROOT/'tests/stage1_files.cpp']
    assert executable.stat().st_mtime_ns >= max(p.stat().st_mtime_ns for p in sources), 'Rebuild native with --host --x64 --packed'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out/'states').mkdir()
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               HMM2_HOST_DAT=str((args.dat or stage/'data/HMM2.DAT').resolve()), HMM2_TEST_MAP='maps/'+args.map,
               HMM2_STATE_OUT=str(out/'states'), HMM2_CANVAS_ORACLE='1',
               TEMP=str(ROOT.parent/'temp'), TMP=str(ROOT.parent/'temp'))
    if args.no_oracle:
        env.pop('HMM2_CANVAS_ORACLE', None)
    with (out/'run.log').open('wb') as log:
        result = subprocess.run([str(executable), str(out), '20000'],
                                cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    result.check_returncode()
    assert 'SCENARIO COMPLETE' in (out/'run.log').read_text(encoding='utf-8')
    print(out)


if __name__ == '__main__':
    main()
