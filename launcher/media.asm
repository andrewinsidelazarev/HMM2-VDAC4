; Codex: штатный raw DMA v2, приёмник 31 — активный HMM2.ELF, раньше AudioApp.
; Приём основан на fast_media.asm плеера VDAC4; WC не используется и не меняется.
MediaProbe:
        xor a
        ld (MediaEnabled),a
        ld a,(SpiPutCan)
        or a
        ret z
        ld b,LINK_STATE_ADDR
        call SpiLink_OpenRead
        in a,(c)
        cp #D7
        jr nz,.old
        in a,(c)
        call SpiLink_Close
        cp 2
        ret c
        ld a,1
        ld (MediaEnabled),a
        ret
.old:
        jp SpiLink_Close

; Codex: C000..DFFF страницы 56: seq,offset,count и <=7680 байт.
MediaSend:
        ld bc,(Chunk)
        inc bc
        inc bc
        inc bc
        inc bc
        inc bc
        inc bc
        ld (MediaLength),bc
        ld a,#31
        jr MediaSendCommand

; Codex: штатный MOD_PUT v2: смещение в управляющем пакете, до 8192 байт через DMA.
MediaModuleSend:
        ld bc,(Chunk)
        ld (MediaLength),bc
        ld a,CMD_MOD_PUT
MediaSendCommand:
        ld (MediaCommand),a
        call ZiFi_Flush
        ret c
        ld hl,#C000
        ld (SpiBulkBuf),hl
        ld a,100
        ld (MediaTries),a
.ask:
        ld a,(MediaCommand)
        ld (SpiTxBuf),a
        ld bc,(MediaLength)
        ld (SpiTxBuf+1),bc
        cp CMD_MOD_PUT
        ld a,3
        jr nz,.control
        ld hl,(Offset)
        ld (SpiTxBuf+3),hl
        ld hl,(Offset+2)
        ld (SpiTxBuf+5),hl
        ld a,7
.control:
        ld (SpiTxLen),a
        xor a
        ld (SpiRoom),a
        ld hl,SpiRsp+3
        ld (SpiDest),hl
        ld a,#10
        call SpiLink_Turn
        ret c
        ld a,(SpiRsp)
        and LINK_S_PUT_READY
        jr nz,.ready
        ; Codex: SPG работает с DI; вместо HALT плеера ограниченная пауза ~1 мс.
        ld bc,512
.pause:
        dec bc
        ld a,b
        or c
        jr nz,.pause
        ld hl,MediaTries
        dec (hl)
        jr nz,.ask
        scf
        ret
.ready:
        ld bc,TS_DMASADDRL
        ld a,SPI_CMD_WRDMA
        call SpiLink_BulkOpen
        ld hl,(MediaLength)
        dec hl
        ld a,h
        srl a
        ld bc,TS_DMANUM
        out (c),a
        ld a,TS_DMA_RAM_SPI
        call SpiLink_BulkRun
        ld a,SPI_CMD_WR_DONE
        call SpiLink_BulkDone
        ld a,LINK_F_PUT_DONE
        call SpiLink_Send
        ret c
        ld a,(SpiRsp)
        and LINK_S_PUT_OK
        ret nz
        ; Клод: ESP отказала блоку (PUT_BAD: приём DMA не сошёлся). Блок в очередь ESP не встал, поэтому он
        ; посылается заново с управляющего запроса, как в SpiLink_PutBlock, в пределах того же счёта MediaTries.
        ; Раньше один отказ сразу давал E1 и останавливал игру (сборка 70 на плате, блок музыки, 2026-11-02).
        ld hl,MediaTries
        dec (hl)
        jp nz,.ask
        scf
        ret
MediaEnabled: db 0
MediaCommand: db 0
MediaLength: dw 0
MediaTries: db 0
