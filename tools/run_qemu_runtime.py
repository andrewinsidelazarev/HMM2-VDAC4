"""Codex: запуск стенда только в QEMU; образ flash — обычный файл в разрешённом tmp."""
from pathlib import Path
import json,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/qemu_runtime')
def main():
    args=json.loads((OUT/'build/flasher_args.json').read_text())
    files=[]
    for offset,name in args['flash_files'].items():files += [offset,str(OUT/'build'/name)]
    cmd=[sys.executable,'-m','esptool','--chip','esp32s3','merge-bin','--output',str(OUT/'flash.bin'),'--fill-flash-size','8MB',*files]
    subprocess.run(cmd,check=True,cwd=OUT)
    qemu=Path('E:/zx/esp/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_20250817/qemu/bin/qemu-system-xtensa.exe')
    cmd=[str(qemu),'-machine','esp32s3','-nographic','-m','8M','-drive','file='+str(OUT/'flash.bin')+',if=mtd,format=raw','-no-reboot']
    (OUT/'command.json').write_text(json.dumps(cmd,ensure_ascii=False,indent=2),encoding='utf-8')
    with (OUT/'qemu.log').open('wb') as log:
        p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,cwd=OUT)
        limit=time.monotonic()+45;success=False
        while p.poll() is None and time.monotonic()<limit:
            time.sleep(.2)
            output=(OUT/'qemu.log').read_bytes()
            if 'QEMU: завершено'.encode() in output:success=True;break
            if b'Guru Meditation' in output or b'CPU halted' in output:break
        if p.poll() is None:p.terminate()
        p.wait(timeout=5)
    text=(OUT/'qemu.log').read_text(encoding='utf-8',errors='replace');print(text[-10000:])
    (ROOT/'Docs/evidence/runtime_qemu.log').write_text(text,encoding='utf-8')
    return 0 if success and 'QEMU: start=0' in text and 'контрольного объекта=1' in text else 1
if __name__=='__main__':raise SystemExit(main())
