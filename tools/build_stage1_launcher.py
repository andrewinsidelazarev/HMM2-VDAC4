"""Codex: игровой SPG на основе проверенного DMA-стенда и файлового сервера DOOM."""
from pathlib import Path
import subprocess,shutil,json,hashlib
from prepare_stage1 import ROOT,BASE
from prepare_sd_launcher import REF,clean
# Клод: в SPG — драйвер FAT32 из DOOM VDAC4 (prepare_doom), см. prepare_stage1_fat32.py.
from prepare_stage1_fat32 import prepare_doom as prepare,DOOM_REV as REV
from make_loader_picture import build as make_picture
OUT=BASE/'launcher';SRC=ROOT/'launcher'
def run(cmd,name):
    p=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True);(OUT/name).write_bytes(p.stdout+p.stderr)
    if p.returncode or b'Unrecognized option' in p.stdout+p.stderr:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
def main():
    SRC.mkdir(exist_ok=True);(OUT/'build').mkdir(parents=True,exist_ok=True)
    fat=prepare()
    # Codex: фиксируем входы того же драйвера, который включаем в SPG; WC не нужен.
    names={'SD_ADDRESS','SD_COMMAND','SD_FAILED','SD_BAD_REPLY','SD_WAIT_TOKEN','SD_BUFFER','SD_WAIT_READY'}
    symbols=(fat/'build/port_sdzc.sym').read_text().splitlines()
    (OUT/'sd_symbols.inc').write_text('\n'.join(line for line in symbols if line.split(':')[0] in names)+'\nSD_DATA equ #57\n',encoding='ascii')
    # Codex: игровые исходники — launcher; повторная сборка не возвращает заглушки стенда SD.
    # Клод: картинка экрана загрузки — страницами SPG, как в DOOM VDAC4.
    picture=make_picture(OUT/'build')
    run(['E:/zx/z80/zuma/sjasmplus.exe','--nologo','--inc='+str(SRC),'--inc='+str(OUT/'build'),'--inc='+str(fat/'include'),'--sym=build/hmm2.sym','--lst=build/hmm2.lst',SRC/'hmm2.asm'],'asm.log')
    blocks=[(2,OUT/'build/hmm2_z80.bin'),(14,fat/'build/fat32.bin'),(15,fat/'build/fat32-work.bin')]
    for page,p in blocks[1:]:shutil.copy2(p,OUT/'build'/p.name)
    blocks+=picture
    for name in ['port_sdzc.sym','fat32.sym','manifest.json']:shutil.copy2(fat/'build'/name,OUT/'build'/name)
    (OUT/'build/fat32_revision.txt').write_text(REV,encoding='ascii')
    ini=OUT/'build/hmm2.ini';ini.write_text('Desc = HMM2 VDAC4\nStart = #8000\nStack = #bff0\nResident = #5b00\nPage3 = 32\nClock = 2\nINT = 0\nPager = 0\n'+''.join('Block = #c000, '+str(page)+', build/'+p.name+'\n' for page,p in blocks),encoding='ascii')
    run(['E:/zx/z80/zuma/spgbld.exe','-b',ini,OUT/'_HMM2.SPG','-c','0'],'spg.log')
    p=OUT/'_HMM2.SPG';print(json.dumps(dict(file=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()),indent=2))
if __name__=='__main__':main()
