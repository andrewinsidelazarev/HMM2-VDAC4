; Codex: карта до 1280 кусков; индексы u32; при переполнении таблицы штатный FILEX.
; (Andrew Lazarev, MIT; licenses/R-Type-MIT.txt).






FastExtents equ #C400
FastMaxExtents equ (#10000-FastExtents)/12

FastOpen:
        xor a
        ld (FastEnabled),a
        ld (FastCacheValid),a
        ld (ScanFatValid),a
        ld (ScanSector),a
        ld hl,0
        ld (FastCount),hl
        ld (FastSector),hl
        ld (FastSector+2),hl
        ld hl,(#3868+28)
        ld de,(#3868+30)
        ld (FastSize),hl
        ld (FastSize+2),de

        ld bc,511
        add hl,bc
        jp nc,.rounded
        inc de
        ld a,d
        or e
        jp z,.fallback
.rounded:
        ld b,9
.shift_size:
        srl d
        rr e
        rr h
        rr l
        djnz .shift_size
        ld (FastSectors),hl
        ld (FastSectors+2),de
        ld a,h
        or l
        or d
        or e
        jp z,.fallback
        ld hl,(#3868+26)
        ld (ScanCluster),hl
        ld hl,(#3868+20)
        ld a,h
        and #0F
        ld h,a
        ld (ScanCluster+2),hl
.cluster:
        call ScanClusterLba
        jp c,FastBad

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
        jp nz,.full_cluster
        ld a,(ScanSectorsPerCluster)
        ld e,a
        ld d,0
        push hl
        or a
        sbc hl,de
        pop hl
        jp c,.part
.full_cluster:
        ld a,(ScanSectorsPerCluster)
        ld l,a
        ld h,0
.part:
        ld (FastPart),hl
        call FastAppend
        jp c,.fallback
        ld hl,(FastSector)
        ld de,(FastPart)
        add hl,de
        ld (FastSector),hl
        ld hl,(FastSector+2)
        ld de,0
        adc hl,de
        ld (FastSector+2),hl
        ld hl,(FastSector)
        ld de,(FastSectors)
        or a
        sbc hl,de
        jp nz,.more
        ld hl,(FastSector+2)
        ld de,(FastSectors+2)
        or a
        sbc hl,de
        jp z,.ready
.more:
        call ScanNextCluster
        jp c,FastError
        jp z,FastBad
        jp .cluster
.ready:
        ld a,1
        ld (FastEnabled),a
.fallback:
        xor a
        ret


FastAppend:
        ld hl,(FastCount)
        ld a,h
        or l
        jp z,.new
        dec hl
        call FastAddress
        ld l,(ix+8)
        ld h,(ix+9)
        ld e,(ix+4)
        ld d,(ix+5)
        add hl,de
        ld e,(ix+10)
        ld d,(ix+11)
        jp nc,.end_lba
        inc de
.end_lba:
        ld bc,(ScanLba)
        or a
        sbc hl,bc
        jp nz,.new
        ex de,hl
        ld bc,(ScanLba+2)
        or a
        sbc hl,bc
        jp nz,.new
        ld l,(ix+4)
        ld h,(ix+5)
        ld de,(FastPart)
        add hl,de
        jp c,.new
        ld (ix+4),l
        ld (ix+5),h
        or a
        ret
.new:
        ld hl,(FastCount)
        ld de,FastMaxExtents
        or a
        sbc hl,de
        jp nc,.full
        ld hl,(FastCount)
        call FastAddress
        inc hl
        ld (FastCount),hl
        ld hl,(FastSector)
        ld (ix+0),l
        ld (ix+1),h
        ld hl,(FastSector+2)
        ld (ix+2),l
        ld (ix+3),h
        ld hl,(FastPart)
        ld (ix+4),l
        ld (ix+5),h
        ld hl,(ScanLba)
        ld (ix+8),l
        ld (ix+9),h
        ld hl,(ScanLba+2)
        ld (ix+10),l
        ld (ix+11),h
        or a
        ret
.full:
        scf
        ret
FastAddress:
        push hl
        add hl,hl
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        add hl,hl
        ld de,FastExtents
        add hl,de
        push hl
        pop ix
        pop hl
        ret


FastReadAt:
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
        jp c,.inside
        jp nz,FastBad
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
        ld b,9
.shift_offset:
        srl d
        rr e
        rr h
        rr l
        djnz .shift_offset
        ld (FastSector),hl
        ld (FastSector+2),de
.loop:
        call FastReadSector
        ret nz
        ld hl,512
        ld de,(FastByte)
        or a
        sbc hl,de
        ld de,(FastRemaining)
        push hl
        or a
        sbc hl,de
        pop hl
        jp c,.chunk
        ex de,hl
.chunk:
        ld (FastPart),hl
        ld b,h
        ld c,l
        ld hl,ScanBuffer
        ld de,(FastByte)
        add hl,de
        ld de,(FastDest)
        ldir
        ld (FastDest),de
        ld hl,(FastRemaining)
        ld de,(FastPart)
        or a
        sbc hl,de
        ld (FastRemaining),hl
        ld a,h
        or l
        ret z
        ld hl,0
        ld (FastByte),hl
        ld hl,(FastSector)
        inc hl
        ld (FastSector),hl
        ld a,h
        or l
        jp nz,.loop
        ld hl,(FastSector+2)
        inc hl
        ld (FastSector+2),hl
        jp .loop



FastReadSector:
        ld a,(FastCacheValid)
        or a
        jp z,.lookup
        ld hl,(FastSector)
        ld de,(FastCached)
        or a
        sbc hl,de
        jp nz,.lookup
        ld hl,(FastSector+2)
        ld de,(FastCached+2)
        or a
        sbc hl,de
        ret z
.lookup:
        xor a
        ld (FastCacheValid),a
        ld hl,0
.find:
        ld de,(FastCount)
        push hl
        or a
        sbc hl,de
        pop hl
        jp nc,FastBad
        call FastAddress
        push hl
        ld hl,(FastSector)
        ld e,(ix+0)
        ld d,(ix+1)
        or a
        sbc hl,de
        ld (FastDelta),hl
        ld hl,(FastSector+2)
        ld e,(ix+2)
        ld d,(ix+3)
        sbc hl,de
        jp c,.next
        ld a,h
        or l
        jp nz,.next
        ld hl,(FastDelta)
        ld e,(ix+4)
        ld d,(ix+5)
        or a
        sbc hl,de
        jp nc,.next
        ld hl,(FastDelta)
        ld e,(ix+8)
        ld d,(ix+9)
        add hl,de
        ld (ScanLba),hl
        ld l,(ix+10)
        ld h,(ix+11)
        jp nc,.lba
        inc hl
.lba:
        ld (ScanLba+2),hl
        pop hl
        ld bc,ScanBuffer
        call ScanReadLba
        ret nz
        ld hl,(FastSector)
        ld (FastCached),hl
        ld hl,(FastSector+2)
        ld (FastCached+2),hl
        ld a,1
        ld (FastCacheValid),a
        xor a
        ret
.next:
        pop hl
        inc hl
        jp .find
FastBad:
        ld a,#FE
FastError:
        or a
        ret
FastEnabled: db 0
FastCacheValid: db 0
FastCount: dw 0
FastSector: dd 0
FastSectors: dd 0
FastPart: dw 0
FastRemaining: dw 0
FastDest: dw 0
FastByte: dw 0
FastCached: dd 0
FastEnd: dw 0
FastSize: dd 0
FastDelta: dw 0
