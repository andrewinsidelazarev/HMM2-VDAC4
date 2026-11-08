; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.


















































































        IFNDEF ZIFI_SPI_HEAD_PLACED
        DEFINE ZIFI_SPI_HEAD_PLACED

ZIFI_SPI_DATA   equ #57
ZIFI_SPI_CFG    equ #77





SPI_HOST_MASTER equ #02
SPI_SELECT_NONE equ #03
SPI_SELECT_ESP  equ #13

SPI_CMD_WRBUF   equ #01
SPI_CMD_RDBUF   equ #02

LINK_REQUEST    equ #C3
LINK_RESPONSE   equ #3C
LINK_HEADER     equ 5
LINK_CHUNK      equ 54
LINK_STATE_ADDR equ 60
LINK_TX_EMPTY   equ #5A
LINK_TX_PENDING equ #A5

LINK_F_DROP     equ #01
LINK_F_BULK     equ #02
LINK_F_SESSION  equ #80
LINK_S_MORE     equ #01
LINK_S_BULK     equ #02
LINK_S_STALE    equ #04
LINK_S_ECHO_BAD equ #08
LINK_F_PUT      equ #04
LINK_F_PUT_DONE equ #08
LINK_S_PUT_READY equ #10
LINK_S_PUT_OK   equ #20
LINK_S_PUT_BAD  equ #40
LINK_S_PUT_CAPABLE equ #80
LINK_BULK       equ 512




LINK_POLLS      equ 2048
LINK_ATTEMPTS   equ 16

LINK_PUSH_TRIES equ 2048

LINK_PUT_TRIES  equ 3


LINK_INIT_TRIES equ 16384



        IFDEF ZIFI_SPI_DMA
ZIFI_BURST_SIZE equ LINK_BULK
        ELSE
ZIFI_BURST_SIZE equ LINK_CHUNK+1
        ENDIF




ZiFi_Init:
        push hl
        ld a,SPI_HOST_MASTER
        out (ZIFI_SPI_CFG),a
        xor a
        ld (SpiTxLen),a
        ld (SpiMore),a
        IFDEF ZIFI_SPI_DMA
        ld (SpiPutCan),a
        ENDIF
        ld de,LINK_INIT_TRIES
.wait:
        call SpiLink_ReadState
        cp LINK_TX_EMPTY
        jr z,.found
        cp LINK_TX_PENDING
        jr z,.found
        dec de
        ld a,d
        or e
        jr nz,.wait
        pop hl
        scf
        ret
.found:


        ld a,LINK_F_SESSION|LINK_F_DROP
        call SpiLink_Send
        IFDEF ZIFI_SPI_DMA

        push af
        ld a,(SpiRsp)
        and LINK_S_PUT_CAPABLE
        ld (SpiPutCan),a
        pop af
        ENDIF
        pop hl
        ret




ZiFi_RxReady:
        ld a,(SpiMore)
        or a
        ret nz
        push bc
        call SpiLink_ReadState
        pop bc
        sub LINK_TX_PENDING
        jr z,.yes
        xor a
        ret
.yes:
        inc a
        ret




ZiFi_ReadBurst:
        push de
        IFDEF ZIFI_SPI_DMA
        ld (SpiBulkBuf),hl

        ld a,(SpiMore)
        and LINK_S_BULK
        jr nz,.fetch
.plain:
        ENDIF
        ld (SpiDest),hl

        ld a,(SpiTxLen)
        ld hl,SpiMore
        or (hl)
        jr nz,.turn
        call SpiLink_ReadState
        cp LINK_TX_PENDING
        jr nz,.none
.turn:
        ld a,LINK_CHUNK
        ld (SpiRoom),a
        IFDEF ZIFI_SPI_DMA
        ld a,LINK_F_BULK
        ELSE
        xor a
        ENDIF
        call SpiLink_Turn
        jr c,.none
        IFDEF ZIFI_SPI_DMA
        ld a,(SpiRsp)
        and LINK_S_BULK|LINK_S_STALE
        jr z,.inline
        cp LINK_S_BULK
        jr z,.fetch

        call SpiLink_BulkRead
        ld hl,SpiMore
        res 1,(hl)
        jr .none
.fetch:
        call SpiLink_BulkRead
        call SpiLink_BulkEcho


        ld a,LINK_F_BULK
        call SpiLink_Send
        jr c,.none
        ld a,(SpiRsp)
        and LINK_S_ECHO_BAD
        ld bc,LINK_BULK
        jr z,.done


        ld a,(SpiMore)
        and LINK_S_BULK
        jr nz,.fetch
        ld hl,(SpiBulkBuf)
        jr .plain
.inline:
        ENDIF
        ld a,(SpiRsp+1)
        ld c,a
        ld b,0
        jr .done
.none:
        ld bc,0
.done:
        pop de
        ld a,b
        or c
        ret





ZiFi_PutChar:
        push hl
        push de
        push bc
        ld c,a
        ld a,(SpiTxLen)
        cp LINK_CHUNK
        jr c,.store
        push bc
        ld c,LINK_CHUNK
        call SpiLink_Push
        pop bc
        jr c,.done
        ld a,(SpiTxLen)
.store:
        ld e,a
        ld d,0
        ld hl,SpiTxBuf
        add hl,de
        ld (hl),c
        inc a
        ld (SpiTxLen),a
.done:
        ld a,c
        pop bc
        pop de
        pop hl
        ret


SpiHeadEnd:

        ENDIF


        IFNDEF ZIFI_SPI_HEAD_ONLY



ZiFi_Flush:
        push hl
        push de
        push bc
        ld c,1
        call SpiLink_Push
        pop bc
        pop de
        pop hl
        ret

        IFUSED ZiFi_ClearRx

ZiFi_ClearRx:
        push hl
        push de
        xor a
        ld (SpiMore),a
        ld a,LINK_F_DROP
        call SpiLink_Send
        pop de
        pop hl
        ret
        ENDIF

        IFUSED ZiFi_ClearTx


ZiFi_ClearTx:
        xor a
        ld (SpiTxLen),a
        ret
        ENDIF

        IFUSED ZiFi_GetChar



ZiFi_GetChar:
        push hl
        push de
        push bc
        call ZiFi_RxReady
        jr z,.none
        ld hl,SpiOneByte
        ld (SpiDest),hl
        ld a,1
        ld (SpiRoom),a
        xor a
        call SpiLink_Turn
        jr c,.none
        ld a,(SpiRsp+1)
        or a
        jr z,.none
        ld a,(SpiOneByte)
        scf
        jr .done
.none:
        or a
.done:
        pop bc
        pop de
        pop hl
        ret
SpiOneByte:     ds 2
        ENDIF

        IFUSED ZiFi_PutZ


ZiFi_PutZ:
        ld a,(hl)
        or a
        jp z,ZiFi_Flush
        call ZiFi_PutChar
        ret c
        inc hl
        jr ZiFi_PutZ
        ENDIF

        IFUSED ZiFi_PutBlock


ZiFi_PutBlock:
        ld a,b
        or c
        jp z,ZiFi_Flush
        ld a,(hl)
        call ZiFi_PutChar
        ret c
        inc hl
        dec bc
        jr ZiFi_PutBlock
        ENDIF

        IFUSED ZiFi_SetTimeout

ZiFi_SetTimeout:
        ld (ZiFiTimeout),de
        xor a
        ld (ZiFiTimeout+2),a
        ld b,11
.shift:
        ld hl,(ZiFiTimeout)
        add hl,hl
        ld (ZiFiTimeout),hl
        ld a,(ZiFiTimeout+2)
        adc a,a
        ld (ZiFiTimeout+2),a
        djnz .shift
        ret



ZiFi_CheckTimeout:
        push hl
        ld hl,(ZiFiTimeout)
        ld a,h
        or l
        jr nz,.dec_low
        ld a,(ZiFiTimeout+2)
        or a
        jr z,.expired
        dec a
        ld (ZiFiTimeout+2),a
.dec_low:
        dec hl
        ld (ZiFiTimeout),hl
        pop hl
        scf
        ret
.expired:
        pop hl
        or a
        ret
ZiFiTimeout:    ds 3
        ENDIF




SpiLink_Push:
        ld de,LINK_PUSH_TRIES
.again:
        ld a,(SpiTxLen)
        cp c
        ccf
        ret nc
        push de
        push bc
        xor a
        call SpiLink_Send
        pop bc
        pop de
        ret c
        dec de
        ld a,d
        or e
        jr nz,.again

        ld (SpiTxLen),a
        scf
        ret


SpiLink_ReadState:
        ld b,LINK_STATE_ADDR
        call SpiLink_OpenRead

SpiLink_Close:
        ld b,a
        ld a,SPI_SELECT_NONE
        out (ZIFI_SPI_CFG),a
        ld a,b
        or a
        ret











SpiLink_OpenRead:
        ld a,SPI_CMD_RDBUF
        call SpiLink_Open
        in a,(c)
        call .first
        ret z
.first:
        in b,(c)
        ld a,b
        rrca
        rrca
        rrca
        rrca
        xor b
        inc a
        ld a,b
        ret

        IFDEF ZIFI_SPI_DMA
TS_DMASADDRL    equ #1AAF

TS_DMADADDRL    equ #1DAF
TS_DMALEN       equ #26AF
TS_DMACTR       equ #27AF
TS_DMANUM       equ #28AF
TS_DMA_RAM_RAM  equ #01
TS_DMA_SPI_RAM  equ #02
TS_DMA_RAM_SPI  equ #82
SPI_CMD_WRDMA   equ #03
SPI_CMD_RDDMA   equ #04
SPI_CMD_WR_DONE equ #07
SPI_CMD_RD_DONE equ #08


SPI_CACHE_SWEEP equ #8000



SpiLink_BulkEcho:
        ld bc,TS_DMASADDRL
        ld a,SPI_CMD_WRDMA
        call SpiLink_BulkOpen
        ld a,TS_DMA_RAM_SPI
        call SpiLink_BulkRun
        ld a,SPI_CMD_WR_DONE

SpiLink_BulkDone:
        ld b,a
        ld a,SPI_SELECT_ESP
        out (ZIFI_SPI_CFG),a
        ld a,b
        out (ZIFI_SPI_DATA),a
        in a,(ZIFI_SPI_DATA)
        ld a,SPI_SELECT_NONE
        out (ZIFI_SPI_CFG),a
        ret




SpiLink_BulkOpen:
        push af
        ld hl,(SpiBulkBuf)
        call SpiLink_DmaAddr
        ld b,high TS_DMALEN
        ld a,LINK_BULK/2-1
        out (c),a
        ld b,high TS_DMANUM
        xor a
        out (c),a
        ld a,SPI_SELECT_ESP
        out (ZIFI_SPI_CFG),a
        pop af
        out (ZIFI_SPI_DATA),a
        in a,(ZIFI_SPI_DATA)
        in a,(ZIFI_SPI_DATA)
        ret




SpiLink_DmaAddr:
        out (c),l
        inc b
        ld a,h
        and #3F
        out (c),a
        inc b
        ld a,(WC_PAGE2)
        bit 6,h
        jr z,.page
        ld a,(WC_PAGE3)
.page:
        out (c),a
        ret






SpiLink_Copy:
        ld a,l
        xor e
        rra
        jr c,.ldir
        bit 0,l
        jr z,.even
        ld a,b
        or c
        ret z
        ldi
.even:
        ld a,b
        cp high LINK_BULK
        jr nc,.full
        or a
        jr nz,.part
        ld a,c
        cp 2
        jr c,.ldir
.part:
        push bc
        res 0,c
        jr .run
.full:
        push bc
        ld bc,LINK_BULK
.run:

        ld a,b
        rra
        ld a,c
        rra
        dec a
        push bc
        push de
        push hl
        ld bc,TS_DMALEN
        out (c),a
        ld b,high TS_DMANUM
        xor a
        out (c),a
        ld b,high TS_DMASADDRL
        call SpiLink_DmaAddr
        ex de,hl
        ld b,high TS_DMADADDRL
        call SpiLink_DmaAddr
        ld b,high TS_DMACTR
        ld a,TS_DMA_RAM_RAM
        out (c),a
.wait:
        in a,(c)
        rla
        jr c,.wait
        pop hl
        pop de
        pop bc
        add hl,bc
        ex de,hl
        add hl,bc
        ex de,hl
        ex (sp),hl
        or a
        sbc hl,bc
        ld b,h
        ld c,l
        pop hl
        jr .even
.ldir:
        ld a,b
        or c
        ret z
        ldir
        ret






SpiLink_Sweep:
        ld a,i
        push af
        di
        ld (.stack+1),sp
        ld sp,SPI_CACHE_SWEEP
        ld b,LINK_BULK/8
.sweep:
        pop hl
        pop hl
        pop hl
        pop hl
        djnz .sweep
.stack:
        ld sp,0
        pop af
        ret po
        ei
        ret


SpiLink_BulkRun:
        ld bc,TS_DMACTR
        out (c),a
.wait:
        in a,(c)
        rla
        jr c,.wait
        ld a,SPI_SELECT_NONE
        out (ZIFI_SPI_CFG),a
        ret








SpiLink_PutBlock:
        ld (SpiPutXor),a
        ld (SpiBulkBuf),hl
        ld e,LINK_PUT_TRIES
.ask:
        ld a,(SpiPutXor)
        ld (SpiRoom),a
        ld hl,SpiRsp+3
        ld (SpiDest),hl
        ld a,LINK_F_PUT
        push de
        call SpiLink_Turn
        pop de
        ret c
        ld a,(SpiRsp)
        and LINK_S_PUT_READY
        jr z,.mailbox
        ld a,(SpiTxLen)
        or a
        jr nz,.mailbox
        push de
        call SpiLink_BulkEcho
        ld a,LINK_F_PUT_DONE
        call SpiLink_Send
        pop de
        ret c
        ld a,(SpiRsp)
        and LINK_S_PUT_OK
        ret nz
        dec e
        jr nz,.ask
.mailbox:

        ld hl,(SpiBulkBuf)
        ld bc,LINK_BULK
.byte:
        ld a,(hl)
        call ZiFi_PutChar
        ret c
        cpi
        jp pe,.byte
        or a
        ret



SpiLink_BulkRead:
        ld bc,TS_DMADADDRL
        ld a,SPI_CMD_RDDMA
        call SpiLink_BulkOpen
        in a,(ZIFI_SPI_DATA)
        ld a,TS_DMA_SPI_RAM
        call SpiLink_BulkRun
        ld a,SPI_CMD_RD_DONE
        call SpiLink_BulkDone

        jp SpiLink_Sweep
        ENDIF


SpiLink_Open:
        ld c,a
        ld a,SPI_SELECT_ESP
        out (ZIFI_SPI_CFG),a
        ld a,c
        ld c,ZIFI_SPI_DATA
        out (c),a
        out (c),b
        ret



SpiLink_Send:
        ld hl,SpiRsp+3
        ld (SpiDest),hl
        ld hl,SpiRoom
        ld (hl),0






SpiLink_Turn:
        ld hl,SpiReq+2
        ld (hl),a
        dec hl
        inc (hl)
        dec hl
        ld (hl),LINK_REQUEST

        ld a,(SpiTxLen)
        add a,LINK_HEADER
        ld b,a
        xor a
        call SpiLink_Sum
        cpl
        ld (hl),a
        ld a,LINK_ATTEMPTS
.send:
        ld (SpiAttempts),a
        ld a,SPI_CMD_WRBUF
        ld b,0
        call SpiLink_Open
        out (c),b
        ld hl,SpiReq
        ld a,(SpiTxLen)
        add a,LINK_HEADER+1
        ld b,a
        otir
        call SpiLink_Close
        ld de,LINK_POLLS
.poll:
        ld b,0
        call SpiLink_OpenRead
        cp LINK_RESPONSE
        jr nz,.wait
        in a,(c)
        ld hl,SpiReq+1
        cp (hl)
        jr nz,.wait

        ld hl,SpiRsp
        ini
        ini
        ini
        ld a,(SpiRoom)
        ld hl,SpiRsp+1
        cp (hl)
        jr c,.wait
        ld b,(hl)
        inc b
        ld hl,(SpiDest)
        inir
        call SpiLink_Close

        ld hl,SpiRsp
        ld b,3
        ld a,(SpiReq+1)
        add a,LINK_RESPONSE
        call SpiLink_Sum
        ld c,a
        ld a,(SpiRsp+1)
        inc a
        ld b,a
        ld a,c
        ld hl,(SpiDest)
        call SpiLink_Sum
        inc a
        jr nz,.next


        ld a,(SpiRsp)
        IFDEF ZIFI_SPI_DMA
        and LINK_S_MORE|LINK_S_BULK
        ELSE
        and LINK_S_MORE
        ENDIF
        ld (SpiMore),a

        ld hl,SpiRsp+2
        ld c,(hl)
        ld a,(SpiTxLen)
        sub c
        jr nc,.keep
        xor a
.keep:
        ld (SpiTxLen),a
        ret z
        ld b,0
        ld hl,SpiTxBuf
        ld d,h
        ld e,l
        add hl,bc
        ld c,a
        ldir
        or a
        ret

.wait:
        call SpiLink_Close
.next:
        dec de
        ld a,d
        or e
        jp nz,.poll
        ld a,(SpiAttempts)
        dec a
        jp nz,.send


        ld (SpiTxLen),a
        ld (SpiMore),a
        scf
        ret


SpiLink_Sum:
        add a,(hl)
        inc hl
        djnz SpiLink_Sum
        ret


SpiMore:        db 0
        IFDEF ZIFI_SPI_DMA
SpiBulkBuf:     dw 0
SpiPutCan:      db 0
SpiPutXor:      db 0
        ENDIF
SpiAttempts:    db 0
SpiDest:        dw 0
SpiRsp:         ds 4


SpiReq:         ds LINK_HEADER
SpiTxLen        equ SpiReq+3
SpiRoom         equ SpiReq+4
SpiTxBuf:       ds LINK_CHUNK
                db 0

        ENDIF
