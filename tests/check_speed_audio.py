"""Codex: побайтный PCM и отметки теста; отмена SD может завершить прогон раньше."""
from pathlib import Path
import argparse,hashlib,json,re

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('before',type=Path);ap.add_argument('after',type=Path);ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args();pcm=[];events=[]
    for folder in (args.before,args.after):
        log=(folder/'audio.log').read_text(encoding='utf-8',errors='replace')
        assert 'complete tracks=42 sfx=ok loops=ok' in log
        assert 'cancel_headers=ok cancel_body=ok' in log
        assert 'AUDIO stopped starves=0' in log
        events.append(re.findall(r'AUDIO TEST (?:track_begin=.*|resume_pcm_begin=.*|sfx_pcm_begin=.*|ambient_pcm_begin=.*|cancellation pass=.*)',log))
        pcm.append((folder/'audio.f32').read_bytes())
    assert events[0]==events[1], 'Изменились позиции начала музыки/SFX/петель или результаты отмены'
    common=min(map(len,pcm));assert common%8==0
    assert pcm[0][:common]==pcm[1][:common], 'Изменились отсчёты PCM'
    result=dict(exact_common_stereo_frames=common//8,frames_before=len(pcm[0])//8,frames_after=len(pcm[1])//8,
                exact_event_positions=True,common_sha256=hashlib.sha256(pcm[0][:common]).hexdigest(),
                note='Различная длина хвоста после искусственной ошибки SD не считается полным равенством файлов.')
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(result)

if __name__=='__main__':main()
