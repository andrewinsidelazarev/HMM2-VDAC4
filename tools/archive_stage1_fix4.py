"""Codex: проверяем и сохраняем свидетельства fix4; не разрешаем выдачу неполной реализации."""
from pathlib import Path
import hashlib,json,re,shutil,sys,zipfile
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
EV=ROOT/'Docs/evidence'
sys.path.insert(0,str(ROOT/'tests'))
from check_fix4_ft import expected

def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def read(path):return json.loads(path.read_text(encoding='utf-8'))

def examine(label):
    out=BASE/'joint'/label;r=read(out/'result.json');ft=read(out/'ft_check.json')
    assert r['elf_sha256']==sha(BASE/'module/HMM2.ELF')
    assert r['spg_sha256']==sha(BASE/'launcher/_HMM2.SPG')
    native=r['native']
    assert native['sha256']==sha(BASE/'joint'/native['dll'])
    for kind in ['sources','objects']:
        for name,h in native[kind].items():assert sha(Path(name))==h,name
    assert ft['frames']==r['frames'] and ft['exact_ft'] and ft['no_visible_overwrites']
    assert ft['resolution']==[1024,768] and r['stage']==12 and r['code']==0
    assert r['spi_stats'][2]==r['spi_stats'][3]==0
    assert r['spi_stats'][11]<=16384 and r['spi_stats'][12]<4096
    assert r['commands']['31']>0 and r['commands']['76']>0
    log=(out/'esp.log').read_text(encoding='utf-8')
    assert 'FAIL ' not in log and 'SD reject' not in log
    starts=re.findall(r'HMM2 SD >([A-F0-9]+) op=(\d+) (\S+) seq=(\d+) off=(\d+) len=(\d+)',log)
    ends=re.findall(r'HMM2 SD <([A-F0-9]+) seq=(\d+) result=(-?\d+) wait=(\d+) rx=([0-9A-F]+)/(\d+)/(\d+)/([0-9A-F]+)',log)
    assert len(starts)==len(ends)>0 and starts[0]==('E4','1','HMM2.DAT','1','0','32768')
    for s,e in zip(starts,ends):assert s[0]==e[0] and s[3]==e[1]==e[5] and int(e[3])<10000
    reads=[e for e in ends if e[0]=='E4']
    def throughput(rows):
        count=sum(int(e[2]) for e in rows);wait=sum(int(e[3]) for e in rows)
        return dict(requests=len(rows),bytes=count,wait_ms=wait,KiBs=count*1000/1024/wait)
    sd_transfer=dict(including_table=throughput(reads),after_table=throughput(reads[1:]))
    frames=[dict(ms=int(t),frame=int(n),phase=p) for t,n,p in re.findall(r'^(\d+) FRAME (\d+) 640x480 phase=(\w+)',log,re.M)]
    assert len(frames)==ft['frames']
    phases={}
    for phase in sorted(set(f['phase'] for f in frames)):
        ff=[f for f in frames if f['phase']==phase];first=ff[0]['frame'];elapsed=ff[-1]['ms']-ff[0]['ms']
        phases[phase]=dict(**ft['scenes'][phase],first_frame=first,last_frame=ff[-1]['frame'],
            entry_ms=ff[0]['ms']-frames[first-2]['ms'] if first>1 else None,
            elapsed_after_first_ms=elapsed,steady_fps=(len(ff)-1)*1000/elapsed if elapsed else None)
    r.update(label=label,data_sha256=sha(BASE/'data/HMM2.DAT'),exact_ft_frames=ft['frames'],
        exact_original=ft['exact_original'],no_visible_overwrites=True,phases=phases,
        sd_requests=len(starts),sd_max_wait_ms=max(int(e[3]) for e in ends),sd_transfer=sd_transfer,
        artifacts={n:sha(out/n) for n in ['frames.ft','esp.log','z80.jsonl','spi.jsonl','ft_check.json','result.json']})
    return r,frames,ft

def main():
    labels={'scene':'fix4_castle_scene','idle':'fix4_castle_idle','ui':'fix4_castle_ui','slow':'fix4_ui_slow'}
    runs={};frames={};checks={}
    for key,label in labels.items():runs[key],frames[key],checks[key]=examine(label)
    assert runs['scene']['exact_original'] and runs['scene']['frames']==370
    refs={hashlib.sha256(expected(BASE/'runs/menu64',int(p.stem.split('_')[1])).tobytes()).hexdigest() for p in (BASE/'runs/menu64').glob('frame_*.idx')}
    for key in ['ui','slow','idle']:
        run=runs[key];out=BASE/'joint'/labels[key]
        matched=sum(hashlib.sha256(expected(out,n).tobytes()).hexdigest() in refs for n in range(1,21))
        assert matched==20;run['menu_rgb_exact']=matched
        if key!='idle':
            assert run['world_ready'] and run['frames']>=245 and run['exit']==-999
            assert (out/'world.be').stat().st_size>10000
            run['world_sha256']=sha(out/'world.be')
    assert runs['slow']['poll_period_ms']==10 and runs['slow']['spi_latency_reads']==3
    idle_rows=[r['link_bytes'] for r in checks['idle']['rows'] if r['frame']>100]
    elapsed=frames['idle'][-1]['ms']-frames['idle'][99]['ms']
    idle=dict(frames=len(idle_rows),elapsed_ms=elapsed,fps=len(idle_rows)*1000/elapsed,
        link_bytes_max=max(idle_rows),link_bytes_mean=sum(idle_rows)/len(idle_rows))
    log=(BASE/'joint'/labels['idle']/'esp.log').read_text()
    intervals=[dict((key,int(value)) for key,value in re.findall(r'(\w+)=(\d+)',line)) for line in log.splitlines() if 'HMM2 PERF' in line]
    assert all(r['SD_B']==r['SD_req']==0 for r in intervals if r['frame']>100)
    idle['intervals']=intervals
    fault=read(BASE/'joint/fix4_bad_seq/result.json')
    assert fault['elf_sha256']==runs['ui']['elf_sha256'] and fault['spg_sha256']==runs['ui']['spg_sha256']
    assert fault['code']==255 and fault['stage']==12 and fault['frames']==0
    assert any('FILE FHEROES2.CFG' in s for s in fault['screen'])
    badlog=(BASE/'joint/fix4_bad_seq/esp.log').read_text()
    assert 'SD reject 74 seq=66' in badlog and 'SD timeout E5/0 fheroes2.cfg S2' in badlog
    result=dict(elf_sha256=runs['ui']['elf_sha256'],spg_sha256=runs['ui']['spg_sha256'],
        data_sha256=runs['ui']['data_sha256'],menu_and_map=True,resolution=[1024,768],
        runs=[runs['ui'],runs['slow']],scene=runs['scene'],idle=idle,idle_run=runs['idle'],negative=fault,
        model_cpu='native x64, not ESP32; scenario game clock virtual, wall frame timestamps real',
        model_link_bytes_s=1330000,model_sd='actual Z80 instructions at 14 MHz; SPI28 DMA; 150 us/sector')
    (EV/'stage1_fix4_joint.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    # Codex: отрицательное свидетельство намеренно не удовлетворяет package_stage1.py.
    sd=read(EV/'stage1_fix4_sd.json')
    sd_min=min([row['KiBs'] for run in sd['results'] for row in run['rows'][1:]]+
        [runs[key]['sd_transfer']['after_table']['KiBs'] for key in ['scene','ui','slow']])
    gate=dict(elf_sha256=result['elf_sha256'],spg_sha256=result['spg_sha256'],data_sha256=result['data_sha256'],
        exact_ft=True,cursor=True,resolution=[1024,768],complete_draw_interception=False,
        performance_pass=False,menu_link_bytes_max=idle['link_bytes_max'],sd_kib_s=sd_min,sd_source='Z80/combined model after cluster table, not board measurement',
        vdac2_comparison={s:{'pass':False,'reason':'Нет измерения одинаковой сцены обоими портами'} for s in ['menu','transitions','scroll','hero','battle','town','load']},
        ready=False,blockers=['CPU fallback for supported FT operations and DL overflow','Unmatched R9 scenes','Hero gain versus old 4.3 fps is only 1.28x'])
    (EV/'stage1_fix4_gate.json').write_text(json.dumps(gate,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(EV/'stage1_fix4_joint.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for label in [*labels.values(),'fix4_bad_seq']:
            out=BASE/'joint'/label
            for name in ['esp.log','z80.jsonl','spi.jsonl','result.json','ft_check.json','world.be','frames.ft']:
                if (out/name).exists():z.write(out/name,label+'/'+name)
            for p in sorted(out.glob('frame_*')):
                if p.suffix in ('.idx','.pal'):z.write(p,label+'/'+p.name)
        z.write(BASE/'launcher/_HMM2.SPG','_HMM2.SPG')
    for key,n in [('idle',200),('ui',245),('scene',20),('scene',200)]:
        shutil.copy2(BASE/'joint'/labels[key]/f'ft_{n:04}.png',EV/f'stage1_fix4_{key}_{n:04}.png')
    print(json.dumps(dict(ready=False,idle=idle,scene=runs['scene']['phases'],archive_bytes=(EV/'stage1_fix4_joint.zip').stat().st_size),indent=2))

if __name__=='__main__':main()
