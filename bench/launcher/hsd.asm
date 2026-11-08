; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.
; SPDX-License-Identifier: GPL-2.0-or-later




        DEVICE ZXSPECTRUM128
        DEFINE ZIFI_SPI
        IFNDEF HSD_NO_DMA
        DEFINE ZIFI_SPI_DMA
        ENDIF
        INCLUDE "fat32.inc"
        INCLUDE "filex.inc"
        ORG #8000
Start:
        di
        ld sp,#BFF0
        ld bc,#21AF
        ld a,#0E
        out (c),a
        ld bc,#10AF
        ld a,#0F
        out (c),a
        inc b
        ld a,#0E
        out (c),a
        ld bc,#13AF
        ld a,#20
        out (c),a
        xor a
        out (#FE),a
        ld a,1
        call BootMark
        call ZiFi_Init
        jp c,Fail

        ld a,2
        call BootMark
        ld a,CMD_APP_STOP
        call Proto_SendEmpty
        ld a,RESP_APP_STOP
        ld b,32
        call WaitLong
        jp c,Fail

        call FtScreen_Start
        ld a,3
        call BootMark
        ld hl,#3900
        xor a
        call FAT32_BIND
        call FAT32_DEVICE_INIT
        jp nz,Fail
        ld a,4
        call BootMark
        call FAT32_MOUNT
        jp nz,Fail
        ld hl,FT_AT_MOUNT
        call FtProgress
        ld a,5
        call BootMark
        call PathCfgLoad
        jr nz,.located
        ld a,4
        ld (FtGap),a
        call LocateGame
        push af
        ld a,2
        ld (FtGap),a
        pop af
        jp c,Fail
        call PathCfgSave
.located:
        ld hl,FT_AT_GAME
        call FtProgress
        ld a,6
        call BootMark
        ld hl,ElfSpec
        call FAT32_FIND
        jp c,FileFindError
        jp z,Fail
        ld (ElfSize),hl
        ld (ElfSize+2),de
        ld (Query+1),hl
        ld (Query+3),de



        ld hl,#3868+22
        ld de,Query+5
        ld bc,4
        ldir
        call FastOpen
        jr z,.mapped
        ld hl,MsgReadError
        ld (BootMessage),hl
        jp Fail
.mapped:
        ld hl,Query
        ld a,7
        call BootMark
        ld bc,QueryEnd-Query
        ld a,CMD_MOD_QUERY
        call Proto_Send
        ld a,RESP_MOD_QUERY
        ld b,16
        call WaitLong
        jp c,Fail
        ld hl,FT_AT_QUERY
        call FtProgress
        ld a,(ProtoBuf)
        or a
        jp z,Uploaded
        cp 1
        jp nz,Fail
Upload:
        ld a,8
        call BootMark

        ld hl,(ElfSize)
        ld de,(Offset)
        or a
        sbc hl,de
        push hl
        ld hl,(ElfSize+2)
        ld de,(Offset+2)
        sbc hl,de
        ld a,h
        or l
        pop hl
        ld bc,1016
        jr nz,.length
        ld a,h
        or l
        jr z,Commit
        push hl
        or a
        sbc hl,bc
        pop hl
        jr nc,.length
        ld b,h
        ld c,l
.length:
        ld (Chunk),bc
        ld (Fx+10),bc
        ld hl,(Offset)
        ld (Fx+4),hl
        ld (PutBuf),hl
        ld hl,(Offset+2)
        ld (Fx+6),hl
        ld (PutBuf+2),hl
        ld hl,PutBuf+4
        ld (Fx+8),hl
        call ReadAt
        jp nz,Fail
        ld hl,PutBuf
        ld bc,(Chunk)
        inc bc
        inc bc
        inc bc
        inc bc
        ld a,CMD_MOD_PUT
        call Proto_Send
        ld a,RESP_MOD_PUT
        ld b,16
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld hl,(Offset)
        ld bc,(Chunk)
        add hl,bc
        ld (Offset),hl
        ld hl,(Offset+2)
        ld bc,0
        adc hl,bc
        ld (Offset+2),hl
        call FtUpload
        jp Upload
Commit:
        ld a,9
        call BootMark
        ld a,CMD_MOD_COMMIT
        call Proto_SendEmpty
        ld a,RESP_MOD_COMMIT
        ld b,64
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld hl,FT_AT_COMMIT
        call FtProgress
Uploaded:

        call ChooseWad
        jr nz,.wad
        ld hl,MsgMissingWad
        ld (BootMessage),hl
        ld a,#F1
        jp Fail
.wad:
        ld a,10
        call BootMark

        call FtHandover

        ld bc,#00AF
        ld a,4
        out (c),a
        ld hl,ElfSpec+1
        ld bc,8
        ld a,CMD_APP_START
        call Proto_Send
        ld a,RESP_APP_START
        ld b,32
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        or a
        jr z,.app_started
        ld hl,MsgStart
        cp #FE
        jr nz,.start_error
        ld hl,MsgEspMemory
.start_error:
        ld (BootMessage),hl
        jp Fail
.app_started:
        ld a,1
        ld (AppStarted),a
        ld a,11
        call BootMark
        ld hl,WadConfig
        ld bc,(ConfigLength)
        ld a,#70
        call Proto_Send
        ld a,#F0
        ld b,8
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld a,12
        call BootMark
Run:
        call Proto_Poll
        jr nc,.keys
        cp #E4
        call z,ServeRead
        cp #E5
        ; Codex: стенд только читает файл.
        cp #F3
        jr nz,.keys
        ld a,(ProtoBuf)
        cp #FF
        jp z,AppFailed
        cp 3
        jp z,Stop
.keys:
        ld hl,KeyDivider
        dec (hl)
        jr nz,Run
        call SendKeys
        jp Run

AppFailed:
        call CaptureAppError
        ld a,#FF
        jp Fail









PS2_DRAIN       equ 24
SendKeys:
        ld hl,KeyBuf
        in a,(#1F)
        cp #FF
        jr nz,.kempston
        xor a
.kempston:
        and 31
        ld (hl),a





        inc hl
        ld bc,#FADF
        in a,(c)
        ld (hl),a
        inc hl
        inc b
        in a,(c)
        ld (hl),a
        inc hl
        ld b,#FF
        in a,(c)
        ld (hl),a
        inc hl
        call Ps2Open
        ld e,PS2_DRAIN
.drain:
        in a,(c)
        or a
        jr z,.send
        ld (hl),a
        inc hl
        dec e
        jr nz,.drain
.send:
        ld de,KeyBuf
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ld hl,KeyBuf
        ld a,#75
        call Proto_Send


        ld hl,StatusDivider
        dec (hl)
        ret nz
        ld (hl),32
        ld a,#73
        jp Proto_SendEmpty



Ps2Open:
        ld bc,#EFF7
        ld a,#80
        out (c),a
        ld a,(Ps2Ready)
        or a
        jr nz,.queue
        inc a
        ld (Ps2Ready),a
        ld b,#DF
        ld a,#0C
        out (c),a
        ld b,#BF
        ld a,1
        out (c),a
        ld b,#DF
        ld a,#F0
        out (c),a
        ld b,#BF
        ld a,2
        out (c),a
.queue:
        ld b,#DF
        ld a,#F0
        out (c),a
        ld b,#BF
        ret


ServeRead:
        ld hl,(ProtoRxLen)
        ld de,8
        or a
        sbc hl,de
        jp nz,Fail
        ld hl,(ProtoBuf)
        ld (PutBuf),hl
        ld hl,(ProtoBuf+2)
        ld (Fx+4),hl
        ld hl,(ProtoBuf+4)
        ld (Fx+6),hl
        ld hl,(ProtoBuf+6)
        ld (Fx+10),hl
        ld (Chunk),hl
        ld de,1017
        or a
        sbc hl,de
        jp nc,Fail
        ld hl,PutBuf+4
        ld (Fx+8),hl


        ld a,(WadSelected)
        or a
        jr nz,.read
        ld hl,WadFind
        call FAT32_FIND
        jr c,.bad
        jr z,.bad
        call FastOpen
        jr nz,.bad
        ld a,1
        ld (WadSelected),a
.read:
        call ReadAt
        jr z,.ok
.bad:
        ld hl,0
        jr .answer
.ok:
        ld hl,(Chunk)
.answer:
        ld (PutBuf+2),hl
        ld b,h
        ld c,l
        inc bc
        inc bc
        inc bc
        inc bc
        ld hl,PutBuf
        ld a,#71
        call Proto_Send
        xor a
        ret

ReadAt:
        ld a,(FastEnabled)
        or a
        jp nz,FastReadAt
        ld hl,Fx
        call FAT32_FILEX
        ret nz
        ld hl,(Fx+24)
        ld de,(Chunk)
        or a
        sbc hl,de
        ret

FindWad:
        push hl
        call FAT32_FIND
        pop bc
        jp c,FileFindError
        ret z
        ld (WadConfig),hl
        ld (WadConfig+2),de
        inc bc
        ld h,b
        ld l,c
        ld de,WadConfig+4
        ld bc,4
.copy:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        inc bc
        or a
        jr nz,.copy
        ld (ConfigLength),bc
        ld hl,WadConfig+4
        ld de,WadFind+1
        ld bc,13
        ldir
        ld a,1
        or a
        ret

FileFindError:
        ld hl,MsgFileRead
        ld (BootMessage),hl
        jp Fail

Stop:
        ld a,CMD_APP_STOP
        call Proto_SendEmpty
        ld a,RESP_APP_STOP
        ld b,64
        call WaitLong
        xor a
        jr Exit
Fail:
        ld (BootError),a
        call BootTrace
        ld a,(AppStarted)
        or a
        jr z,.border
        ld a,CMD_APP_STOP
        call Proto_SendEmpty
        ld a,RESP_APP_STOP
        ld b,64
        call WaitLong
.border:
        call ShowBootError
        ld a,2
Exit:
        out (#FE),a
        ld bc,#00AF
        xor a
        out (c),a
Halt:
        jr Halt
WaitLong:
        ld (WaitWanted),a
.again:
        push bc
        ld a,(WaitWanted)
        ld de,#FFFF
        call Proto_WaitCmd
        pop bc
        ret nc
        djnz .again
        scf
        ret

WC_PAGE2: db 2
WC_PAGE3: db #20
GameDir: dd 0
WadFind: db 0
        ds 13
ElfSpec: db 0,"HSD.ELF",0
Wad2: db 0,"HEROES2.AGG",0
WadFull: db 0,"HEROES2.AGG",0
WadShare: db 0,"HEROES2.AGG",0
Query: db 0
        ds 8
        db "HSD.ELF",0
QueryEnd:
WadConfig: ds 17
ConfigLength: dw 0
ElfSize: dd 0
Offset: dd 0
Chunk: dw 0
WaitWanted: db 0
BootStage: db 0
BootError: db 0
AppStarted: db 0
WadSelected: db 0
KeyDivider: db 0
StatusDivider: db 32
Ps2Ready: db 0
KeyBuf: ds 4+PS2_DRAIN
Fx: db 32,1,1,0
        ds 28
PutBuf: ds 1020
        INCLUDE "bench_stubs.asm"
        INCLUDE "directory.asm"
        INCLUDE "pathcfg.asm"
        INCLUDE "fastread.asm"




        INCLUDE "zifi_link.asm"
        INCLUDE "proto.asm"
CodeEnd:
        ASSERT CodeEnd < #B800
        SAVEBIN "build/hsd_z80.bin",Start,CodeEnd-Start
