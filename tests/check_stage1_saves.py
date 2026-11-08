"""Codex: оба направления SAV проверяются разными реализациями сжатия и сериализации мира."""
from pathlib import Path
import subprocess,sys,os,shutil,json,hashlib
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def run(packed,existing):
    env=os.environ.copy()
    if existing:env['HMM2_TEST_LOAD_EXISTING']='1'
    else:env.pop('HMM2_TEST_LOAD_EXISTING',None)
    subprocess.run([sys.executable,str(ROOT/'tests/run_stage1_scene.py'),'--packed' if packed else '--x64','--save'],env=env,check=True,stdout=subprocess.DEVNULL)
def main():
    packed=BASE/'runs/scene_packed_save';ref=BASE/'runs/scene64_save';(ref/'run/files/save').mkdir(parents=True,exist_ok=True)
    run(True,False)
    index=(packed/'run/HMM2.IDX').read_bytes();slot=next(i for i in range(64) if index[i*128:(i+1)*128].split(b'\0',1)[0].lower()==b'king_of_death_day_1.sav')
    target=packed/f'run/H2S{slot:05}.SAV';original=ref/'run/files/save/King_of_Death_day_1.sav'
    shutil.copy2(target,original);run(False,True)
    expected=(packed/'states/moved.bin').read_bytes();assert (ref/'states/reloaded.bin').read_bytes()==expected
    run(False,False);shutil.copy2(original,target);run(True,True)
    assert (packed/'states/reloaded.bin').read_bytes()==expected
    result=dict(bytes=len(expected),sha256=hashlib.sha256(expected).hexdigest(),streamed_to_original=True,original_to_streamed=True)
    (ROOT/'Docs/evidence/stage1_saves.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
