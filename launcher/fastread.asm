; Codex: позиционирование O(1): LBA каждого кластера в страницах 64..127.
; FAT читается при открытии, с кэшем одного сектора на 128 записей.
; Переполнения 1280 отрезков и скрытого возврата в FILEX больше нет.
; Кольцо данных: страницы #80..#F6 и #F8..#FF, по 32 сектора.
; #F7 хранит пути Wild Commander и не участвует в распределении памяти.
; Метки логических слотов — в #BA00..#BBFF, вне кода.
; Пока Z80 ждёт ESP, FastIdle подгружает в кольцо строки по 4 КиБ за последним запросом:
; FAST_AHEAD при чтении подряд, FAST_AHEAD_NEAR вразброс. За один проход — одна строка.
FAST_RING_PAGE  equ 128
WC_PATH_PAGE    equ #F7
FAST_RING_PAGES equ 127
        ASSERT WC_PATH_PAGE >= FAST_RING_PAGE && WC_PATH_PAGE <= #FF
        ASSERT FAST_RING_PAGE+FAST_RING_PAGES = #FF
FAST_AHEAD      equ 16
FAST_AHEAD_NEAR equ 8
FastTags        equ #BA00
        ASSERT ScanFatBuffer+512 <= FastTags
; Codex: внутри тех же 128 записей FAT не повторяем деление и сложение LBA.
; Проверка значения следующего кластера остаётся штатной, включая reserved/EOF.
FastNextCluster:
        ld hl,(ScanCluster+2)
        ld de,(FastFatCluster+2)
        or a
        sbc hl,de
        jr nz,.miss
        ld a,(ScanCluster+1)
        ld hl,FastFatCluster+1
        cp (hl)
        jr nz,.miss
        ld a,(ScanCluster)
        and #80
        dec hl
        cp (hl)
        jr nz,.miss
        ld a,(ScanCluster)
        and 127
        ld (ScanFatEntry),a
        jp ScanNextCluster.cached
.miss:
        ld hl,(ScanCluster+2)
        ld (FastFatCluster+2),hl
        ld hl,(ScanCluster)
        ld a,l
        and #80
        ld l,a
        ld (FastFatCluster),hl
        jp ScanNextCluster

; Клод: DE:HL — смещение в файле, в FastSector — номер сектора; HL, DE — он же.
FastSectorOf:
        ld b,9
.shift:
        srl d
        rr e
        rr h
        rr l
        djnz .shift
        ld (FastSector),hl
        ld (FastSector+2),de
        ret

FastMap:
        ld b,a
        ld a,(WC_PAGE3)
        cp b
        ld a,b
        ret z
        ld (WC_PAGE3),a
        ld bc,#13AF
        out (c),a
        ret

; Codex: DE:HL — номер кластера файла, результат HL в окне его таблицы.
FastMapAddress:
        ld a,h
        and #F0
        rrca
        rrca
        rrca
        rrca
        ld b,a
        ld a,e
        rlca
        rlca
        rlca
        rlca
        or b
        add a,64
        call FastMap
        ld a,h
        and 15
        ld h,a
        add hl,hl
        add hl,hl
        set 6,h
        set 7,h
        ret

FastResolve:
        ld hl,(FastSector)
        ld de,(FastSector+2)
        ld a,(ScanSectorsPerCluster)
        ld b,a
        dec a
        and l
        ld (FastWithin),a
.divide:
        srl b
        jr c,.divided
        srl d
        rr e
        rr h
        rr l
        jr .divide
.divided:
        call FastMapAddress
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)
        ex de,hl
        ld a,(FastWithin)
        ld e,a
        ld d,0
        add hl,de
        jr nc,.lba
        inc bc
.lba:
        ld (ScanLba),hl
        ld (ScanLba+2),bc
        ld a,(ScanSectorsPerCluster)
        sub e
        ld (FastRun),a
        ret

FastReadAt:
        ld a,(WC_PAGE3)
        ld (FastOutPage),a
        ld hl,(Fx+10)
        ld (FastRemaining),hl
        ld a,h
        or l
        ret z
        ld de,(Fx+4)
        add hl,de
        ld (FastEnd),hl
        ld hl,(Fx+6)
        ld de,0
        adc hl,de
        jp c,FastBad
        ld de,(FastSize+2)
        or a
        sbc hl,de
        jp c,.inside
        jp nz,FastBad
        ld hl,(FastEnd)
        ld de,(FastSize)
        or a
        sbc hl,de
        jp z,.inside
        jp nc,FastBad
.inside:
        ld hl,(Fx+8)
        ld (FastDest),hl
        ld hl,(Fx+4)
        ld a,h
        and 1
        ld h,a
        ld (FastByte),hl
        ld hl,(Fx+4)
        ld de,(Fx+6)
        call FastSectorOf
        ; Клод: чтение вперёд — только когда запрос продолжает предыдущий (та же или
        ; следующая строка 4 КиБ). При чтении вразброс оно занимало SD после каждого запроса,
        ; и следующий запрос ждал его конца.
        ld a,l
        and #F8
        ld l,a
        ld bc,(FastSeqNext)
        or a
        sbc hl,bc
        ex de,hl
        ld bc,(FastSeqNext+2)
        sbc hl,bc
        ld a,h
        or l
        or d
        or e
        jr z,.sequential
        ld a,h
        and l
        and d
        inc a
        jr nz,.random
        ld a,e
        cp #F8
        jr z,.sequential
.random:
        xor a
        jr .ahead
.sequential:
        ld a,FAST_AHEAD
.ahead:
        ld (FastSeqAhead),a
        call FastMusicNotice
.loop:
        call FastBuffer
        jp nz,.return
        ld hl,512
        ld de,(FastByte)
        or a
        sbc hl,de
        ld de,(FastRemaining)
        push hl
        or a
        sbc hl,de
        pop hl
        jr c,.chunk
        ex de,hl
.chunk:
        ld (FastPart),hl
        ld b,h
        ld c,l
        ld a,(FastSector)
        and 31
        add a,a
        add a,#C0
        ld h,a
        ld l,0
        ld de,(FastByte)
        add hl,de
        ld de,(FastDest)
        bit 6,d
        jr nz,.bank
        ldir
        jr .copied
.bank:
        ; Codex: большие ответы выровнены; DMA RAM→RAM в страницу 56.
        ; Для произвольных нечётных смещений ServeRead выбирает обычные пакеты.
        call FastCopyBank
.copied:
        ld (FastDest),de
        ld hl,(FastRemaining)
        ld de,(FastPart)
        or a
        sbc hl,de
        ld (FastRemaining),hl
        ld a,h
        or l
        jr z,.return
        ld hl,0
        ld (FastByte),hl
        ld hl,(FastSector)
        inc hl
        ld (FastSector),hl
        ld a,h
        or l
        jr nz,.loop
        ld hl,(FastSector+2)
        inc hl
        ld (FastSector+2),hl
        jr .loop
.return:
        push af
        ; Клод: после запроса Z80 в простое читает следующие блоки файла.
        ld hl,(FastSector)
        ld a,l
        and #F8
        ld l,a
        ld de,8
        add hl,de
        ld (FastSeqNext),hl
        ld (FastAheadBase),hl
        ld hl,(FastSector+2)
        ld de,0
        adc hl,de
        ld (FastSeqNext+2),hl
        ld (FastAheadBase+2),hl
        ld a,(FastSeqAhead)
        or a
        jr nz,.arm
        ld a,FAST_AHEAD_NEAR
.arm:
        ld (FastAheadLeft),a
        ld a,(FastOutPage)
        call FastMap
        pop af
        ret

; Клод: одна строка 4 КиБ за вызов из цикла ожидания Run: пока ESP считает, Z80 наполняет кольцо.
; Запрос ESP ждёт не дольше одной строки; ошибка SD просто останавливает подгрузку.
FastIdle:
        call FastMusicIdle
        ret c
        ld a,(FastAheadLeft)
        or a
        ret z
        dec a
        ld (FastAheadLeft),a
        ld a,(FastEnabled)
        or a
        jr z,.stop
        ld hl,(FastAheadBase)
        ld bc,(FastSectors)
        or a
        sbc hl,bc
        ld hl,(FastAheadBase+2)
        ld bc,(FastSectors+2)
        sbc hl,bc
        jr nc,.stop
        ld hl,(FastAheadBase)
        ld (FastSector),hl
        ld hl,(FastAheadBase+2)
        ld (FastSector+2),hl
        ; Клод: подгрузка берёт одну строку: остаток мал, признака «подряд» нет.
        ld hl,#0100
        ld (FastRemaining),hl
        xor a
        ld (FastSeqAhead),a
        ld a,(WC_PAGE3)
        push af
        call FastBuffer
        pop bc
        push af
        ld a,b
        call FastMap
        pop af
        jr nz,.stop
        ld hl,(FastAheadBase)
        ld de,8
        add hl,de
        ld (FastAheadBase),hl
        ret nc
        ld hl,(FastAheadBase+2)
        inc hl
        ld (FastAheadBase+2),hl
        ret
.stop:
        xor a
        ld (FastAheadLeft),a
        ret

FastCopyBank:
        push bc
        push hl
        push de
        ld a,c
        and 1
        ld (FastCopyTail),a
        res 0,c
        ld a,b
        or c
        jr z,.tail_only
        ld a,b
        rra
        ld a,c
        rra
        dec a
        ld bc,TS_DMALEN
        out (c),a
        ld b,high TS_DMANUM
        xor a
        out (c),a
        ld b,high TS_DMASADDRL
        call SpiLink_DmaAddr
        ex de,hl
        ld b,high TS_DMADADDRL
        out (c),l
        inc b
        ld a,h
        and #3F
        out (c),a
        inc b
        ld a,(FastOutPage)
        out (c),a
        ld b,high TS_DMACTR
        ld a,TS_DMA_RAM_RAM
        out (c),a
.wait:
        in a,(c)
        rla
        jr c,.wait
        pop de
        pop hl
        pop bc
        add hl,bc
        ex de,hl
        add hl,bc
        ex de,hl
        ld a,(FastCopyTail)
        or a
        ret z
        dec hl
        dec de
        jr .tail
.tail_only:
        pop de
        pop hl
        pop bc
.tail:
        ld a,(hl)
        push af
        ld a,(WC_PAGE3)
        push af
        ld a,(FastOutPage)
        call FastMap
        pop bc
        pop af
        ld (de),a
        ld a,b
        call FastMap
        inc hl
        inc de
        ret

FastBuffer:
        ld hl,(FastSector)
        ld (FastRequested),hl
        ld de,(FastSector+2)
        ld (FastRequested+2),de
        ld a,l
        and #E0
        ld l,a
        ld (FastBase),hl
        ld (FastBase+2),de
        ; Клод: слот и страницу выбирает FastMusicSelect; метка слота — 4 байта.
        call FastMusicSelect
        ld a,(FastRing)
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,de
        ld (FastTag),hl
        ; Клод: биты 0..3 младшего байта метки — какие строки по 4 КиБ в слоте уже прочитаны,
        ; бит 4 — слот недействителен. Сектор метки кратен 32, эти биты в номере всегда нули.
        ld c,(hl)
        ld a,c
        and #F0
        ld de,FastBase
        ex de,hl
        cp (hl)
        ex de,hl
        ld b,3
        jr nz,.other
.check:
        inc hl
        inc de
        ld a,(de)
        cp (hl)
        jr nz,.other
        djnz .check
        ld a,c
        and 15
        jr .known
.other:
        xor a
.known:
        ld (FastLines),a
        ld c,a
        ; Строка сектора: 1 << биты 3..4 номера.
        ld a,(FastRequested)
        and #18
        ld (FastWithinSlot),a
        rrca
        rrca
        rrca
        inc a
        ld b,a
        ld a,#80
.bit:
        rlca
        djnz .bit
        ld b,a
        ; Чтение вперёд и музыка (остаток #FFFF) заполняют слот целиком.
        ld a,(FastRemaining+1)
        inc a
        jr nz,.line
        ld a,c
        cp 15
        jr z,.ready
        jr .rest
.line:
        ld a,b
        and c
        jr nz,.ready
        ; Клод: вразброс читаем только нужную строку: 8 секторов вместо 32.
        ; Подряд и большими кусками — до конца слота.
        ld a,(FastSeqAhead)
        or a
        jr nz,.rest
        ld a,(FastRemaining+1)
        cp #11
        jr nc,.rest
        ld a,8
        jr .count
.rest:
        ld a,b
        neg
        and 15
        ld b,a
        ld a,(FastWithinSlot)
        neg
        add a,32
.count:
        ld (FastFillLeft),a
        ld a,b
        ld hl,FastLines
        or (hl)
        ld (hl),a
        ; Клод: метка снимается до перезаписи — при ошибке SD недописанный
        ; слот не выдаётся за старые данные (#FF у младшего байта не бывает).
        ld hl,(FastTag)
        ld (hl),#FF
        ld a,(FastWithinSlot)
        ld b,a
        ld hl,(FastBase)
        or l
        ld l,a
        ld (FastSector),hl
        ld hl,(FastBase+2)
        ld (FastSector+2),hl
        ld a,b
        add a,a
        add a,#C0
        ld h,a
        ld l,0
        ld (FastFillDest),hl
        call FastFill
        jr nz,.restore
        ld hl,FastBase
        ld de,(FastTag)
        ld bc,4
        ldir
        ld hl,(FastTag)
        ld a,(FastLines)
        or (hl)
        ld (hl),a
.ready:
        ld a,(FastDataPage)
        call FastMap
        xor a
.restore:
        push af
        ld hl,(FastRequested)
        ld (FastSector),hl
        ld hl,(FastRequested+2)
        ld (FastSector+2),hl
        pop af
        ret

; Клод: чтение E6 в начало страницы 56; выбор слота кольца после него считается заново.
FastFillPacket:
        ld hl,#C000
        ld (FastFillDest),hl
        ld a,56
        ld (FastDataPage),a
        ld a,#FF
        ld (FastSelBase),a
; Клод: FastFillLeft секторов файла с FastSector в страницу FastDataPage по адресу FastFillDest,
; прямо с SD: отрезок на кластер, один CMD18 на отрезок. Выход: A = 0 и Z — прочитано.
; Общий путь кольца, музыки и E6; сектор и остаток меняются, страница остаётся FastDataPage.
FastFill:
        call FastResolve
        ld a,(FastFillLeft)
        ld b,a
        ld a,(FastRun)
        cp b
        jr c,.take
        ld a,b
.take:
        ld (FastReadCount),a
        ld hl,(FastSectors)
        ld de,(FastSector)
        or a
        sbc hl,de
        push hl
        ld hl,(FastSectors+2)
        ld de,(FastSector+2)
        sbc hl,de
        ld a,h
        or l
        pop hl
        jr nz,.count
        ld a,h
        or a
        jr nz,.count
        ld a,(FastReadCount)
        cp l
        jr c,.count
        ld a,l
        ld (FastReadCount),a
.count:
        ld a,(FastDataPage)
        call FastMap
        ld hl,(ScanLba)
        ld de,(ScanLba+2)
        ld bc,(FastFillDest)
        ld a,(FastReadCount)
        call FastSDReadMany
        or a
        ret nz
        ld a,(FastReadCount)
        ld e,a
        ld d,0
        ld hl,(FastSector)
        add hl,de
        ld (FastSector),hl
        ld hl,(FastSector+2)
        ld de,0
        adc hl,de
        ld (FastSector+2),hl
        ld a,(FastReadCount)
        ld b,a
        ld a,(FastFillLeft)
        sub b
        ld (FastFillLeft),a
        ret z
        ld hl,(FastSectors)
        ld de,(FastSector)
        or a
        sbc hl,de
        jr nz,.more
        ld hl,(FastSectors+2)
        ld de,(FastSector+2)
        sbc hl,de
        ld a,h
        or l
        ret z
.more:
        ld a,(FastReadCount)
        add a,a
        ld hl,(FastFillDest)
        add a,h
        ld h,a
        ld (FastFillDest),hl
        jp FastFill
FastBad:
        ld a,#FE
FastError:
        or a
        ret
FastEnabled: db 0
FastCount: dd 0
FastSize: dd 0
FastSectors: dd 0
FastBuildLeft: dd 0
FastSector: dd 0
FastRemaining: dw 0
FastDest: dw 0
FastByte: dw 0
FastEnd: dw 0
FastPart: dw 0
FastWithin: db 0
FastRun: db 0
FastRing: db 0
FastOutPage: db 0
FastTag: dw 0
FastAheadBase: dd 0
FastAheadLeft: db 0
FastBase: dd 0
FastRequested: dd 0
FastFillDest: dw 0
FastFillLeft: db 0
FastLines: db 0
FastWithinSlot: db 0
; Клод: начало следующей строки после конца запроса; FastIdle его не сдвигает.
FastSeqNext: dd 0
FastSeqAhead: db 0
FastReadCount: db 0
FastCopyTail: db 0
FastFatCluster: dd 0
FastTableLba: dd 0
FastTablePtr: dw 0
FastTablePage: db 0
