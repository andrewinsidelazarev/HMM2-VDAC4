"""Codex: отдельный каталог кадров; текущий прогон 97 карт не затрагивается."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'tests/run_profile.py').read_text(encoding='utf-8')
text=text.replace("TMP=Path('E:/zx/cache/tmp/hmm2_esp')","TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')")
text=text.replace("TMP/'host32/profile.exe'","TMP/'host32/profile_smk.exe'")
text=text.replace("label=f'runs_extra_{a.resolution}' if a.extra else 'runs'","label='runs_trace'")
text=text.replace("report=f'profile_extra_{a.resolution}.json' if a.extra else 'profile_runs.json'","report='stage0b_trace.json'")
if len(sys.argv)==1:sys.argv+=['--map','KINGDTH.MX2']
exec(compile(text,str(ROOT/'tests/run_profile.py'),'exec'),{'__file__':str(ROOT/'tests/run_profile.py'),'__name__':'__main__'})
