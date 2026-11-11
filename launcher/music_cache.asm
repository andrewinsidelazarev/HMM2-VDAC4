; Codex: независимая предвыборка музыки Z80 во все свободные банки из 4 МиБ ОЗУ Ево.
; E7 [DAT offset,u32 size]. Кольцо (кроме #F7) и остаток свободных банков — данные DAT; PCM обслуживает ESP.
MUSIC_MAX_PAGES equ 122
MUSIC_SLOTS equ 64
; Codex: свободные окна рабочей страницы FAT32; его секторные буферы и CRC-таблицы сохранены.
MusicTags equ #3600
MusicPageMap equ #3E00
        ASSERT MusicTags+MUSIC_MAX_PAGES*4 <= #37F8
        ASSERT MusicPageMap+MUSIC_MAX_PAGES <= #4000
FastMusicOpen:
        xor a
        ld (MusicActive),a
        ld hl,MusicPageMap
        ld bc,0
.low:
        ld a,b
        cp 2                     ; исполняемый код и стек SPG
        jr z,.skip
        cp 14                    ; код FAT32
        jr z,.skip
        cp 15                    ; рабочая страница FAT32
        jr z,.skip
        cp 32                    ; секторный буфер/поиск каталогов
        jr z,.skip
        cp 56                    ; пакет обмена с ESP
        jr z,.skip
        ld (hl),a
        inc hl
        inc c
.skip:
        inc b
        ld a,b
        cp 64
        jr c,.low
        ld a,(FastTablePage)
        inc a
        ld b,a
.high:
        ld a,b
        cp 128
        jr nc,.chosen
        ld (hl),a
        inc hl
        inc c
        inc b
        jr .high
.chosen:
        ld a,c
        ld (MusicPages),a
        ; Клод: музыке — 64 слота (1 МиБ, около 50 с при 168 кбит/с) из свободных банков, данным — все остальные.
        cp MUSIC_SLOTS
        jr c,.few
        ld a,MUSIC_SLOTS
.few:
        ld (MusicSlots),a
        ld b,a
        ld a,c
        sub b
        add a,FAST_RING_PAGES
        ld (FastDataSlots),a
        ld a,#FF
        ld (FastSelBase),a
        ld hl,MusicTags
        ld de,MusicTags+1
        ld bc,MUSIC_MAX_PAGES*4-1
        ld (hl),#FF
        ldir
        xor a
        ret

; Codex: сравнить два u32 по HL и DE, C означает первое меньше второго.
MusicCompare:
        inc hl
        inc hl
        inc hl
        inc de
        inc de
        inc de
        ld b,4
.loop:
        ld a,(de)
        ld c,a
        ld a,(hl)
        cp c
        ret nz
        dec hl
        dec de
        djnz .loop
        ret

MusicShift9:
        ld b,9
.loop:
        srl d
        rr e
        rr h
        rr l
        djnz .loop
        ret

ServeMusic:
        push af
        call MusicHint
        pop af
        ret
MusicHint:
        ld a,#FF
        ld (FastSelBase),a
        xor a
        ld (MusicActive),a
        ld hl,(ProtoRxLen)
        ld de,8
        or a
        sbc hl,de
        ret nz
        ld a,(FastEnabled)
        or a
        ret z
        ld a,(MusicPages)
        or a
        ret z
        ld hl,(ProtoBuf+4)
        ld de,(ProtoBuf+6)
        ld a,h
        or l
        or d
        or e
        ret z
        ld de,(ProtoBuf)
        add hl,de
        ld (MusicEnd),hl
        ld hl,(ProtoBuf+2)
        ld de,(ProtoBuf+6)
        adc hl,de
        ret c
        ld (MusicEnd+2),hl
        ld hl,FastSize
        ld de,MusicEnd
        call MusicCompare
        ret c
        ld hl,(ProtoBuf)
        ld de,(ProtoBuf+2)
        call MusicShift9
        ld a,l
        and #E0
        ld l,a
        ld (MusicStart),hl
        ld (MusicStart+2),de
        ld hl,MusicStart
        ld de,MusicDemand
        call ScanCopy32
        ld hl,MusicStart
        ld de,MusicCursor
        call ScanCopy32
        ld hl,(MusicEnd)
        ld de,(MusicEnd+2)
        ld bc,511
        add hl,bc
        jr nc,.end_shift
        inc de
.end_shift:
        call MusicShift9
        ld (MusicEnd),hl
        ld (MusicEnd+2),de
        ld a,1
        ld (MusicActive),a
        ret

; Codex: HL — номер сектора; NZ только в текущей области OGG.
FastMusicContains:
        ld a,(MusicActive)
        or a
        ret z
        push hl
        ld de,MusicStart
        call MusicCompare
        pop hl
        jr c,.outside
        push hl
        ld de,MusicEnd
        call MusicCompare
        pop hl
        jr nc,.outside
        ld a,1
        or a
        ret
.outside:
        xor a
        ret
FastMusicNotice:
        ld hl,FastSector
        call FastMusicContains
        ret z
        ld hl,(FastSector)
        ld a,l
        and #E0
        ld l,a
        ld (MusicDemand),hl
        ld hl,(FastSector+2)
        ld (MusicDemand+2),hl
        ret

; Клод: выбрать слот, страницу и таблицу меток. Данным — FAST_RING_PAGES банков кольца и все свободные банки,
; кроме MusicSlots под музыку: блок файла по модулю числа слотов. На один блок считается один раз.
FastMusicSelect:
        ld hl,FastBase
        ld de,FastSelBase
        ld b,4
.same:
        ld a,(de)
        cp (hl)
        jr nz,.compute
        inc hl
        inc de
        djnz .same
        ld de,(FastSelTable)
        ret
.compute:
        ld hl,FastBase
        ld de,FastSelBase
        call ScanCopy32
        call .select
        ld (FastSelTable),de
        ret
.select:
        ld hl,FastBase
        call FastMusicContains
        jr z,.ordinary
        ; Codex: номер блока относительно начала трека по модулю числа слотов музыки.
        ld hl,(FastBase)
        ld de,(MusicStart)
        or a
        sbc hl,de
        push hl
        ld hl,(FastBase+2)
        ld de,(MusicStart+2)
        sbc hl,de
        ex de,hl
        pop hl
        ld a,(MusicSlots)
        call .modulo
        jr .mapped
.ordinary:
        ld hl,(FastBase)
        ld de,(FastBase+2)
        ld a,(FastDataSlots)
        call .modulo
        cp FAST_RING_PAGES
        jr nc,.extra
        ld (FastRing),a
        add a,FAST_RING_PAGE
        cp WC_PATH_PAGE
        jr c,.ring_page
        inc a                    ; не выдавать страницу путей Wild Commander
.ring_page:
        ld (FastDataPage),a
        ld de,FastTags
        ret
.extra:
        sub FAST_RING_PAGES
        ld hl,MusicSlots
        add a,(hl)
.mapped:
        ld (FastRing),a
        ld l,a
        ld h,high MusicPageMap
        ld a,(hl)
        ld (FastDataPage),a
        ld de,MusicTags
        ret
; DE:HL — номер сектора, A — число слотов; выход: A — номер блока (сектор/32) по этому модулю.
.modulo:
        ld c,a
        ld b,5
.shift:
        srl d
        rr e
        rr h
        rr l
        djnz .shift
        ld b,32
        xor a
.bit:
        add hl,hl
        rl e
        rl d
        rla
        jr c,.sub
        cp c
        jr c,.keep
.sub:
        sub c
.keep:
        djnz .bit
        ret

; Codex: не более одного блока SD за проход. Новый запрос ESP имеет приоритет перед следующим.
FastMusicIdle:
        ld a,(MusicActive)
        or a
        ret z
        ld hl,MusicCursor
        ld de,MusicDemand
        call MusicCompare
        jr nc,.goal
        ld hl,MusicDemand
        ld de,MusicCursor
        call ScanCopy32
.goal:
        ld a,(MusicSlots)
        dec a
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,(MusicDemand)
        add hl,de
        ld (MusicGoal),hl
        ld hl,(MusicDemand+2)
        ld de,0
        adc hl,de
        ld (MusicGoal+2),hl
        ld hl,MusicCursor
        ld de,MusicGoal
        call MusicCompare
        jr nc,.none
        ld hl,MusicCursor
        ld de,MusicEnd
        call MusicCompare
        jr nc,.none
        ld hl,(MusicCursor)
        ld (FastSector),hl
        ld hl,(MusicCursor+2)
        ld (FastSector+2),hl
        ; Клод: блок музыки читается в слот целиком, как чтение вперёд.
        ld hl,#FFFF
        ld (FastRemaining),hl
        ld a,(WC_PAGE3)
        push af
        call FastBuffer
        pop bc
        push af
        ld a,b
        call FastMap
        pop af
        jr nz,.error
        ld hl,(MusicCursor)
        ld de,32
        add hl,de
        ld (MusicCursor),hl
        jr nc,.done
        ld hl,(MusicCursor+2)
        inc hl
        ld (MusicCursor+2),hl
.done:
        scf
        ret
.error:
        xor a
        ld (MusicActive),a
.none:
        or a
        ret
MusicActive: db 0
MusicPages: db 0
MusicSlots: db 0
FastDataSlots: db FAST_RING_PAGES
FastSelBase: dd #FFFFFFFF
FastSelTable: dw 0
MusicStart: dd 0
MusicEnd: dd 0
MusicDemand: dd 0
MusicCursor: dd 0
MusicGoal: dd 0
FastDataPage: db 0
