"""Codex: точное сравнение минутного PCM кольца PSRAM с независимым источником OGG."""
from pathlib import Path
import hashlib,json,re,sys

def main():
    ring,direct=map(Path,sys.argv[1:3])
    def capture(stem):
        log=stem.with_suffix('.log').read_text(encoding='utf-8')
        assert 'AUDIO RING complete' in log
        values=tuple(map(int,re.search(r'AUDIO RING loaded=(\d+) consumed=(\d+) capacity=(\d+) frames=(\d+)',log).groups()))
        data=stem.with_suffix('.f32').read_bytes()
        assert len(data)==values[3]*8
        return values,data
    r,pcm=capture(ring);d,reference=capture(direct)
    assert r[2]==131072 and r[1]>r[2]*2 and r[1]<=r[0]<=r[1]+r[2]
    assert d[0]==d[1]==r[1] and pcm==reference
    result=dict(pcm_equal=True,stereo_frames=r[3],bytes=len(pcm),seconds=r[3]/44100,
                compressed_loaded=r[0],compressed_consumed=r[1],capacity=r[2],wraps=r[1]//r[2],
                sha256=hashlib.sha256(pcm).hexdigest(),ring_source=str(ring),direct_source=str(direct))
    ring.with_suffix('.check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
