; Клод: поток DAT читается так же, как LOAD512 в WC (DSDZC.ASM: RDDSE, READS, CMD12, SNB) —
; им на плате читают видео FTVIEW и плеер VDAC4. CMD18, на каждый блок — маркер, 512 байт
; DMA TS-Conf (SPI → ОЗУ, как READS) и два IN под CRC; в конце CMD12: байт пропуска,
; ответ R1 (значение не проверяется, как у WC), ещё 16 байт и ожидание готовности.
; Прежний поток Codex на плате 28.10 давал E2 на этапе 8: после CMD12 он проверял R1 и не
; вычитывал хвост, а карта ещё выдаёт байты данных. Стенд SD останавливает поток сразу.
; Как LOAD512, программную CRC16 по данным DAT не считаем; чтения FAT и запись — у драйвера.
        INCLUDE "sd_symbols.inc"
; Клод: у WC здесь %01000010, но бит 6 (OPT) по таблице TS-Conf действует только при блиттинге;
; для SPI → ОЗУ это тот же режим #02.
SD_DMA_READ     equ TS_DMA_SPI_RAM
; A — 1..32 сектора, DE:HL — LBA, BC — адрес #C000..#FE00 (сектора не выходят за #FFFF).
; Выход: A = 0 и Z — прочитано; иначе A — код ошибки.
FastSDReadMany:
        ld (SdManyLeft),a
        call SD_ADDRESS
        ret c
        ld a,18
        call SD_COMMAND
        jp c,SD_FAILED
        or a
        jp nz,SD_BAD_REPLY
.sector:
        call SD_WAIT_TOKEN
        jr c,.stop
        cp #FE
        jr nz,.token_error
        ld hl,(SD_BUFFER)
        ld bc,TS_DMADADDRL
        call SpiLink_DmaAddr
        ld b,high TS_DMALEN
        ld a,255
        out (c),a
        ld b,high TS_DMANUM
        xor a
        out (c),a
        ld b,high TS_DMACTR
        ld a,SD_DMA_READ
        out (c),a
.wait:
        in a,(c)
        rla
        jr c,.wait
        ld bc,SD_DATA
        in a,(c)
        in a,(c)
        ld hl,(SD_BUFFER)
        inc h
        inc h
        ld (SD_BUFFER),hl
        ld hl,SdManyLeft
        dec (hl)
        jr nz,.sector
        xor a
        jr .stop
.token_error:
        ld a,#E2
.stop:
        ; Codex: даже при ошибке маркера закрываем открытый CMD18.
        push af
        ld bc,SD_DATA
        ld a,#4C
        out (c),a
        xor a
        out (c),a
        out (c),a
        out (c),a
        out (c),a
        ld a,#FF
        out (c),a
        in a,(c)
        ld d,10
.r1:
        in a,(c)
        bit 7,a
        jr z,.flush
        dec d
        jr nz,.r1
.flush:
        ld d,16
.skip:
        in a,(c)
        dec d
        jr nz,.skip
        call SD_WAIT_READY
        jr nc,.done
        pop af
        or a
        jr nz,.failed
        ld a,#E1
.failed:
        jp SD_FAILED
.done:
        pop af
        jp SD_FAILED
SdManyLeft: db 0
