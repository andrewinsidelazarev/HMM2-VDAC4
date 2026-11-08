"""Codex: сохраняем проверяемый след исполнения именно выданного ELF, без образа прошивки."""
from pathlib import Path
from ft_command_fifo import CommandFifo
import json,hashlib,re,shutil,struct,sys,zipfile,os
from check_fix5_ft import render
from test_ft_trace import png
ROOT=Path(__file__).resolve().parents[1];BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'));FOLDER=BASE/'qemu_stage1'
def main():
    label=sys.argv[1];assert re.fullmatch('[a-z0-9_]+',label)
    snapshots={int(s) for s in sys.argv[2:] if s!='--keys'};out=Path(os.environ.get('HMM2_EVIDENCE',str(ROOT/'Docs/evidence')));out.mkdir(parents=True,exist_ok=True);log=(FOLDER/'qemu.log').read_text(encoding='utf-8')
    if '--keys' in sys.argv:
        # Codex: сохраняем кадр после каждого тестового нажатия для проверки реально открытых окон.
        previous=0
        for line in log.splitlines():
            m=re.search(r'кадр=(\d+)',line)
            if m:previous=int(m[1])
            if 'QEMU: PS2=' in line:snapshots.add(previous+5)
    for failure in ('AUDIO OGG decode error','AUDIO decoder allocation failed','AUDIO PCM open failed','AUDIO unsupported original rate'):
        assert failure not in log,failure
    assert 'QEMU: entry=1' in log and 'QEMU: start=0' in log and 'вход в main OpenHMM2' in log
    assert 'QEMU: захват кадров завершён' in log and 'main завершён, код=1' not in log
    if (FOLDER/'catalogue.bin').exists():
        catalogue_size=(FOLDER/'catalogue.bin').stat().st_size
        assert f'QEMU: catalogue_override bytes={catalogue_size}' in log,'Стенд обязан заменить весь каталог, включая SFX'
    module=(FOLDER/'main/HMM2.ELF').read_bytes();assert module==(BASE/'module/HMM2.ELF').read_bytes()
    ram=bytearray(3*1024*1024);data=(FOLDER/'menu.ft').read_bytes();at=0;dl=bytearray(8192);frames=[];protected=[];high=0
    fifo=CommandFifo();max_words=0;handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    while at<len(data):
        addr,n=struct.unpack_from('<II',data,at);addr&=0x7fffffff;at+=8;raw=data[at:at+n*4];at+=len(raw);assert len(raw)==n*4
        if addr<0x300000:
            assert addr+len(raw)<=len(ram) and not any(addr<end and addr+len(raw)>begin for begin,end in protected)
            ram[addr:addr+len(raw)]=raw;high=max(high,addr+len(raw))
        elif 0x300000<=addr<0x302000:
            offset=addr-0x300000;assert offset+len(raw)<=8192;dl[offset:offset+len(raw)]=raw
        elif addr==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            assert words[-1]==0 and len(words)<=2048;max_words=max(max_words,len(words))
            protected=[];rgb,_=render(words,ram,protected,handle_state=handles);serial=len(frames)+1
            frames.append(dict(frame=serial,rgb_sha256=hashlib.sha256(rgb.tobytes()).hexdigest()))
            if serial in snapshots:png(out/f'stage1_fix5_{label}_{serial:04}.png',rgb.tobytes(),rgb.shape[1],rgb.shape[0])
        elif fifo.write(addr,raw,ram,dl):pass
        elif addr not in (0x302034,0x302048,0x302070):raise AssertionError(hex(addr))
    memory=[tuple(map(int,m)) for m in re.findall(r'кадр=(\d+) SD=(\d+)/(\d+) PSRAM свободно=(\d+) минимум=(\d+)',log)]
    result=dict(label=label,elf_bytes=len(module),elf_sha256=hashlib.sha256(module).hexdigest(),data_sha256=hashlib.sha256((Path((FOLDER/'data_path.bin').read_bytes().rstrip(b'\0').decode()) if (FOLDER/'data_path.bin').exists() else BASE/'data/HMM2.DAT').read_bytes()).hexdigest(),frames=len(frames),max_dl_words=max_words,psram_budget=int(re.search(r'QEMU: PSRAM budget=(\d+)',log)[1]) if 'QEMU: PSRAM budget=' in log else 7864320,minimum_free=min(x[4] for x in memory),last_free=memory[-1][3],sd_requests=memory[-1][1],sd_bytes=memory[-1][2],oom_retries=log.count('QEMU: OOM'),ram_high_water=high,no_visible_overwrites=True,frame_hashes=frames)
    stacks=[tuple(map(int,m)) for m in re.findall(r'STACK=(\d+)/(\d+)',log)]
    limits=re.search(r'внутренняя RAM ограничена free=(\d+) largest=(\d+)',log)
    tasks=re.findall(r'task_start stack=(\d+) ok=(\d+) internal_before=(\d+) internal_after=(\d+) largest=(\d+)',log)
    assert stacks and limits and len(tasks) in (1,2,3) and all(int(t[1])==1 for t in tasks)
    free,largest=map(int,limits.groups());allocated=int(tasks[0][0]);watermark=min(n for n,total in stacks)
    assert allocated<=16384 and watermark>=2048 and all(total==allocated for _,total in stacks)
    assert 49000<=free<=49843 and 24000<=largest<=24576
    config=(FOLDER/'build/config/sdkconfig.h').read_text()
    assert '#define CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY 1' in config
    assert '#define CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK 1' in config
    result['stack']=dict(tasks=len(tasks),allocated_bytes=allocated,minimum_free_bytes=watermark,maximum_used_bytes=allocated-watermark,
        internal_before=free,largest_before=largest,internal_after=int(tasks[0][3]),internal_allocation=free-int(tasks[0][3]),canary=True,watchpoint=True)
    audio_tasks=re.findall(r'task_start stack=(\d+) ok=1 .*?priority=5 core=0',log)
    graphic_tasks=re.findall(r'task_start stack=(\d+) ok=1 .*?priority=3 core=0',log)
    if audio_tasks:
        audio_free=[int(x) for x in re.findall(r'AUDIO_FREE=(\d+)/4096',log)]
        assert int(audio_tasks[0])<=16384 and audio_free and min(audio_free)>=1024
        pcm=re.findall(r'QEMU: PCM frames=(\d+) nonzero=(\d+) peak=([.0-9]+)',log)
        assert pcm and int(pcm[-1][1])>0 and 0<float(pcm[-1][2])<=1
        result['audio']=dict(stack_bytes=int(audio_tasks[0]),minimum_free_stack=min(audio_free),pcm_frames=int(pcm[-1][0]),nonzero_samples=int(pcm[-1][1]),peak=float(pcm[-1][2]))
        gaps=re.findall(r'QEMU: PCM .* gaps=(\d+) max_gap_samples=(\d+)',log)
        if gaps:result['audio'].update(pcm_gaps=int(gaps[-1][0]),max_gap_samples=int(gaps[-1][1]))
        underruns=re.findall(r'AUDIO underrun count=(\d+)',log)
        if underruns:result['audio']['underrun_count']=int(underruns[-1])

    if graphic_tasks:
        assert audio_tasks,"Рабочая задача рендера не должна вытеснить задачу PCM"
        assert "#define CONFIG_FREERTOS_HZ 1000" in config
        workers=[tuple(map(int,m)) for m in re.findall(r'WORKER_FREE=(\d+)/(\d+)',log)]
        assert workers and all(total==3072 for _,total in workers) and min(n for n,_ in workers)>=1024
        assert int(graphic_tasks[0])==3072
        assert 'priority=3 core=0' in log and 'priority=5 core=0' in log
        result['graphic_worker']=dict(stack_bytes=3072,minimum_free_stack=min(n for n,_ in workers),core=0,priority=3,pcm_priority=5)
    filex=re.search(r'QEMU: FILEX read=(\d+) write=(\d+)',log)
    if filex:result['filex']=dict(read_bytes=int(filex[1]),write_bytes=int(filex[2]))
    (out/f'stage1_fix5_qemu_{label}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    shutil.copy2(FOLDER/'qemu.log',out/f'stage1_fix5_qemu_{label}.log')
    with zipfile.ZipFile(out/f'stage1_fix5_qemu_{label}.zip','w',zipfile.ZIP_DEFLATED) as z:
        z.write(FOLDER/'initial.cfg' if (FOLDER/'initial.cfg').exists() else ROOT/'main/fheroes2.cfg','initial.cfg')
        for name in ['menu.ft','sd.log','command.json','catalogue.bin','ui_keys.bin','ui_times.bin','ui_test.bin','internal_limit.bin','psram_budget.bin','data_path.bin','sd_latency.bin']:
            if (FOLDER/name).exists():z.write(FOLDER/name,name)
    print(json.dumps({k:v for k,v in result.items() if k!='frame_hashes'},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
