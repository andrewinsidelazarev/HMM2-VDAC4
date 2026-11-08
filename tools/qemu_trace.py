"""Клод: трасса исполнения QEMU для модели платы.

Сцена стенда QEMU (-icount) идёт до заданной строки журнала ESP. Затем через монитор включается
журнал QEMU exec,nochain,in_asm: каждый исполненный блок трансляции и дизассемблер каждого нового
блока. После заданного объёма журнал выключается, QEMU закрывается. Разбор — tools/qemu_trace_report.py.

Пример: python tools/qemu_trace.py gatehell --start "HMM2 GPU: frame=300 " --mb 600
"""
from pathlib import Path
import argparse,json,os,shutil,socket,struct,subprocess,sys,time,re
ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))/'qemu_stage1'
SCENES={'menu':['menu'],'gatehell':['GateHell.mp2','--extended'],'town':['KINGDTH.MX2','--town'],'battle':['battle'],'load':['load'],'week':['sevenl.mp2','--week'],'deep':['KINGDTH.MX2','--deep-town'],'alteris':['alteris.mp2','--extended']}

def monitor(port,command):
    s=socket.create_connection(('127.0.0.1',port),timeout=10);buf=b''
    def prompt():
        nonlocal buf
        while b'(qemu)' not in buf:
            d=s.recv(65536)
            if not d:raise EOFError
            buf+=d
        i=buf.index(b'(qemu)');t=buf[:i];buf=buf[i+6:];return t
    prompt();s.sendall(command.encode()+b'\n')
    try:prompt()
    except (EOFError,OSError):pass
    s.close()

# Codex: точная граница полного Redraw после прогрева. GDB включает журнал до первой команды функции.
def break_full(out,raw,loaded,port,data_bias,needle=None):
    nm=Path('E:/zx/esp/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')
    gdb=Path('E:/zx/esp/.espressif/tools/xtensa-esp-elf-gdb/17.1_20260402/xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb.exe')
    table=subprocess.check_output([str(nm),'-C','--defined-only',str(raw)],text=True,errors='replace')
    items=[line.split(None,2) for line in table.splitlines()];items=[(int(v[0],16),v[2]) for v in items if len(v)==3]
    entry=struct.unpack_from('<I',(out/'HMM2.ELF').read_bytes(),24)[0];delta=loaded-entry
    def unique(needle):
        matches=[(name,address+delta) for address,name in items if needle in name]
        # Клод: у функции с лямбдами совпадают и они; сама функция — самое короткое имя.
        if not matches:raise RuntimeError(('Нет символа',needle))
        return min(matches,key=lambda m:len(m[0]))[1]
    # Codex: код исполняется через IROM, переменные лежат в другом отображении PSRAM.
    address=unique(needle or 'Interface::GameArea::Redraw(');animation=unique('map_animation_only')-data_bias;frame=unique('hmm_frames')-data_bias
    # Клод: --break-at — трасса с первого входа в указанную функцию (например, ход ИИ), без условия на кадр.
    condition='' if needle else f'condition 1 (*(unsigned char*)0x{animation:x} == 0 && *(unsigned int*)0x{frame:x} > 150)\n'
    commands=f"""set pagination off
set confirm off
set remotetimeout 30
target remote 127.0.0.1:{port}
break *0x{address:x}
{condition}continue
printf "HMM2_TRACE_FULL pc=%p frame=%u animation=%u\\n", $pc, *(unsigned int*)0x{frame:x}, *(unsigned char*)0x{animation:x}
delete breakpoints
monitor log exec,nochain,in_asm
detach
quit
"""
    script=out/'full_redraw.gdb';script.write_text(commands,encoding='utf-8')
    (out/'breakpoint.json').write_text(json.dumps(dict(function=hex(address),animation_flag=hex(animation),frame_counter=hex(frame),minimum_frame=151,data_alias_bias=data_bias),indent=2))
    log=(out/'gdb.log').open('wb')
    process=subprocess.Popen([str(gdb),'-nx','-nh','--batch','-x',str(script)],stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    return process,log

def main():
    ap=argparse.ArgumentParser();ap.add_argument('scene',choices=list(SCENES))
    ap.add_argument('--start',default='HMM2 GPU: frame=300 ');ap.add_argument('--mb',type=int,default=600)
    ap.add_argument('--out',default='E:/zx/cache/tmp/hmm2_esp/trace');ap.add_argument('--port',type=int,default=45454)
    ap.add_argument('--input-dir',default='');ap.add_argument('--symbols');ap.add_argument('--frames',type=int);ap.add_argument('--steady',action='store_true');ap.add_argument('--mouse',choices=['cursor','scroll'])
    ap.add_argument('--full-redraw',action='store_true');ap.add_argument('--break-at');ap.add_argument('--on',type=float,default=0);ap.add_argument('--off',type=float,default=0);ap.add_argument('--stop',default='');ap.add_argument('--gdb-port',type=int,default=45455)
    a=ap.parse_args()
    out=Path(a.out)/a.scene;out.mkdir(parents=True,exist_ok=True)
    # Codex: сохраняем именно встроенный в стенд ELF и его символы до запуска,
    # чтобы параллельная сборка приложения не подменила метаданные завершённой трассы.
    raw=Path(a.symbols) if a.symbols else BASE.parent/'module/HMM2.raw.ELF'
    if not a.symbols and (BASE/'main/HMM2.ELF').read_bytes()!=(BASE.parent/'module/HMM2.ELF').read_bytes():
        raise RuntimeError('Стенд и модуль различаются: пересоберите стенд или укажите --symbols от его ELF')
    if (BASE/'main/HMM2.ELF').read_bytes() not in (BASE/'build/hmm2_runtime_qemu.elf').read_bytes():
        raise RuntimeError('Сохранённый модуль не встроен в стенд: проверьте пересборку embedded ELF')
    shutil.copy2(BASE/'main/HMM2.ELF',out/'HMM2.ELF')
    shutil.copy2(raw,out/'HMM2.raw.ELF')
    shutil.copy2(BASE/'build/hmm2_runtime_qemu.elf',out/'hmm2_runtime_qemu.elf')
    trace=out/'trace.log'
    if trace.exists():trace.unlink()
    subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),*SCENES[a.scene]],cwd=ROOT,check=True)
    # Codex: фиксированный вид карты после запуска, без чередования диалогов по таймеру.
    # Семь Enter запускают GateHell и закрывают его исходные вступительные сообщения.
    # Одинаковый сценарий нужен для сравнения сборок с различной скоростью.
    if a.steady and a.scene=='gatehell':
        keys=(BASE/'ui_keys.bin').read_bytes();n=keys.index(0x5a)+7
        (BASE/'ui_keys.bin').write_bytes(keys[:n]);(BASE/'ui_times.bin').write_bytes((BASE/'ui_times.bin').read_bytes()[:n*4])
    if a.mouse:(BASE/'ui_mouse.bin').write_bytes(struct.pack('<II',1 if a.mouse=='cursor' else 2,50000))
    if a.mouse=='scroll':
        # Codex: исходный Settings читает отсутствующий ключ как 0 (прокрутка выключена).
        # В тестовой конфигурации задаётся штатная NORMAL: 8 px за шаг; игра не патчится.
        cfg=BASE/'run/fheroes2.cfg';cfg.write_text(cfg.read_text(encoding='utf-8')+'\nscroll speed = 2\n',encoding='utf-8')
        shutil.copy2(cfg,BASE/'initial.cfg')
    if a.frames:
        (BASE/'ui_test.bin').write_bytes(struct.pack('<I',a.frames))
        if a.scene=='menu' and not a.input_dir:(BASE/'ui_keys.bin').write_bytes(b'')
    # Codex: исходные нажатия для измерения переходов меню тем же игровым ELF.
    if a.input_dir:
        for name in ['ui_keys.bin','ui_times.bin','ui_mouse.bin']:
            source=Path(a.input_dir)/name
            if source.exists():shutil.copy2(source,BASE/name)
    (out/'options.json').write_text(json.dumps(vars(a),ensure_ascii=False,indent=2),encoding='utf-8')
    for name in ['ui_keys.bin','ui_times.bin','ui_test.bin','ui_mouse.bin','initial.cfg','psram_budget.bin','catalogue.bin']:
        if (BASE/name).exists():shutil.copy2(BASE/name,out/name)
    # Клод: журнал прошлого прогона содержит ту же строку-метку — без удаления трасса включилась бы с загрузки.
    (BASE/'qemu.log').unlink(missing_ok=True)
    env=dict(os.environ,HMM2_QEMU_MONITOR=str(a.port),HMM2_QEMU_LOGFILE=str(trace))
    if a.full_redraw or a.break_at:env['HMM2_QEMU_GDB']=str(a.gdb_port)
    log=(out/'run.log').open('wb')
    p=subprocess.Popen([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),'--icount','--timeout','3600'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    qlog=BASE/'qemu.log';marker=a.start.encode();started=False;t0=time.monotonic();debugger=debugger_log=None;window_on=True;window_at=time.monotonic();start_size=None
    try:
        while p.poll() is None:
            time.sleep(.01 if not started or a.on else .5)
            if not started:
                if a.full_redraw or a.break_at:
                    if debugger is None and qlog.exists():
                        entries=re.findall(rb'elf->entry=0x([0-9a-f]+)',qlog.read_bytes())
                        biases=re.findall(rb'FDE=\d+[^\r\n]*=(-?\d+)',qlog.read_bytes())
                        if entries and biases:debugger,debugger_log=break_full(out,raw,int(entries[0],16),a.gdb_port,int(biases[0]),a.break_at)
                    elif debugger is not None and debugger.poll() is not None:
                        debugger_log.flush();result=(out/'gdb.log').read_text(encoding='utf-8',errors='replace')
                        if debugger.returncode or 'HMM2_TRACE_FULL' not in result:raise RuntimeError(result[-5000:])
                        started=True;t0=time.monotonic();print('trace on: full Redraw',flush=True)
                elif qlog.exists() and marker in qlog.read_bytes():
                    monitor(a.port,'log exec,nochain,in_asm');started=True;t0=time.monotonic();print('trace on',flush=True)
            elif a.on and time.monotonic()-window_at>=(a.on if window_on else a.off):
                # Клод: окна равномерно по длинному участку (ход ИИ): 1 мс виртуальных часов — 1 млн команд, а при включённом журнале QEMU в тысячи раз медленнее.
                # Дизассемблер остаётся включённым, чтобы блоки из выключенных участков были известны разбору.
                monitor(a.port,'log in_asm' if window_on else 'log exec,nochain,in_asm');window_on=not window_on;window_at=time.monotonic()
                if a.stop and qlog.exists():
                    data=qlog.read_bytes()
                    if start_size is None:start_size=len(data)
                    elif a.stop.encode() in data[start_size:]:
                        monitor(a.port,'log none');print('trace off at stop',round(time.monotonic()-t0),'s',flush=True)
                        time.sleep(1);monitor(a.port,'quit');break
            if started and trace.exists() and trace.stat().st_size>=a.mb<<20:
                monitor(a.port,'log none');print('trace off after',round(time.monotonic()-t0),'s',flush=True)
                time.sleep(1);monitor(a.port,'quit');break
    finally:
        if debugger is not None:
            if debugger.poll() is None:debugger.terminate()
            debugger.wait(timeout=5);debugger_log.close()
        if p.poll() is None:
            try:monitor(a.port,'log none');monitor(a.port,'quit')
            except (EOFError,OSError):pass
        try:p.wait(timeout=30)
        except subprocess.TimeoutExpired:p.kill()
        log.close()
    (out/'qemu.log').write_bytes(qlog.read_bytes())
    # Codex: снимок команд и SD нужен для проверки, какая сцена действительно измерена.
    for name in ['menu.ft','sd.log']:
        if (BASE/name).exists():shutil.copy2(BASE/name,out/name)
    print('trace',trace,trace.stat().st_size if trace.exists() else 0)
    return 0 if started else 1

if __name__=='__main__':raise SystemExit(main())
