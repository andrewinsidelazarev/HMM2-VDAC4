; Codex: сообщения об ошибке своим шрифтом, без ожидания шрифтов ESP и без буферов SD.
; Codex: новая сцена и полная оригинальная полоса находятся в одном JPEG 1024×768.
; Codex: затемняем оставшуюся часть; общие координаты SPG/ELF создаёт make_loader_picture.py.
; SPDX-License-Identifier: GPL-2.0-or-later
        INCLUDE "ft_spi.asm"
        INCLUDE "ldrpic.inc"
FT_PIC equ #080000
FILL_X0 equ LOADER_X
FT_FILL_W equ LOADER_FILL
FT_SPIN equ 60000
        MACRO DL_V x, y
        dd #40000000|(((x)*16)<<15)|((y)*16)
        ENDM
ChooseWad:
        ld hl,WadFull
        jp FindWad
FtScreen_Start:
        xor a
        ld (FtLive),a
        ld bc,#00AF
        ld a,4
        out (c),a
        ld bc,#6800
        call FtCmd
        ld bc,#4400
        call FtCmd
        ld bc,#4100
        call FtCmd
        ld bc,#61C8
        call FtCmd
        ld bc,0
        call FtCmd
        ld hl,300
.wait:
        push hl
        ld bc,5000
        call FtDelay
        ld a,#30
        ld de,#2000
        call FtRd32
        pop hl
        ld a,(FtBuf)
        cp #7C
        jr z,.ready
        dec hl
        ld a,h
        or l
        jr nz,.wait
        xor a
        ld (FtOk),a
        ret
.ready:
        ld hl,FtModeTab
.mode:
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld a,d
        or e
        jr z,.font
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        push hl
        ld h,b
        ld l,c
        call FtWrReg
        pop hl
        jr .mode
.font:
        xor a
        ld de,0
        ld hl,LetterFont
        ld bc,29*8+16*8
        call FtWrite
        ld a,1
        ld (FtOk),a
        jp FtLoading
; Клод: цель полосы (точек); меньше прежней не ставится. Портит всё, как FtTick.
FtProgress:
        ld de,(FtCap)
        or a
        sbc hl,de
        add hl,de
        jr c,FtTick
        ld (FtCap),hl
FtUpload:
FtTick:
        ld a,(FtLive)
        or a
        ret z
        ld a,(BootError)
        or a
        ret nz
        ld a,FT_REG_HI
        ld de,#2004
        call FtRd32
        ld hl,(FtBuf)
        ld de,(FtLast)
        or a
        sbc hl,de
        ld a,h
        or a
        jr nz,.step
        ld a,l
        cp 8
        ret c
.step:
        ld hl,(FtBuf)
        ld (FtLast),hl
        ld hl,(FtCap)
        ld de,(FtShown)
        or a
        sbc hl,de
        ret c
        ret z
        ld de,31
        add hl,de
        ld b,5
.shift:
        srl h
        rr l
        djnz .shift
        ld de,(FtShown)
        add hl,de
        ld (FtShown),hl
FtBar:
        ld a,FT_REG_HI
        ld de,#2054
        call FtRd32
        ld a,(FtBuf)
        and 3
        ret nz
        ; Codex: сдвиг на радиус RECT даёт точную границу затемнения; изменяется одна вершина.
        ld hl,(FtShown)
        ld de,FILL_X0+1
        add hl,de
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,h
        or #40
        ld h,a
        ld (FtSplashFill+2),hl
        ld a,FT_REG_HI
        ld de,0
        ld hl,FtSplashDl
        ld bc,FtSplashEnd-FtSplashDl
        call FtWrite
        ld de,#2054
        ld hl,2
        jp FtWrReg
; Клод: перед запуском ELF: метка в RAM_G (как у DOOM): картинка на месте, полоса — FtShown точек, слово картинки,
; куда CORERST в ft_open положит CHIPID. Дальше экран ведёт ELF.
FtHandover:
        ld a,(FtLive)
        or a
        jr z,.off
        ld a,#0C
        ld de,0
        ld hl,FtMark+8
        ld b,4
        call FtRead
        ld hl,(FtShown)
        ld (FtMark+4),hl
        ld a,#07
        ld de,#FFF0
        ld hl,FtMark
        ld bc,16
        call FtWrite
.off:
        xor a
        ld (FtLive),a
        ld (FtOk),a
        ret
FT_AT_MOUNT equ 16
FT_AT_GAME equ 32
FT_AT_QUERY equ 48
FT_AT_COMMIT equ 340
FT_AT_WAD equ 460
FtGap: db 0
FtLoading:
        ld a,(BootStage)
        ld hl,200
        cp 8
        jp z,FtProgress
        ld hl,330
        cp 9
        jp z,FtProgress
        ld hl,480
        cp 10
        jp z,FtProgress
        jp FtTick
; Клод: шрифты ПЗУ заливает ESP сразу после загрузки ядра; распаковка картинки в это время отнимала бы порт
; у её окна (DOOM 0.1.5 на плате). Z — шрифт 29 уже на месте.
FtFontsReady:
        ld a,#2F
        ld de,#FFFC
        call FtRd32
        ld a,(FtBuf+3)
        or a
        jr nz,.no
        ld a,(FtBuf)
        and 3
        jr nz,.no
        ld a,(FtBuf+2)
        cp #20
        jr c,.no
        cp #30
        jr nc,.no
        ld hl,(FtBuf)
        ld de,148*13+140
        add hl,de
        ex de,hl
        ld a,(FtBuf+2)
        adc a,0
        call FtRd32
        ld a,(FtBuf)
        or a
        jr z,.no
        cp 65
        jr nc,.no
        ld a,(FtBuf+1)
        or a
        ret
.no:
        ld a,1
        or a
        ret
; HL ← REG_CMDB_SPACE: свободно в кольце, кратно 4; #FFC — пусто.
FtSpace:
        ld a,FT_REG_HI
        ld de,#2574
        call FtRd32
        ld hl,(FtBuf)
        ld a,l
        and #FC
        ld l,a
        ld a,h
        and #0F
        ld h,a
        ret
; В кольцо через REG_CMDB_WRITE: HL — откуда, DE — байтов (кратно 4). CF — сопроцессор не берёт.
FtCmdbPut:
        ld (FtSrc),hl
        ld (FtLeft),de
        ld hl,FT_SPIN
        ld (FtSpin),hl
.loop:
        ld hl,(FtLeft)
        ld a,h
        or l
        ret z
        call FtSpace
        ld a,h
        or l
        jr nz,.room
        ld hl,(FtSpin)
        dec hl
        ld (FtSpin),hl
        ld a,h
        or l
        jr nz,.loop
        scf
        ret
.room:
        ld b,h
        ld c,l
        ld hl,(FtLeft)
        push hl
        or a
        sbc hl,bc
        pop hl
        jr nc,.send
        ld b,h
        ld c,l
.send:
        push bc
        ld hl,(FtSrc)
        ld a,FT_REG_HI
        ld de,#2578
        call FtWrite
        ld (FtSrc),hl
        pop bc
        ld hl,(FtLeft)
        or a
        sbc hl,bc
        ld (FtLeft),hl
        ld hl,FT_SPIN
        ld (FtSpin),hl
        jr .loop
; Клод: чёрный экран, шрифты ПЗУ (до ~3 с), JPEG страницами SPG в CMD_LOADIMAGE, распаковка (до ~5 с), затем сразу весь экран.
; Сопроцессор не взял картинку — экран остаётся чёрным, а загрузка идёт как обычно.
FtPicture:
        ld a,(FtOk)
        or a
        ret z
        call FtBegin
        call FtPresent
        ld hl,300
.fonts:
        push hl
        call FtFontsReady
        pop hl
        jr z,.send
        push hl
        ld bc,5000
        call FtDelay
        pop hl
        dec hl
        ld a,h
        or l
        jr nz,.fonts
.send:
        ld hl,FtPicHead
        ld de,12
        call FtCmdbPut
        ret c
        ld a,LDRPIC_PAGE0
        ld (FtPage),a
        ld a,LDRPIC_PAGES
        ld (FtPages),a
.page:
        ld a,(FtPage)
        ld bc,#13AF
        out (c),a
        ld hl,#C000
        ld de,16384
        ld a,(FtPages)
        dec a
        jr nz,.full
        ld de,LDRPIC_LAST
.full:
        call FtCmdbPut
        jr c,.restore
        ld hl,FtPage
        inc (hl)
        ld hl,FtPages
        dec (hl)
        jr nz,.page
.restore:
        ld bc,#13AF
        ld a,#20
        out (c),a
        ld a,(FtPages)
        or a
        ret nz
        ld hl,500
.unpack:
        push hl
        call FtSpace
        ld de,#0FFC
        or a
        sbc hl,de
        pop hl
        jr z,.live
        push hl
        ld bc,5000
        call FtDelay
        pop hl
        dec hl
        ld a,h
        or l
        ret z
        jr .unpack
.live:
        ld hl,8
        ld (FtShown),hl
        ld (FtCap),hl
        ld a,1
        ld (FtLive),a
        jp FtBar
FtError:
        ld a,(FtOk)
        or a
        ret z
        call FtBegin
        ld hl,MsgFailed
        call FtLine
        ld hl,(BootMessage)
        ld a,h
        or l
        jr nz,.message
        ld a,(BootStage)
        dec a
        and 15
        cp 12
        jr c,.stage
        xor a
.stage:
        add a,a
        ld e,a
        ld d,0
        ld hl,StageMessages
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
.message:
        call FtLine
        ld a,(BootStage)
        ld hl,ErrorLine+6
        call AsciiHex
        ld a,(BootError)
        ld hl,ErrorLine+14
        call AsciiHex
        ld hl,ErrorLine
        call FtLine
        ld a,(BootError)
        cp #E0
        jr nz,.reset
        ld hl,TimeoutLine
        call FtLine
.reset:
        ld a,(LastRequestValid)
        or a
        jr z,.no_request
        ld hl,LastRequestLine
        call FtLine
        ld hl,LastFileLine
        call FtLine
        ld hl,LastRangeLine
        call FtLine
.no_request:
        ld hl,MsgReset
        call FtLine
        jp FtPresent
AsciiHex:
        push af
        rrca
        rrca
        rrca
        rrca
        call .digit
        inc hl
        pop af
.digit:
        and 15
        add a,'0'
        cp '9'+1
        jr c,.put
        add a,7
.put:
        ld (hl),a
        ret
; Codex: PATH_GET — только чтение; штатный обработчик пишет имя запроса в журнал ESP.
; HMM2-Esscc кодирует этап и отказ, не создаёт записей path.cfg. При обрыве LINK
; остаются экран и пассивный FT_SEEN, доставить сообщение отключённой ESP невозможно.
LogBootError:
        ld a,(BootStage)
        ld hl,ErrorQuery+10
        call AsciiHex
        ld a,(BootError)
        ld hl,ErrorQuery+12
        call AsciiHex
        ld hl,ErrorQuery
        ld bc,ErrorQueryEnd-ErrorQuery
        ld a,#2A
        jp Proto_Send
ErrorQuery: dd 0
        db "HMM2-E0000",0
ErrorQueryEnd:
ErrorLine: db "STAGE 00 CODE 00",0
TimeoutLine: db "ESP RESPONSE TIMEOUT",0
FtBegin:
        ld hl,FtHead
        ld de,FtDl
        ld bc,FtHeadEnd-FtHead
        ldir
        ld (FtPtr),de
        ld hl,240*16
        ld (FtY),hl
        ret
FtLine:
        ld de,128*16
        ld (FtX),de
        ld b,48
.char:
        ld a,(hl)
        inc hl
        or a
        jr z,.next
        push hl
        push bc
        call FtChar
        pop bc
        pop hl
        djnz .char
.next:
        ld hl,(FtY)
        ld de,32*16
        add hl,de
        ld (FtY),hl
        ret
FtChar:
        ; Codex: имена файлов и сообщение игры приходят также в нижнем регистре.
        cp 'a'
        jr c,.upper
        cp 'z'+1
        jr nc,.upper
        and #DF
.upper:
        cp ' '
        jr z,.advance
        cp '0'
        jr c,.punct
        cp '9'+1
        jr c,.number
        cp 'A'
        jr c,.punct
        cp 'Z'+1
        jr nc,.advance
        sub 'A'
        jr .glyph
.number:
        sub '0'-29
        jr .glyph
.punct:
        cp '.'
        jr nz,.colon
        ld a,26
        jr .glyph
.colon:
        cp ':'
        ld a,27
        jr z,.glyph
        ld a,28
.glyph:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld (FtWord),hl
        ld hl,#0100
        ld (FtWord+2),hl
        call FtWordPut
        ld hl,(FtY)
        ld (FtWord),hl
        ; Codex: x=128..880 кратно 16; VERTEX2F: x*16 << 15, y*16.
        ld hl,(FtX)
        ld a,l
        rrca
        and #80
        ld de,(FtY)
        or d
        ld (FtWord+1),a
        ld a,l
        srl h
        rra
        ld (FtWord+2),a
        ld a,h
        or #40
        ld (FtWord+3),a
        call FtWordPut
.advance:
        ld hl,(FtX)
        ld de,16*16
        add hl,de
        ld (FtX),hl
        ret
FtWordPut:
        ld hl,FtWord
        ld de,(FtPtr)
        ld bc,4
        ldir
        ld (FtPtr),de
        ret
FtPresent:
        ld hl,0
        ld (FtWord),hl
        ld (FtWord+2),hl
        call FtWordPut
        ld hl,(FtPtr)
        ld de,FtDl
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ld hl,FtDl
        ld a,#30
        ld de,0
        call FtWrite
        ld de,#2054
        ld hl,2
        jp FtWrReg
FtHead:
        dd #020A1020,#26000007,#04FFFFFF,#27000004
        dd #07080208,#08002010,#15000080,#19000080,#1F000001
FtHeadEnd:
FtSplashDl:
        dd #02000000,#26000007,#27000004,#04FFFFFF,#100000FF
        dd #05000000,#01000000|FT_PIC,#07380100,#28000009,#08000100,#29000009,#1F000001
        DL_V 0,0
        ; Codex: SCISSOR оставляет золотую рамку нетронутой; углы маски скрыты вне ложа.
        dd #21000000,#22000000
        dd #1B000000|(LOADER_X<<11)|LOADER_TOP
        dd #1C000000|(LOADER_FILL<<12)|(LOADER_BOTTOM-LOADER_TOP)
        dd #1F000009,#0E000010,#04000000,#10000000|LOADER_MASK_ALPHA
FtSplashFill:
        DL_V FILL_X0+1,LOADER_TOP-2
        DL_V FILL_X0+LOADER_FILL+2,LOADER_BOTTOM+2
        dd #21000000,#23000000,#00000000
FtSplashEnd:
FtPicHead:
        dd #FFFFFF24,FT_PIC,2
FtMark:
        db "VDLP"
        dw 0,0
        ds 4
        dd 0
FtOk: db 0
FtLive: db 0
FtPage: db 0
FtPages: db 0
FtCap: dw 0
FtShown: dw 0
FtLast: dw 0
FtSrc: dw 0
FtLeft: dw 0
FtSpin: dw 0
FtBuf: ds 4
FtWord: ds 4
FtX: dw 0
FtY: dw 0
FtPtr: dw 0
; Клод: список экрана ошибки — в окне #C000: при обычном запуске он пуст, а в коде SPG для него нет места.
FtDl equ #C000
