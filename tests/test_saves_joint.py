"""Codex: сохранение 180 КиБ, чтение, seek, EOF, отказы SD и запасной путь без PUT_READY."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
from analyze_saves import requests,analyze

ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))
# Codex: журналы и образ тестовой SD не засоряют каталог сборки.
RUNS=Path(os.environ.get('HMM2_JOINT_RUNS',str(BASE/'joint')))
CASES=('ok','write','nospace','last','sync','fallback')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--verify-only',action='store_true');ap.add_argument('--prefix',default='verified_');a=ap.parse_args()
    elf=hashlib.sha256((BASE/'module/HMM2.ELF').read_bytes()).hexdigest()
    spg=hashlib.sha256((BASE/'launcher/_HMM2.SPG').read_bytes()).hexdigest();proof=[]
    expected=bytes(((i*79)^(i>>7))&255 for i in range(180*1024+137))
    for case in CASES:
        name=a.prefix+case;out=RUNS/name;out.mkdir(parents=True,exist_ok=True)
        if not a.verify_only:
            env=os.environ.copy()
            for key in ('HMM2_TEST_SAVE','HMM2_TEST_RECRUIT_SAVE','HMM2_JOINT_EXTRA','HMM2_SAVES_NO_READY','HMM2_JOINT_BUILD'):env.pop(key,None)
            env['HMM2_SAVES_PROBE']='ok' if case=='fallback' else case
            if case=='fallback':env['HMM2_SAVES_NO_READY']='1'
            with (out/'run.log').open('wb') as log:
                subprocess.run([sys.executable,str(ROOT/'tests/test_stage1_joint.py'),'--name',name,'--timeout','100'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=130,check=True)
        result=json.loads((out/'result.json').read_text(encoding='utf-8'))
        log=(out/'esp.log').read_text(encoding='utf-8')
        assert result['exit']==0 and result['stage']==12 and result['code']==0,result
        assert result['elf_sha256']==elf and result['spg_sha256']==spg
        assert 'SAVES PROBE COMPLETE' in log
        if case in ('ok','fallback'):
            assert (out/'sd/PROBE.SAV').read_bytes()==expected
            assert 'short_read=rejected' in log
            rows=[r for r in requests(out) if r['name']=='PROBE.SAV']
            writes=[r for r in rows if r['op']==8]
            assert len(writes)==25 and sum(r['pulls'] for r in writes)==25
            assert sum(r['fat']['FAT32_FIND'] for r in writes)==1
            assert sum(r['fat']['FAT32_FILEX'] for r in writes)==0
            assert sum(r['fat']['FAT32_CLOSE']+r['fat']['FAT32_SYNC'] for r in writes)==0
            if case=='fallback':assert result['commands'].get('71',0)>0
            else:
                reads=[r for r in rows if r['op']==7 and r['offset']%7680==0 and not r['status']]
                assert len(reads)==25 and sum(r['fat']['FAT32_READ'] for r in reads)==25
                assert not sum(r['fat']['FAT32_FILEX'] for r in reads)
        else:assert 'SAVES PROBE expected error mode='+case in log
        proof.append(dict(case=case,status='PASS',result=str(out/'result.json')))
        print(proof[-1],flush=True)
    (RUNS/'saves_checks.json').write_text(json.dumps(dict(elf_sha256=elf,spg_sha256=spg,cases=proof),indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
