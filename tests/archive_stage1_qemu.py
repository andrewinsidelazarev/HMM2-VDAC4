"""Codex: сохраняем проверяемый след исполнения именно выданного ELF, без образа прошивки."""
from pathlib import Path
import json,hashlib,re,shutil,struct,sys,zipfile
from check_fix4_ft import render
from test_ft_trace import png
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1');FOLDER=BASE/'qemu_stage1'
def main():
    label=sys.argv[1];assert re.fullmatch('[a-z0-9_]+',label)
    snapshots={int(s) for s in sys.argv[2:] if s!='--keys'};out=ROOT/'Docs/evidence';log=(FOLDER/'qemu.log').read_text(encoding='utf-8')
    if '--keys' in sys.argv:
        # Codex: сохраняем кадр после каждого тестового нажатия для проверки реально открытых окон.
        previous=0
        for line in log.splitlines():
            m=re.search(r'кадр=(\d+)',line)
            if m:previous=int(m[1])
            if 'QEMU: PS2=' in line:snapshots.add(previous+5)
    assert 'QEMU: entry=1' in log and 'QEMU: start=0' in log and 'вход в main OpenHMM2' in log
    assert 'QEMU: захват кадров завершён' in log and 'main завершён, код=1' not in log
    module=(FOLDER/'main/HMM2.ELF').read_bytes();assert module==(BASE/'module/HMM2.ELF').read_bytes()
    ram=bytearray(3*1024*1024);data=(FOLDER/'menu.ft').read_bytes();at=0;dl=bytearray(8192);frames=[];protected=[];high=0
    while at<len(data):
        addr,n=struct.unpack_from('<II',data,at);at+=8;raw=data[at:at+n*4];at+=len(raw);assert len(raw)==n*4
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
            rgb,protected=render(words,ram);serial=len(frames)+1
            frames.append(dict(frame=serial,rgb_sha256=hashlib.sha256(rgb.tobytes()).hexdigest()))
            if serial in snapshots:png(out/f'stage1_{label}_{serial:04}.png',rgb.tobytes(),1024,768)
    memory=[tuple(map(int,m)) for m in re.findall(r'кадр=(\d+) SD=(\d+)/(\d+) PSRAM свободно=(\d+) минимум=(\d+)',log)]
    result=dict(label=label,elf_bytes=len(module),elf_sha256=hashlib.sha256(module).hexdigest(),data_sha256=hashlib.sha256((BASE/'data/HMM2.DAT').read_bytes()).hexdigest(),frames=len(frames),psram_budget=7864320,minimum_free=min(x[4] for x in memory),last_free=memory[-1][3],sd_requests=memory[-1][1],sd_bytes=memory[-1][2],oom_retries=log.count('QEMU: OOM'),ram_high_water=high,no_visible_overwrites=True,frame_hashes=frames)
    stacks=[tuple(map(int,m)) for m in re.findall(r'STACK=(\d+)/(\d+)',log)]
    limits=re.search(r'внутренняя RAM ограничена free=(\d+) largest=(\d+)',log)
    tasks=re.findall(r'task_start stack=(\d+) ok=(\d+) internal_before=(\d+) internal_after=(\d+) largest=(\d+)',log)
    assert stacks and limits and len(tasks)==1 and int(tasks[0][1])==1
    free,largest=map(int,limits.groups());allocated=int(tasks[0][0]);watermark=min(n for n,total in stacks)
    assert allocated<=16384 and watermark>=2048 and all(total==allocated for _,total in stacks)
    assert 49000<=free<=49843 and 24000<=largest<=24576
    config=(FOLDER/'build/config/sdkconfig.h').read_text()
    assert '#define CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY 1' in config
    assert '#define CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK 1' in config
    result['stack']=dict(tasks=1,allocated_bytes=allocated,minimum_free_bytes=watermark,maximum_used_bytes=allocated-watermark,
        internal_before=free,largest_before=largest,internal_after=int(tasks[0][3]),internal_allocation=free-int(tasks[0][3]),canary=True,watchpoint=True)
    filex=re.search(r'QEMU: FILEX read=(\d+) write=(\d+)',log)
    if filex:result['filex']=dict(read_bytes=int(filex[1]),write_bytes=int(filex[2]))
    (out/f'stage1_qemu_{label}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    shutil.copy2(FOLDER/'qemu.log',out/f'stage1_qemu_{label}.log')
    with zipfile.ZipFile(out/f'stage1_qemu_{label}.zip','w',zipfile.ZIP_DEFLATED) as z:
        z.write(FOLDER/'initial.cfg' if (FOLDER/'initial.cfg').exists() else ROOT/'main/fheroes2.cfg','initial.cfg')
        for name in ['menu.ft','sd.log','command.json','catalogue.bin','ui_keys.bin','ui_times.bin','ui_test.bin','internal_limit.bin']:
            if (FOLDER/name).exists():z.write(FOLDER/name,name)
    print(json.dumps({k:v for k,v in result.items() if k!='frame_hashes'},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
