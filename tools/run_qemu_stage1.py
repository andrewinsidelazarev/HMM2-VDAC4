"""Codex: QEMU исполняет ELF игры; semihosting заменяет только накопитель и FT812 стенда."""
from pathlib import Path
import argparse,json,subprocess,sys,time,os
ROOT=Path(__file__).resolve().parents[1]
OUT=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))/'qemu_stage1'
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--icount',action='store_true');ap.add_argument('--timeout',type=int,default=600);ap.add_argument('--exit-test',action='store_true');a=ap.parse_args()
    args=json.loads((OUT/'build/flasher_args.json').read_text());files=[]
    for offset,name in args['flash_files'].items():files += [offset,str(OUT/'build'/name)]
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','merge-bin','--output',str(OUT/'flash.bin'),'--pad-to-size','8MB',*files],check=True,cwd=OUT)
    # Codex: исключаем случайный прогон старой прошивки после подмены встроенного ELF.
    assert (OUT/'main/HMM2.ELF').read_bytes() in (OUT/'flash.bin').read_bytes(),'В стенд встроен другой HMM2.ELF: пересоберите его'
    # Codex: доступная локальная установка той же версии QEMU задаётся без изменения SDK.
    qemu=Path(os.environ.get('HMM2_QEMU_EXE','E:/zx/esp/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_20250817/qemu/bin/qemu-system-xtensa.exe'))
    cmd=[str(qemu),'-machine','esp32s3','-nographic','-m','8M','-drive','file='+str(OUT/'flash.bin')+',if=mtd,format=raw','-no-reboot','-semihosting-config','enable=on,target=native']
    # Клод: HMM2_QEMU_SHIFT=2 — такт команды 4 нс (≈250 млн команд/с): процессор платы с PSRAM, а не 1 ГГц, чтобы тормоза платы были видны на стенде.
    if a.icount:cmd+=['-icount','shift='+os.environ.get('HMM2_QEMU_SHIFT','0')+',align=off,sleep=off']
    # Клод: HMM2_QEMU_MONITOR=порт — монитор QEMU по TCP для выборки PC (профиль кода игры).
    if os.environ.get('HMM2_QEMU_MONITOR'):cmd+=['-monitor','tcp:127.0.0.1:'+os.environ['HMM2_QEMU_MONITOR']+',server,nowait']
    # Клод: HMM2_QEMU_LOGFILE — файл журнала QEMU (-D); сам журнал включает tools/qemu_trace.py через монитор.
    if os.environ.get('HMM2_QEMU_LOGFILE'):cmd+=['-D',os.environ['HMM2_QEMU_LOGFILE']]
    if os.environ.get('HMM2_QEMU_GDB'):cmd+=['-gdb','tcp:127.0.0.1:'+os.environ['HMM2_QEMU_GDB']]
    (OUT/'command.json').write_text(json.dumps(cmd,ensure_ascii=False,indent=2),encoding='utf-8')
    with (OUT/'qemu.log').open('wb') as log:
        p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,cwd=OUT);limit=time.monotonic()+a.timeout;success=False
        # Codex: PID относится только к этой стадии; остановка диагностики не затронет соседний стенд.
        (OUT/'qemu.pid').write_text(str(p.pid),encoding='ascii')
        while p.poll() is None and time.monotonic()<limit:
            time.sleep(.5);output=(OUT/'qemu.log').read_bytes()
            marker=b'QEMU: exit verified' if a.exit_test else 'QEMU: захват кадров завершён'.encode()
            if marker in output:success=True;break
            if b'QEMU: relocate=-' in output or b'QEMU: OGG relocate=-' in output or b'Guru Meditation' in output or b'CPU halted' in output or b'abort() was called' in output or b'QEMU: start=-' in output or b'QEMU: exit FAILED' in output or (not a.exit_test and 'main завершён, код='.encode() in output) or 'аварийное завершение runtime'.encode() in output:break
        if p.poll() is None:p.terminate()
        p.wait(timeout=5)
        (OUT/'qemu_exit.json').write_text(json.dumps({'returncode':p.returncode,'capture_complete':success}),encoding='utf-8')
    text=(OUT/'qemu.log').read_text(encoding='utf-8',errors='replace');print(text[-12000:])
    return 0 if success else 1
if __name__=='__main__':raise SystemExit(main())
