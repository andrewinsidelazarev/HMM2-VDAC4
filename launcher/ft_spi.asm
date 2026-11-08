; Codex: хост SPI FT812 перенесён из DOOM VDAC4; происхождение в THIRD_PARTY.md.
; SPDX-License-Identifier: GPL-2.0-or-later
FT_REG_HI equ #30

FtOn:
        ld a,#02
        out (#77),a
        ld a,#07
        out (#77),a
        ret
FtOff:
        ld a,#03
        out (#77),a
        ret


FtCmd:
        call FtOn
        ld a,b
        out (#57),a
        ld a,c
        out (#57),a
        xor a
        out (#57),a
        jr FtOff



FtWrite:
        push af
        call FtOn
        pop af
        or #80
        out (#57),a
        ld a,d
        out (#57),a
        ld a,e
        out (#57),a
        ld a,c
        ld d,b
        ld c,#57
        or a
        jr z,.blocks
        ld b,a
        otir
.blocks:
        ld a,d
        or a
        jr z,.done
.block:
        ld b,0
        otir
        dec d
        jr nz,.block
.done:
        jr FtOff


FtRead:
        push bc
        push af
        call FtOn
        pop af
        and #3F
        out (#57),a
        ld a,d
        out (#57),a
        ld a,e
        out (#57),a
        xor a
        out (#57),a
        in a,(#57)
        pop bc
        ld c,#57
        inir
        jr FtOff


FtRd32:
        ld hl,FtBuf
        ld b,4
        jr FtRead


FtWrReg:
        ld (FtBuf),hl
        ld hl,0
        ld (FtBuf+2),hl
        ld a,FT_REG_HI
        ld hl,FtBuf
        ld bc,4
        jr FtWrite


FtDelay:
        dec bc
        ld a,b
        or c
        jr nz,FtDelay
        ret



FtModeTab:
        dw #2070,0
        dw #2038,24
        dw #203C,24+136
        dw #2030,24+136+160
        dw #2034,1024
        dw #202C,1344
        dw #204C,2
        dw #2050,8
        dw #2044,37
        dw #2048,768
        dw #2040,806
        dw #206C,0
        dw #2068,0
        dw #2070,1
        dw 0


