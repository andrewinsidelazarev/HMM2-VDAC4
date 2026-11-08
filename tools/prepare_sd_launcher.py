"""Codex: переносим проверенный загрузчик DOOM в отдельный стенд; исходный DOOM только читаем."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
REF=Path('E:/zx/DOOM VDAC4')
OUT=ROOT/'bench/launcher'

def clean(s):
    # Codex: в перенесённых файлах сохраняем код; новые пояснения здесь на русском.
    notices=[line for line in s.splitlines() if any(k in line for k in ['SPDX-License-Identifier','Copyright','Andrew Lazarev','Andrew Lazar'])]
    return '\n'.join(notices)+'\n'+'\n'.join(line.split(';')[0].rstrip() for line in s.splitlines())+'\n'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    for name in ['doom.asm','directory.asm','pathcfg.asm','zifi_link.asm','zifi_spi.asm','proto.asm']:
        s=clean((REF/'launcher'/name).read_text(encoding='utf-8'))
        s=s.replace('DOOM.ELF','HSD.ELF').replace('DOOM    ELF','HSD     ELF')
        s=s.replace('"DOOM"','"HSD"')
        for n in ['DOOM2.WAD','DOOM.WAD','DOOM1.WAD']:s=s.replace(n,'HEROES2.AGG')
        for n in ['DOOM2   WAD','DOOM    WAD','DOOM1   WAD']:s=s.replace(n,'HEROES2 AGG')
        # Codex: SFN HEROES2 занимает семь букв и один пробел перед расширением.
        s=s.replace('HEROES2 AGG','HEROES2 AGG')
        if name=='doom.asm':
            s=s.replace('DEFINE ZIFI_SPI','DEFINE ZIFI_SPI\n        IFNDEF HSD_NO_DMA\n        DEFINE ZIFI_SPI_DMA\n        ENDIF')
            s=s.replace('        ld bc,9\n        ld a,CMD_APP_START','        ld bc,8\n        ld a,CMD_APP_START')
            s=s.replace('        call z,ServeFile','        ; Codex: стенд только читает файл.')
            for inc in ['files.asm','diagnostic.asm','wadmenu.asm','ftscreen.asm']:s=s.replace('        INCLUDE "'+inc+'"','')
            s=s.replace('        INCLUDE "directory.asm"','        INCLUDE "bench_stubs.asm"\n        INCLUDE "directory.asm"')
            s=s.replace('"build/doom_z80.bin"','"build/hsd_z80.bin"')
            s=s.replace('        ASSERT CodeEnd < #BB00','        ASSERT CodeEnd < #B800')
            # Codex: окно C000 постоянно содержит страницу 32; DMA получает фактические страницы SPG.
            s=s.replace('GameDir: dd 0','WC_PAGE2: db 2\nWC_PAGE3: db #20\nGameDir: dd 0')
            name='hsd.asm'
        (OUT/name).write_text('; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.\n'+s,encoding='utf-8')
    stubs='''; Codex: тест пишет результат в журнал ESP; загрузочный экран игры здесь не измеряется.
ChooseWad:
        ld hl,WadFull
        jp FindWad
BootMark:
        ld (BootStage),a
        out (#FE),a
        ret
FtScreen_Start:
FtProgress:
FtUpload:
FtHandover:
FtTick:
BootTrace:
ShowBootError:
CaptureAppError:
        ret
FT_AT_MOUNT equ 0
FT_AT_GAME equ 0
FT_AT_QUERY equ 0
FT_AT_COMMIT equ 0
FtGap: db 0
BootMessage: dw 0
MsgReadError:
MsgMissingWad:
MsgStart:
MsgEspMemory:
MsgFileRead:
MsgMissingElf:
MsgDepthLimit:
MsgSearchLimit:
        db 0
'''
    (OUT/'bench_stubs.asm').write_text(stubs,encoding='utf-8')
if __name__=='__main__':main()
