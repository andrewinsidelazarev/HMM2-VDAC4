"""Codex: PCM всех треков и точные отсчёты SFX из предоставленного AGG."""
from pathlib import Path
import argparse,json,re,struct,sys
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np

def main():
    ap=argparse.ArgumentParser();ap.add_argument('stem',type=Path);a=ap.parse_args();base=a.stem
    x=np.fromfile(base.with_suffix('.f32'),dtype='<f4').reshape(-1,2);log=base.with_suffix('.log').read_text(encoding='utf-8')
    assert 'complete tracks=42 sfx=ok loops=ok' in log
    peaks=[float(np.max(np.abs(x[int(i*2.5*44100):int((i+1)*2.5*44100)]))) for i in range(42)]
    agg=Path('E:/zx/HMM2/Assets/Original/DATA/HEROES2.AGG').read_bytes();n=struct.unpack_from('<H',agg)[0]
    for i in range(n):
        name=agg[len(agg)-15*n+i*15:len(agg)-15*n+(i+1)*15].split(b'\0')[0]
        if name==b'BOARATTK.82M':
            _,at,size=struct.unpack_from('<3I',agg,2+i*12);raw=agg[at:at+size]
    start=int(re.search(r'sfx_pcm_begin=(\d+)',log)[1]);want=np.repeat((np.frombuffer(raw,dtype='u1').astype(np.float32)-128)/128*.6,2)
    err=float(np.max(np.abs(x[start:start+len(want)]-want[:,None])))
    assert min(peaks)>.0001 and err<1e-6 and np.all(np.isfinite(x)) and np.max(np.abs(x))<=1
    resume_check=None
    if 'resume_pcm_begin=' in log:
        source=int(re.search(r'track_begin=42 frame=(\d+)',log)[1]);resume=int(re.search(r'track=42 mode=1 resume=([1-9]\d*)',log)[1]);begin=int(re.search(r'resume_pcm_begin=(\d+)',log)[1]);end=int(re.search(r'sfx_pcm_begin=(\d+)',log)[1])
        wanted=x[source+resume:source+resume+256];segment=x[begin:end]
        candidates=np.flatnonzero(np.all(segment==wanted[0],axis=1));matches=[int(i) for i in candidates if i+len(wanted)<=len(segment) and np.array_equal(segment[i:i+len(wanted)],wanted)]
        assert matches,'MusicRestorer должен продолжить исходный PCM с сохранённой позиции'
        resume_check=dict(frame=resume,exact_match_offsets=matches,stereo_frames=len(wanted))
    result=dict(frames=len(x),tracks=42,track_peaks=peaks,sfx_frames=len(want),sfx_max_error=err,music_resume=resume_check)
    base.with_suffix('.check.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print({k:v for k,v in result.items() if k!='track_peaks'})
if __name__=='__main__':main()
