FastOpen:
        ld a,32
        call FastMap
        xor a
        ld (FastEnabled),a
        ld (FastAheadLeft),a
        ld (ScanFatValid),a
        ld (ScanSector),a
        ld hl,#FFFF
        ld (FastFatCluster),hl
        ld (FastFatCluster+2),hl
        ld hl,0
        ld (FastCount),hl
        ld (FastCount+2),hl
        ld hl,FastTags
        ld de,FastTags+1
        ld bc,511
        ld (hl),#FF
        ldir
        ld hl,(#3868+28)
        ld de,(#3868+30)
        ld (FastSize),hl
        ld (FastSize+2),de
        ld bc,511
        add hl,bc
        jr nc,.round
        inc de
.round:
        ld b,9
.shift:
        srl d
        rr e
        rr h
        rr l
        djnz .shift
        ld (FastSectors),hl
        ld (FastSectors+2),de
        ld (FastBuildLeft),hl
        ld (FastBuildLeft+2),de
        ld a,h
        or l
        or d
        or e
        jp z,FastBad
        ld hl,(#3868+26)
        ld (ScanCluster),hl
        ld hl,(#3868+20)
        ld a,h
        and #0F
        ld h,a
        ld (ScanCluster+2),hl
        ld a,64
        ld (FastTablePage),a
        ld hl,#C000
        ld (FastTablePtr),hl
.fragment:
        call ScanClusterLba
        jp c,FastBad
        ld hl,(ScanLba)
        ld (FastTableLba),hl
        ld hl,(ScanLba+2)
        ld (FastTableLba+2),hl
.cluster:
        ; Codex: последовательные кластеры не требуют повторного умножения LBA
        ; и вычисления страницы для каждой записи. FAT всё равно проверяется.
        ld a,(FastTablePage)
        call FastMap
        ld hl,(FastTablePtr)
        ld de,(FastTableLba)
        ld (hl),e
        inc hl
        ld (hl),d
        inc hl
        ld de,(FastTableLba+2)
        ld (hl),e
        inc hl
        ld (hl),d
        inc hl
        ld (FastTablePtr),hl
        ld hl,(FastCount)
        inc hl
        ld (FastCount),hl
        ; Клод: долгая таблица DAT: полоса экрана загрузки идёт и здесь, раз в 256 кластеров.
        ld a,l
        or a
        call z,FtTick
        ld hl,(FastCount)
        ld a,h
        or l
        jr nz,.counted
        ld hl,(FastCount+2)
        inc hl
        ld (FastCount+2),hl
.counted:
        ld a,(ScanSectorsPerCluster)
        ld c,a
        ld b,0
        ld hl,(FastBuildLeft)
        or a
        sbc hl,bc
        ld (FastBuildLeft),hl
        ld hl,(FastBuildLeft+2)
        ld bc,0
        sbc hl,bc
        ld (FastBuildLeft+2),hl
        jr c,.ready
        ld de,(FastBuildLeft)
        ld a,h
        or l
        or d
        or e
        jr z,.ready
        ld hl,(ScanCluster)
        ld de,(ScanCluster+2)
        inc hl
        ld a,h
        or l
        jr nz,.expected
        inc de
.expected:
        push de
        push hl
        call FastNextCluster
        pop de
        pop hl
        jp c,FastError
        jp z,FastBad
        ld bc,(ScanCluster)
        ex de,hl
        or a
        sbc hl,bc
        jr nz,.fragment_next
        ld hl,(ScanCluster+2)
        sbc hl,de
        jr nz,.fragment_next
        jr .advance_lba
.fragment_next:
        call .next_page
        jp .fragment
.advance_lba:
        ld a,(ScanSectorsPerCluster)
        ld c,a
        ld b,0
        ld hl,(FastTableLba)
        add hl,bc
        ld (FastTableLba),hl
        jr nc,.next
        ld hl,(FastTableLba+2)
        inc hl
        ld (FastTableLba+2),hl
.next:
        call .next_page
        jp .cluster
.next_page:
        ld hl,(FastTablePtr)
        ld a,h
        or l
        ret nz
        ld hl,#C000
        ld (FastTablePtr),hl
        ld hl,FastTablePage
        inc (hl)
        ld a,(hl)
        cp 128
        ret c
        pop hl
        jp FastBad
.ready:
        ld a,32
        call FastMap
        ld a,1
        ld (FastEnabled),a
        jp FastMusicOpen

