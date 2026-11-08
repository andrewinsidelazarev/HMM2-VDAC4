; Codex: время изменения FAT из Gluk RTC; буфер ответа ESP и часы/NVRAM не меняются.
FileStamp:
        call FileClock
        ret c
        ld hl,FileName
        ld a,(FileOp)
        cp 4
        jr nz,.find
        ld hl,OtherName
.find:
        ; Codex: MOVE сбрасывает выбор; после усечения тоже выбираем файл заново.
        call FAT32_FIND
        ret c
        ret z
        ld hl,FileTimeFx
        ; Codex: метаданные вспомогательны; основной результат и штатный SYNC сохраняются.
        jp FAT32_FILEX

; Codex: CF=1 при несогласованном/недопустимом снимке. VRT/SET у Evo не критерий даты.
FileClock:
        ld bc,#EFF7
        ld a,#80
        out (c),a
        ld d,4
.retry:
        ld a,#0A
        call RtcRegister
        bit 7,a
        jr nz,.again
        ld hl,RtcRegisters
        ld ix,FileRtc
        ld e,7
.read:
        ld a,(hl)
        inc hl
        call RtcRegister
        ld (ix),a
        inc ix
        dec e
        jr nz,.read
        ld a,#0A
        call RtcRegister
        bit 7,a
        jr nz,.again
        xor a
        call RtcRegister
        ld hl,FileRtc+1
        cp (hl)
        jr nz,.again
        ld a,#0B
        call RtcRegister
        dec hl
        cp (hl)
        jr z,.ready
.again:
        dec d
        jr nz,.retry
        scf
        jr .close
.ready:
        or a
.close:
        ; Codex: выбор FIFO возвращается и при отказе; очередь PS/2 не читается.
        ld b,#DF
        ld a,#F0
        out (c),a
        ret c

        ; Codex: PM отделяем только в 12-часовом режиме до проверки BCD.
        ld a,(FileRtc)
        ld e,a
        ld a,(FileRtc+3)
        ld d,a
        bit 1,e
        jr nz,.hours
        and #7F
.hours:
        ld (FileRtc+3),a
        ld hl,FileRtc+1
        ld b,6
.decode:
        ld a,(hl)
        bit 2,e
        jr nz,.binary
        call FileBcd
        ret c
.binary:
        ld (hl),a
        inc hl
        djnz .decode
        ld a,(FileRtc+3)
        bit 1,e
        jr nz,.hour24
        or a
        jr z,.bad
        cp 13
        jr nc,.bad
        cp 12
        jr nz,.pm
        xor a
.pm:
        bit 7,d
        jr z,.hour24
        add a,12
.hour24:
        cp 24
        jr nc,.bad
        ld (FileRtc+3),a
        ld a,(FileRtc+1)
        cp 60
        jr nc,.bad
        ld a,(FileRtc+2)
        cp 60
        jr nc,.bad
        ld a,(FileRtc+6)
        cp 100
        jr nc,.bad
        ld a,(FileRtc+5)
        dec a
        cp 12
        jr nc,.bad
        ld c,a
        ld b,0
        ld hl,FileMonthDays
        add hl,bc
        ld b,(hl)
        cp 1
        jr nz,.day
        ld a,(FileRtc+6)
        and 3
        jr nz,.day
        inc b
.day:
        ld a,(FileRtc+4)
        dec a
        cp b
        jr c,.pack
.bad:
        scf
        ret
.pack:
        ; Codex: 2000..2099; FAT хранит секунды с шагом 2, нечётные округляем вниз.
        ld a,(FileRtc+5)
        rrca
        rrca
        rrca
        ld c,a
        and 1
        ld d,a
        ld a,(FileRtc+6)
        add a,20
        add a,a
        or d
        ld d,a
        ld a,c
        and #E0
        ld e,a
        ld a,(FileRtc+4)
        or e
        ld e,a
        ld (FileTimeMeta+FILEX_META_O_WRITE_DATE),de
        ld a,(FileRtc+2)
        rrca
        rrca
        rrca
        ld c,a
        and 7
        ld b,a
        ld a,(FileRtc+3)
        add a,a
        add a,a
        add a,a
        or b
        ld h,a
        ld a,c
        and #E0
        ld c,a
        ld a,(FileRtc+1)
        srl a
        or c
        ld l,a
        ld (FileTimeMeta+FILEX_META_O_WRITE_TIME),hl
        ret

; Codex: строгая проверка обеих цифр BCD; B, DE, HL сохраняются, CF=1 при отказе.
FileBcd:
        ld c,a
        and #0F
        cp 10
        jr nc,.bad
        ld a,c
        and #F0
        cp #A0
        jr nc,.bad
        rrca
        rrca
        rrca
        rrca
        add a,a
        ld c,a
        add a,a
        add a,a
        add a,c
        ld c,a
        ld a,(hl)
        and #0F
        add a,c
        ret
.bad:
        scf
        ret

FileRtc: ds 7
FileMonthDays: db 31,28,31,30,31,30,31,31,30,31,30,31
FileTimeMeta:
        db FILEX_META_SIZE,0,0,FILEX_META_TIME_WRITE
        ds 12
        ASSERT $-FileTimeMeta == FILEX_META_SIZE
FileTimeFx:
        db FILEX_BLOCK_SIZE,FILEX_API_VERSION,FILEX_OP_SET_METADATA,0
        dd 0
        dw FileTimeMeta,FILEX_META_SIZE
        ds 20
        ASSERT $-FileTimeFx == FILEX_BLOCK_SIZE
