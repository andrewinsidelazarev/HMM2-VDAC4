; Codex: шрифт ошибок и пассивный след DOOM VDAC4; см. THIRD_PARTY.md.
; SPDX-License-Identifier: GPL-2.0-or-later






BootMark:
        ld (BootStage),a
        push af
        push bc
        push de
        push hl
        call FtLoading
        pop hl
        pop de
        pop bc
        pop af
BootTrace:
        push af
        ld a,#02
        out (#77),a
        ld a,#07
        out (#77),a
        ld a,#4F
        out (#57),a
        ld a,(BootStage)
        out (#57),a
        ld a,(BootError)
        out (#57),a
        ld a,#03
        out (#77),a
        pop af
        ret

ShowBootError:

        ld bc,#02AF
        xor a
.regs:
        out (c),a
        inc b
        bit 3,b
        jr z,.regs

        ld hl,DebugPalettePorts
.palette:
        ld a,(hl)
        cp #FF
        jr z,.palette_end
        ld b,a
        ld c,#AF
        inc hl
        ld a,(hl)
        out (c),a
        inc hl
        jr .palette
.palette_end:
        ld bc,#13AF
        ld a,#20
        out (c),a
        ld hl,#C000
        ld de,#C001
        ld bc,#17FF
        ld (hl),0
        ldir
        ld hl,#D800
        ld de,#D801
        ld bc,767
        ld (hl),#47
        ldir
        ld hl,MsgFailed
        ld de,#C802
        call PrintText
        ld hl,(BootMessage)
        ld a,h
        or l
        jr nz,.message
        ld a,(BootStage)
        dec a
        cp 12
        jr c,.stage
        xor a
.stage:
        add a,a
        ld l,a
        ld h,0
        ld de,StageMessages
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
.message:
        ld de,#C842
        call PrintWrapped
        ld hl,MsgStage
        ld de,#C8A2
        call PrintText
        ld a,(BootStage)
        ld de,#C8A8
        call HexByte
        ld hl,MsgCode
        ld de,#C8AB
        call PrintText
        ld a,(BootError)
        ld de,#C8B0
        call HexByte
        ld a,(LastRequestValid)
        or a
        jr z,.reset
        ld hl,LastRequestLine
        ld de,#C8E2
        call PrintText
        ld hl,LastFileLine
        ld de,#C902
        call PrintText
        ld hl,LastRangeLine
        ld de,#C922
        call PrintText
.reset:
        ld hl,MsgReset
        ld de,#C962
        call PrintText
        ld bc,#01AF
        ld a,#20
        out (c),a
        ret
DebugPalettePorts:
        db #1A,low DebugPalette,#1B,(high DebugPalette)&#3F,#1C,2
        db #1D,0,#1E,0,#1F,0,#26,15,#28,0,#27,#84,#FF
        ALIGN 2
DebugPalette:
        dw 0,#0018,#6000,#6018,#0300,#0318,#6300,#6318
        dw 0,#001F,#7C00,#7C1F,#03E0,#03FF,#7FE0,#7FFF
BootMessage: dw 0
StageMessages:
        dw MsgEsp,MsgStop,MsgSd,MsgFat,MsgReadError,MsgMissingElf
        dw MsgCache,MsgUpload,MsgCommit,MsgStart,MsgConfig,MsgRuntime
MsgFailed: db "HMM2: START FAILED",0
MsgEsp: db "ESP32 NOT RESPONDING",0
MsgStop: db "CANNOT STOP PREVIOUS APP",0
MsgSd: db "SD CARD INIT FAILED",0
MsgFat: db "FAT32 MOUNT FAILED",0
MsgMissingElf: db "FILE NOT FOUND: HMM2.ELF",0
MsgMissingWad: db "FILE NOT FOUND: HMM2.DAT",0
MsgReadError: db "SD DIRECTORY READ FAILED",0
MsgFileRead: db "SD FILE LOOKUP FAILED",0
MsgFileReply: db "SD RESPONSE TRANSFER FAILED",0
MsgSearchLimit: db "DIRECTORY SEARCH LIMIT REACHED",0
MsgDepthLimit: db "DIRECTORY NESTING TOO DEEP",0
MsgCacheFull: db "MODULE CACHE FULL",0
MsgQueryInvalid: db "INVALID MODULE QUERY",0
MsgCache: db "ESP MODULE CACHE ERROR",0
MsgUpload: db "ELF READ OR TRANSFER FAILED",0
MsgCommit: db "ESP COULD NOT STORE ELF",0
MsgStart: db "ESP COULD NOT START HMM2",0
MsgEspMemory: db "ESP32: NOT ENOUGH MEMORY",0
MsgConfig: db "HMM2 INIT FAILED",0
MsgRuntime: db "HMM2 STOPPED WITH AN ERROR",0
MsgStage: db "STAGE ",0
MsgCode: db "CODE ",0
MsgReset: db "RESET TO EXIT",0




CaptureAppError:
        ld hl,(ProtoRxLen)
        ld de,6
        or a
        sbc hl,de
        ret c
        ret z
        ld a,h
        or a
        jr nz,.maximum
        ld a,l
        cp 91
        jr c,.length
.maximum:
        ld a,90
.length:
        ld b,a
        ld hl,ProtoBuf+6
        ld de,AppErrorText
.copy:
        ld a,(hl)
        or a
        jr z,.end
        cp 32
        jr c,.space
        cp 127
        jr c,.put
.space:
        ld a,' '
.put:
        ld (de),a
        inc hl
        inc de
        djnz .copy
.end:
        xor a
        ld (de),a
        ld a,(AppErrorText)
        or a
        ret z
        ld hl,AppErrorText
        ld (BootMessage),hl
        ret
AppErrorText: ds 91

; Codex: последний обслуживаемый запрос остаётся после порчи ProtoBuf новым
; ответом. Смещение, длина и seq на экране — шестнадцатеричные числа.
RememberRequest:
        push af
        ld hl,LastCommand
        call AsciiHex
        ld a,1
        ld (LastRequestValid),a
        ld hl,LastSeq
        ld de,ProtoBuf+1
        ld b,2
        call RequestHex
        pop af
        cp #E5
        jr z,.file
        ld a,1
        ld hl,LastOp
        call AsciiHex
        ld hl,WadFind+1
        ld de,LastFile
        ld bc,13
        ldir
        ld de,ProtoBuf+5
        ld hl,LastOffset
        ld b,4
        call RequestHex
        ld de,ProtoBuf+7
        jr .length
.file:
        ld a,(ProtoBuf+2)
        ld hl,LastOp
        call AsciiHex
        ld hl,ProtoBuf+3
        ld de,LastFile
        ld bc,13
        ldir
        ld de,ProtoBuf+32
        ld hl,LastOffset
        ld b,4
        call RequestHex
        ld de,ProtoBuf+34
.length:
        ld hl,LastLength
        ld b,2
RequestHex:
        ld a,(de)
        dec de
        call AsciiHex
        inc hl
        djnz RequestHex
        ret
LastRequestValid: db 0
LastRequestLine: db "LAST "
LastCommand: db "00 OP "
LastOp: db "00 SEQ "
LastSeq: db "0000",0
LastFileLine: db "FILE "
LastFile: ds 13
        db 0
LastRangeLine: db "OFF "
LastOffset: db "00000000 LEN "
LastLength: db "0000",0

PrintWrapped:
        ld b,30
.char:
        ld a,(hl)
        inc hl
        or a
        ret z
        push bc
        push hl
        call PrintChar
        pop hl
        pop bc
        inc e
        djnz .char
        inc e
        inc e
        ld b,30
        jr .char


PrintText:
        ld a,(hl)
        inc hl
        or a
        ret z
        push hl
        call PrintChar
        pop hl
        inc e
        jr PrintText
PrintChar:
        cp 'a'
        jr c,.upper
        cp 'z'+1
        jr nc,.upper
        and #DF
.upper:
        cp ' '
        ret z
        cp '-'
        jr z,.minus
        cp ':'
        jr z,.colon
        cp '0'
        jr c,.punct
        cp '9'+1
        jr nc,.letter
        sub '0'
        jp HexDigit
.letter:
        sub 'A'
        cp 26
        ret nc
        jr .glyph
.punct:
        ld a,26
        jr .glyph
.colon:
        ld a,27
        jr .glyph
.minus:
        ld a,28
.glyph:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,LetterFont
        add hl,bc
        jr DrawGlyph
HexByte:
        push af
        rrca
        rrca
        rrca
        rrca
        call HexDigit
        inc e
        pop af
HexDigit:
        and 15
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,HexFont
        add hl,bc
DrawGlyph:
        push de
        ld b,8
.row:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz .row
        pop de
        ret
LetterFont:
        db #18,#3C,#66,#66,#7E,#66,#66,0
        db #7C,#66,#66,#7C,#66,#66,#7C,0
        db #3C,#66,#60,#60,#60,#66,#3C,0
        db #78,#6C,#66,#66,#66,#6C,#78,0
        db #7E,#60,#60,#7C,#60,#60,#7E,0
        db #7E,#60,#60,#7C,#60,#60,#60,0
        db #3C,#66,#60,#6E,#66,#66,#3C,0
        db #66,#66,#66,#7E,#66,#66,#66,0
        db #7E,#18,#18,#18,#18,#18,#7E,0
        db #1E,#0C,#0C,#0C,#6C,#6C,#38,0
        db #66,#6C,#78,#70,#78,#6C,#66,0
        db #60,#60,#60,#60,#60,#60,#7E,0
        db #63,#77,#7F,#6B,#63,#63,#63,0
        db #66,#76,#7E,#7E,#6E,#66,#66,0
        db #3C,#66,#66,#66,#66,#66,#3C,0
        db #7C,#66,#66,#7C,#60,#60,#60,0
        db #3C,#66,#66,#66,#6E,#3C,#0E,0
        db #7C,#66,#66,#7C,#78,#6C,#66,0
        db #3C,#66,#60,#3C,#06,#66,#3C,0
        db #7E,#18,#18,#18,#18,#18,#18,0
        db #66,#66,#66,#66,#66,#66,#3C,0
        db #66,#66,#66,#66,#66,#3C,#18,0
        db #63,#63,#63,#6B,#7F,#77,#63,0
        db #66,#66,#3C,#18,#3C,#66,#66,0
        db #66,#66,#66,#3C,#18,#18,#18,0
        db #7E,#06,#0C,#18,#30,#60,#7E,0
        db 0,0,0,0,0,#18,#18,0
        db 0,#18,#18,0,#18,#18,0,0
        db 0,0,0,#7E,0,0,0,0
HexFont:
        db #3C,#66,#6E,#76,#66,#66,#3C,0
        db #18,#38,#18,#18,#18,#18,#7E,0
        db #3C,#66,6,#0C,#30,#60,#7E,0
        db #3C,#66,6,#1C,6,#66,#3C,0
        db #0C,#1C,#3C,#6C,#7E,#0C,#0C,0
        db #7E,#60,#7C,6,6,#66,#3C,0
        db #3C,#60,#60,#7C,#66,#66,#3C,0
        db #7E,6,#0C,#18,#30,#30,#30,0
        db #3C,#66,#66,#3C,#66,#66,#3C,0
        db #3C,#66,#66,#3E,6,#0C,#38,0
        db #18,#3C,#66,#66,#7E,#66,#66,0
        db #7C,#66,#66,#7C,#66,#66,#7C,0
        db #3C,#66,#60,#60,#60,#66,#3C,0
        db #78,#6C,#66,#66,#66,#6C,#78,0
        db #7E,#60,#60,#7C,#60,#60,#7E,0
        db #7E,#60,#60,#7C,#60,#60,#60,0
