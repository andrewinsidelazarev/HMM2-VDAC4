"""Codex: сохраняем новый прогон 97 карт отдельно от базового этапа 0."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'tests/run_profile.py').read_text(encoding='utf-8')
text=text.replace("TMP=Path('E:/zx/cache/tmp/hmm2_esp')","TMP=Path('E:/zx/cache/tmp/hmm2_esp/stage0b')")
text=text.replace("'profile_extra_{a.resolution}.json'","'stage0b_profile_extra_{a.resolution}.json'")
text=text.replace("'profile_runs.json'","'stage0b_profile_runs.json'")
exec(compile(text,str(ROOT/'tests/run_profile.py'),'exec'),{'__file__':str(ROOT/'tests/run_profile.py'),'__name__':'__main__'})
