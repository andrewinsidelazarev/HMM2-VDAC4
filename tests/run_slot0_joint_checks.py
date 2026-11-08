"""Codex: независимые процессы стенда, общий носитель только для чтения."""
from pathlib import Path
import concurrent.futures,json,os,shutil,subprocess,sys
R=Path(__file__).resolve().parents[1];B=Path('E:/zx/cache/tmp/hmm2_esp/stage1');O=Path(os.environ.get('HMM2_EXIT_EVIDENCE','E:/zx/cache/tmp/hmm2_esp/slot0_autounload'))
def main():
    O.mkdir(parents=True,exist_ok=True);dep=B/'launcher/third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for name in ['fat32.bin','fat32-work.bin']:shutil.copy2(B/'launcher/build'/name,dep/name)
    shutil.copy2(B/'launcher/build/hmm2_z80.bin',B/'launcher/build/hmm2.bin')
    env=dict(os.environ,HMM2_TEST_AUTOUNLOAD='1',HMM2_SKIP_SHARED_SETUP='1')
    jobs=[]
    for label,extra,folder in [('joint_reset',[],'exit_reset'),('joint_reset_before_poll',['--before-poll'],'exit_reset_before_poll'),('joint_reset_loading',['--during-load'],'exit_reset_during_load')]:
        log=(O/(label+'.log')).open('wb');p=subprocess.Popen([sys.executable,str(R/'tests/test_stage1_exit.py'),'reset',*extra,'--timeout','240'],cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT);jobs.append((label,folder,p,log))
    results=[]
    for label,folder,p,log in jobs:
        code=p.wait();log.close();print(label,code,flush=True)
        if code:raise RuntimeError((O/(label+'.log')).read_text(encoding='utf-8',errors='replace')[-2000:])
        shutil.copy2(B/'joint'/folder/'result.json',O/(label+'_result.json'));results.append(dict(scenario=label,exit=code))
    (O/'joint_results.json').write_text(json.dumps(results,indent=2))
if __name__=='__main__':main()
