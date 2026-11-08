"""Клод: разбор трассы tools/qemu_trace.py — модель платы по командам и кэшу команд ESP32-S3.

Журнал QEMU exec,nochain,in_asm даёт каждый исполненный блок трансляции и его команды.
- Команды считаются по дизассемблеру блока (in_asm).
- Кэш команд — как в прошивке VDAC4 (sdkconfig): 16 КиБ, 8 путей, строка 32 байта, LRU.
  Через него идут только адреса 0x42000000–0x43FFFFFF (флэш и PSRAM); IRAM и ПЗУ — мимо кэша.
- Кадр — вход в hmm_gpu_frame.
- Включительная стоимость функции: от входа (блок с её начала) до возврата (блок, кончающийся retw/ret).

Пример: python tools/qemu_trace_report.py E:/zx/cache/tmp/hmm2_esp/trace/gatehell
"""
from pathlib import Path
import bisect,collections,json,re,struct,subprocess,sys
NM=Path('E:/zx/esp/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')
RUNTIME=Path('E:/zx/cache/tmp/hmm2_esp/stage1/qemu_stage1/build/hmm2_runtime_qemu.elf')
LINE=32;SETS=16384//LINE//8;WAYS=8

def symbols(elf,delta):
    out=subprocess.run([str(NM),'-C','-S','--defined-only',str(elf)],capture_output=True,text=True,errors='replace').stdout
    syms=[]
    for line in out.splitlines():
        p=line.split(' ',3)
        if len(p)==4 and p[2] in 'tTwW':
            try:syms.append((int(p[0],16)+delta,int(p[1],16),p[3]))
            except ValueError:pass
    syms.sort();return syms

def main():
    folder=Path(sys.argv[1]);elf=folder/'HMM2.ELF'
    entry=struct.unpack_from('<I',elf.read_bytes(),24)[0]
    qlog=(folder/'qemu.log').read_text(encoding='utf-8',errors='replace')
    # Codex: первым загружается HMM2, затем OGG; адрес второго ELF не относится к символам игры.
    loaded=int(re.findall(r'elf->entry=0x([0-9a-f]+)',qlog)[0],16)
    # Клод: HMM2.ELF модуля без символов (strip); символы — из HMM2.raw.ELF той же сборки.
    runtime=folder/'hmm2_runtime_qemu.elf'
    syms=symbols(folder/'HMM2.raw.ELF',loaded-entry)+symbols(runtime if runtime.exists() else RUNTIME,0);syms.sort()
    starts=[s[0] for s in syms]
    def name(pc):
        i=bisect.bisect_right(starts,pc)-1
        if i>=0 and pc<syms[i][0]+max(syms[i][1],1):return syms[i][2]
        return '<0x%08x>'%(pc&~0xfff)
    func_start={s[0]:s[2] for s in syms}
    frame_set={s[0] for s in syms if s[2].startswith('hmm_gpu_frame')}
    tbs={};cur=None
    # Codex: по ESP32-S3 datasheet 4.1.2.3 I-кэш общий для двух ядер.
    cache=[[] for _ in range(SETS)]
    flat_i=collections.Counter();flat_m=collections.Counter();name_cache={};rom_callers=collections.Counter()
    incl_i=collections.Counter();incl_m=collections.Counter();calls=collections.Counter()
    # Codex: потоки двух ядер имеют разные стеки; I-кэш остаётся общим.
    cpus={};per_cpu=[]
    total_i=total_m=0;frames=[];per=[];tb_count=0;scopes=[];lifecycle=[];pipelines=[]
    def fname(pc):
        n=name_cache.get(pc)
        if n is None:n=name_cache[pc]=name(pc)
        return n
    rx_exec=re.compile(r'^Trace (\d+): (?:0x)?[0-9a-f]+ \[[0-9a-f]+/([0-9a-f]+)/')
    with (folder/'trace.log').open('r',encoding='utf-8',errors='replace') as f:
        for line in f:
            if line.startswith('Trace'):
                m=rx_exec.match(line)
                if not m:continue
                cpu=int(m.group(1));pc=int(m.group(2),16);tb=tbs.get(pc)
                if tb is None:continue
                cs=cpus.setdefault(cpu,dict(stack=[],onstack=collections.Counter(),push=None,pop=None,instructions=0,misses=0,scope_counts={}))
                stack,onstack,pending_push,pending_pop=cs['stack'],cs['onstack'],cs['push'],cs['pop']
                cpu_i,cpu_m=cs['instructions'],cs['misses']
                tb_count+=1;instrs,lines,ret,is_entry,last_pc,vector=tb
                if pc in frame_set:frames.append((total_i,total_m));per.append(collections.Counter());per_cpu.append(collections.defaultdict(collections.Counter))
                n=fname(pc)
                # Клод: исключения окна регистров (переполнение на entry, опустошение на retw) повторяют
                # команду после обработчика. Вход и возврат засчитываются по следующему блоку вне векторов:
                # если это повтор той же команды — ждём дальше.
                if not vector:
                    if pending_push and pc!=pending_push[0]:
                        _,pn,pi,pm=pending_push;stack.append((pn,pi,pm));onstack[pn]+=1;calls[pn]+=1;pending_push=None
                        if len(lifecycle)<2000 and '::{lambda' not in pn and any(k in pn for k in ('hmm_gpu_offer(', 'hmm_gpu_pixels(', 'hmm_gpu_move(', 'map_materialize(', 'map_cache_clear(', 'ImageRestorer::update(', 'ApplyPalette(', 'FadeDisplay(', 'hmm_gpu_map_view(')):
                            lifecycle.append(dict(function=pn,frame=len(frames),cpu=cpu,parents=[v[0] for v in stack[-9:-1]]))
                    if pending_pop and pc!=pending_pop[0]:
                        fn=pending_pop[1];pending_pop=None
                        # Возврат снимает свою функцию и всё выше неё; чужой (другая задача FreeRTOS) — пропуск.
                        if onstack[fn]:
                            while stack:
                                top,i0,m0=stack.pop();onstack[top]-=1
                                if not onstack[top]:
                                    incl_i[top]+=cpu_i-i0;incl_m[top]+=cpu_m-m0
                                    if top.startswith('hmm_gpu_frame') or 'GameArea::Redraw(' in top or ('ft_hash_job(' in top or top.startswith('(anonymous namespace)::hmm_render_job_')):
                                        counts=cs['scope_counts'].pop((top,i0),collections.Counter())
                                        raw_scope=cpu_i-i0
                                        excluded_scope=sum(c for n,c in counts.items() if 'prvTaskCheckFreeStackSpace' in n or 'uxTaskGetStackHighWaterMark' in n)
                                        harness_scope=counts.get('<QEMU frame telemetry>',0)
                                        scopes.append(dict(function=top,frame=len(frames),render_frame=len(frames)+(1 if 'GameArea::Redraw(' in top else 0),cpu=cpu,instructions_raw=raw_scope,excluded_stack=excluded_scope,excluded_harness=harness_scope,instructions=raw_scope-excluded_scope-harness_scope,estimated_ms_15cycles=(raw_scope-excluded_scope-harness_scope)/16000,icache_misses=cpu_m-m0,functions=dict(counts.most_common())))
                                if top==fn:break
                            if fn.startswith('hmm_gpu_frame') and cs.get('pipeline') is not None:
                                pipeline=cs.pop('pipeline');raw=cpu_i-pipeline['start'];counts=pipeline['counts']
                                excluded=sum(c for n,c in counts.items() if 'prvTaskCheckFreeStackSpace' in n or 'uxTaskGetStackHighWaterMark' in n)
                                harness=counts.get('<QEMU frame telemetry>',0)
                                pipelines.append(dict(frame=len(frames),cpu=cpu,start_function=pipeline['function'],instructions_raw=raw,excluded_stack=excluded,excluded_harness=harness,instructions=raw-excluded-harness,estimated_ms_15cycles=(raw-excluded-harness)/16000,icache_misses=cpu_m-pipeline['misses'],functions=dict(counts.most_common())))
                    if pc in func_start and is_entry and not pending_push:
                        pending_push=(pc,n,cpu_i,cpu_m)
                        # Codex: весь путь Redraw/палитра до возврата GPU, включая подготовку между функциями.
                        if cs.get('pipeline') is None and (n.startswith('hmm_gpu_frame') or 'GameArea::Redraw(' in n or n.startswith('fheroes2::ApplyPalette(')):
                            cs['pipeline']=dict(start=cpu_i,misses=cpu_m,function=n,counts=collections.Counter())
                        if n.startswith('hmm_gpu_frame') or 'GameArea::Redraw(' in n or ('ft_hash_job(' in n or n.startswith('(anonymous namespace)::hmm_render_job_')):cs['scope_counts'].setdefault((n,cpu_i),collections.Counter())
                miss=0
                for ln in lines:
                    s=cache[ln%SETS]
                    if ln in s:s.remove(ln);s.append(ln)
                    else:
                        miss+=1;s.append(ln)
                        if len(s)>WAYS:del s[0]
                # Codex: новые строки о worker не являются работой игры. Отделяем
                # только измерение/печать из SDK ft_write, сохраняя обычный игровой log.
                counted=n
                if onstack.get('ft_write') and (onstack.get('logline') or onstack.get('vTaskGetInfo') or n in ('logline','vTaskGetInfo')):
                    counted='<QEMU frame telemetry>'
                total_i+=instrs;total_m+=miss;flat_i[counted]+=instrs;flat_m[counted]+=miss
                # Клод: libgcc и memcpy лежат в ПЗУ без символов — считаем их на вызывающую функцию, чтобы видеть, кто тратит.
                if 0x40000000<=pc<0x40060000 and stack:rom_callers[stack[-1][0]]+=instrs
                for counts in cs['scope_counts'].values():counts[counted]+=instrs
                if cs.get('pipeline') is not None:cs['pipeline']['counts'][counted]+=instrs
                if per:per[-1][counted]+=instrs;per_cpu[-1][cpu][counted]+=instrs
                if ret and not vector:pending_pop=(last_pc,fname(last_pc))
                cs.update(push=pending_push,pop=pending_pop,instructions=cpu_i+instrs,misses=cpu_m+miss)
            elif line.startswith('IN:'):cur=[]
            elif cur is not None and line.startswith('0x'):
                a,_,text=line.partition(':');cur.append((int(a,16),text.strip()))
            elif cur is not None and cur and not line.strip():
                addrs=[a for a,_ in cur];last=cur[-1][1].split()[0] if cur[-1][1] else ''
                size=2 if last.endswith('.n') else 3;end=addrs[-1]+size
                cached=0x42000000<=addrs[0]<0x44000000
                lines=[]
                if cached:
                    for a in addrs+[end-1]:
                        ln=a//LINE
                        if not lines or lines[-1]!=ln:lines.append(ln)
                first=cur[0][1].split()[0] if cur[0][1] else ''
                # Векторы окна регистров кончаются rfwo/rfwu: такие блоки не входят в учёт вызовов.
                tbs[addrs[0]]=(len(addrs),lines,last in ('retw','retw.n'),first=='entry',addrs[-1],last in ('rfwo','rfwu'))
                cur=None
            elif cur is not None and line.startswith('---'):pass
    # Codex: работа ядра0 не исчезает из оценки после распараллеливания.
    worker_scopes=[r for r in scopes if r['cpu']==0 and ('ft_hash_job(' in r['function'] or r['function'].startswith('(anonymous namespace)::hmm_render_job_'))]
    (folder/'worker.json').write_text(json.dumps(worker_scopes,ensure_ascii=False,indent=2),encoding='utf-8')
    parallel=[]
    for row in pipelines:
        worker=sum(r['instructions'] for r in worker_scopes if r['frame']==row['frame'])
        total=row['instructions']+worker
        parallel.append(dict(frame=row['frame'],main_core_instructions=row['instructions'],worker_core_instructions=worker,total_render_instructions=total,estimated_ms_lower=max(row['instructions'],worker)/16000,estimated_ms_upper=total/16000))
    (folder/'parallel_pipeline.json').write_text(json.dumps(parallel,indent=2),encoding='utf-8')
    (folder/'pipeline.json').write_text(json.dumps(pipelines,ensure_ascii=False,indent=2),encoding='utf-8')
    (folder/'lifecycle.json').write_text(json.dumps(lifecycle,ensure_ascii=False,indent=2),encoding='utf-8')
    (folder/'flat.json').write_text(json.dumps(dict(instructions=flat_i,calls=calls),ensure_ascii=False,indent=2),encoding='utf-8')
    (folder/'scopes.json').write_text(json.dumps(scopes,ensure_ascii=False,indent=2),encoding='utf-8')
    # Codex: Redraw относится к следующему вызову hmm_gpu_frame, а не к предыдущему интервалу.
    pairs=[]
    for frame in sorted({row['render_frame'] for row in scopes}):
        selected=[row for row in scopes if row['render_frame']==frame]
        engine=[row for row in selected if 'GameArea::Redraw(' in row['function']]
        gpu=[row for row in selected if row['function'].startswith('hmm_gpu_frame')]
        if engine and gpu:
            instructions=sum(row['instructions'] for row in engine+gpu)
            pairs.append(dict(frame=frame,engine=sum(row['instructions'] for row in engine),gpu=sum(row['instructions'] for row in gpu),instructions=instructions,estimated_ms_15cycles=instructions/16000))
    (folder/'render_frames.json').write_text(json.dumps(pairs,indent=2),encoding='utf-8')

    print('блоков',tb_count,'команд',total_i,'промахов I-кэша',total_m,'кадров',len(frames))
    if len(frames)>=2:
        di=frames[-1][0]-frames[0][0];dm=frames[-1][1]-frames[0][1];k=len(frames)-1
        print('на кадр: команд %.0f, промахов %.0f (кадров %d)'%(di/k,dm/k,k))
    # Клод: команды и промахи каждого кадра — кадры окна бывают разные (карта, окно героя).
    for k in range(1,len(frames)):
        print('кадр %d: команд %d, промахов %d'%(k,frames[k][0]-frames[k-1][0],frames[k][1]-frames[k-1][1]))
        for n,c in per[k-1].most_common(12):print('    %9d  %s'%(c,n[:110]))
    # Codex: сохраняем сырой счёт и отдельно модель без проверки стека тестового стенда.
    rows=[]
    for k in range(1,len(frames)):
        counts=per[k-1]
        raw=frames[k][0]-frames[k-1][0]
        excluded=sum(c for n,c in counts.items() if 'prvTaskCheckFreeStackSpace' in n or 'uxTaskGetStackHighWaterMark' in n)
        harness=counts.get('<QEMU frame telemetry>',0)
        core_counts={}
        for cpu,functions in per_cpu[k-1].items():
            core_raw=sum(functions.values());core_excluded=sum(c for n,c in functions.items() if 'prvTaskCheckFreeStackSpace' in n or 'uxTaskGetStackHighWaterMark' in n)
            core_harness=functions.get('<QEMU frame telemetry>',0)
            core_counts[str(cpu)]=dict(instructions_raw=core_raw,excluded_stack=core_excluded,excluded_harness=core_harness,instructions=core_raw-core_excluded-core_harness)
        assert sum(v['instructions_raw'] for v in core_counts.values())==raw
        rows.append(dict(frame=k,per_core=core_counts,instructions_raw=raw,instructions=raw-excluded-harness,excluded_stack=excluded,excluded_harness=harness,
                         icache_misses=frames[k][1]-frames[k-1][1],estimated_ms_15cycles=(raw-excluded-harness)/16000,
                         functions=dict(counts.most_common())))
    (folder/'frames.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    print('\nПлоский профиль (команды, промахи I-кэша):')
    for n,c in flat_i.most_common(40):print('%9d %5.1f%% %8d  %s'%(c,100*c/total_i,flat_m[n],n[:120]))
    # Клод: два ядра работают параллельно: скорость игры ограничивает ядро 1 — смотреть его отдельно.
    for cpu in sorted({c for frame in per_cpu for c in frame}):
        counts=collections.Counter()
        for frame in per_cpu:counts.update(frame.get(cpu,{}))
        print('\nЯдро %d, команд %d:'%(cpu,sum(counts.values())))
        for n,c in counts.most_common(45):print('%9d %5.1f%%  %s'%(c,100*c/max(sum(counts.values()),1),n[:110]))
    print('\nПЗУ (memcpy, libgcc) по вызывающим:')
    for n,c in rom_callers.most_common(30):print('%9d %5.1f%%  %s'%(c,100*c/total_i,n[:120]))
    print('\nПлоский профиль по промахам:')
    for n,c in flat_m.most_common(25):print('%8d %5.1f%% %9d  %s'%(c,100*c/max(total_m,1),flat_i[n],n[:120]))
    print('\nВключительно (команды, промахи, вызовы):')
    for n,c in incl_i.most_common(70):print('%9d %5.1f%% %8d %6d  %s'%(c,100*c/total_i,incl_m[n],calls[n],n[:120]))

if __name__=='__main__':raise SystemExit(main())
