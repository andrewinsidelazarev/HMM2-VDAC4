"""Codex: сравнение полных сериализованных миров на всех картах, одинаковый seed, отдельные процессы."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
variant=sys.argv.pop(1);assert variant in ['baseline','stream']
text=(ROOT/'tests/run_profile.py').read_text(encoding='utf-8')
text=text.replace("TMP=Path('E:/zx/cache/tmp/hmm2_esp')","TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')")
text=text.replace("TMP/'host32/profile.exe'",f"TMP/'host32/world_{variant}.exe'")
text=text.replace("label=f'runs_extra_{a.resolution}' if a.extra else 'runs'",f"label='world_{variant}'")
text=text.replace("report=f'profile_extra_{a.resolution}.json' if a.extra else 'profile_runs.json'",f"report='world_{variant}_runs.json'")
exec(compile(text,str(ROOT/'tests/run_profile.py'),'exec'),{'__file__':str(ROOT/'tests/run_profile.py'),'__name__':'__main__'})
