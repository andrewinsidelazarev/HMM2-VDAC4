"""Codex: игровой SPG исполняется на Z80-стенде с настоящим FAT32 и DMA."""
from pathlib import Path
import shutil,sys
sys.path.insert(0,'E:/zx/esp/fat32-venv/Lib/site-packages')
ROOT=Path(__file__).resolve().parents[1]
OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage1/launcher')
shutil.copy2(OUT/'build/hmm2_z80.bin',OUT/'build/hmm2.bin')
text=(ROOT/'tests/test_sd_launcher.py').read_text(encoding='utf-8')
text=text.replace('stage0b/sd_bench','stage1/launcher').replace('HSD.ELF','HMM2.ELF').replace('HEROES2.AGG','HMM2.DAT')
text=text.replace("E:/zx/HMM2/Assets/Original/DATA/HMM2.DAT","E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.DAT")
text=text.replace("elf=(OUT/'HMM2.ELF').read_bytes()","elf=Path('E:/zx/cache/tmp/hmm2_esp/stage1/module/HMM2.ELF').read_bytes()")
text=text.replace("'hsd_dma' if dma else 'hsd_plain'","'hmm2'")
text=text.replace('[(False,False),(True,False),(True,True)]','[(True,False),(True,True)]')
text=text.replace('sd_launcher_test.json','stage1_launcher_test.json')
text=text.replace("REF/'third_party/fat32/build'/n","OUT/'build'/n")
text=text.replace('def main():','from fix4_z80 import patched_machine\nbase.Machine=patched_machine(base)\n\ndef main():')
exec(compile(text,__file__,'exec'),{'__file__':str(ROOT/'tests/test_sd_launcher.py'),'__name__':'__main__'})
