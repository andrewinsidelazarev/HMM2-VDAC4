; Codex: E6 [seq, DAT offset, RAM_G address, length] -> 77 [seq,status].
; Только готовые PALETTED8, выравнивание 512; данные не проходят через ESP.
ServeGraphics:
        ld hl,(ProtoRxLen)
        ld de,12
        or a
        sbc hl,de
        jp nz,Fail
        ld hl,ProtoBuf
        ld de,GraphicsRequest
        ld bc,12
        ldir
        ld a,1
        ld (GraphicsReply+2),a
        ld hl,(GraphicsRequest)
        ld (GraphicsReply),hl
        ld a,(WadMapped)
        or a
        jp z,.reply
        ; Codex: проверяем длину и границу RAM_G до первого DMA.
        ld hl,(GraphicsLeft)
        ld a,h
        or l
        jp z,.reply
        ld a,l
        or a
        jp nz,.reply
        bit 0,h
        jp nz,.reply
        ld a,(GraphicsSource)
        ld b,a
        ld a,(GraphicsSource+1)
        and 1
        or b
        jp nz,.reply
        ld de,(GraphicsTarget)
        add hl,de
        ld hl,(GraphicsTarget+2)
        ld de,0
        adc hl,de
        jp c,.reply
        ld de,#30
        or a
        sbc hl,de
        jr c,.next
        jp nz,.reply
        ld hl,(GraphicsTarget)
        ld de,(GraphicsLeft)
        add hl,de
        ld a,h
        or l
        jp nz,.reply
.next:
        ld hl,(GraphicsLeft)
        ld de,8192
        or a
        sbc hl,de
        jr nc,.length
        ld de,(GraphicsLeft)
.length:
        ld (Chunk),de
        ; Клод: атлас читается один раз и сразу уходит в RAM_G: с SD прямо в страницу 56, мимо
        ; кольца, без лишней копии. Границы проверяет ELF (hmm_remote_graphics), кратность 512 — выше.
        ld a,d
        srl a
        ld (FastFillLeft),a
        ld hl,(GraphicsSource)
        ld de,(GraphicsSource+2)
        call FastSectorOf
        call FastFillPacket
        jr nz,.restore
        call FtOn
        ld a,(GraphicsTarget+2)
        or #80
        out (#57),a
        ld a,(GraphicsTarget+1)
        out (#57),a
        ld a,(GraphicsTarget)
        out (#57),a
        ld hl,#C000
        ld bc,TS_DMASADDRL
        call SpiLink_DmaAddr
        ld b,high TS_DMALEN
        ld a,255
        out (c),a
        ld hl,(Chunk)
        ld a,h
        srl a
        dec a
        ld b,high TS_DMANUM
        out (c),a
        ld b,high TS_DMACTR
        ld a,TS_DMA_RAM_SPI
        out (c),a
.wait:
        in a,(c)
        rla
        jr c,.wait
        call FtOff
        ld hl,(GraphicsSource)
        ld de,(Chunk)
        add hl,de
        ld (GraphicsSource),hl
        jr nc,.source
        ld hl,(GraphicsSource+2)
        inc hl
        ld (GraphicsSource+2),hl
.source:
        ld hl,(GraphicsTarget)
        add hl,de
        ld (GraphicsTarget),hl
        jr nc,.target
        ld hl,(GraphicsTarget+2)
        inc hl
        ld (GraphicsTarget+2),hl
.target:
        ld hl,(GraphicsLeft)
        or a
        sbc hl,de
        ld (GraphicsLeft),hl
        ld a,h
        or l
        jp nz,.next
        ld (GraphicsReply+2),a
.restore:
        ld a,32
        call FastMap
.reply:
        ld hl,GraphicsReply
        ld bc,3
        ld a,#77
        call Proto_Send
        ld a,#F7
        ld b,16
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        xor a
        ret
GraphicsRequest:
        dw 0
GraphicsSource: dd 0
GraphicsTarget: dd 0
GraphicsLeft: dw 0
GraphicsReply: ds 3
