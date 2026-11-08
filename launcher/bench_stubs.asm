; Codex: тест пишет результат в журнал ESP; загрузочный экран игры здесь не измеряется.
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
