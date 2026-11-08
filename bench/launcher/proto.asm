; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.














PROTO_SYNC      equ #5A
PROTO_MAX       equ 1024
PROTO_ERR_TEXT  equ 64

CMD_ECHO        equ #00
CMD_WIFI_INI    equ #03
CMD_PING        equ #04
CMD_GET_STEP    equ #05
CMD_FTP_START   equ #06
CMD_FTP_STOP    equ #07
CMD_UPDATE_START equ #08
CMD_UPDATE_STOP equ #09
CMD_SMB_START   equ #0B
CMD_SMB_STOP    equ #0C
CMD_ONLINE_UPDATE equ #0D
CMD_ONLINE_UPDATE_CHECK equ #0E
CMD_SYS_RESET   equ #0F
CMD_SYS_INFO    equ #02
CMD_NET_IPCONFIG equ #21
CMD_NET_NTP     equ #22
CMD_NET_PROXY_STATUS equ #23
CMD_WEATHER_GET equ #24



CMD_WCU_START   equ #25
CMD_WCU_APPLY   equ #26
CMD_WCU_STOP    equ #27
CMD_WCU_SYNC    equ #28
CMD_SYS_CLOCK   equ #29


CMD_AUDIO_OPEN  equ #30
CMD_AUDIO_DATA  equ #31
CMD_AUDIO_CLOSE equ #32
CMD_AUDIO_STATUS equ #33
CMD_AUDIO_TONE  equ #34
CMD_AUDIO_VOLUME equ #35
CMD_AUDIO_PAUSE equ #36
CMD_AUDIO_PROBE equ #37
CMD_AUDIO_SEEK  equ #38
CMD_AUDIO_FLUSH equ #39
CMD_AUDIO_INFO  equ #3A
CMD_AUDIO_COVER equ #3B
CMD_AUDIO_THUMB equ #3C
CMD_AUDIO_PICTURE equ #3D
CMD_AUDIO_VIEW  equ #2F
CMD_AUDIO_TAGS  equ #3F

CMD_MOD_QUERY   equ #69
CMD_MOD_PUT     equ #6A
CMD_MOD_COMMIT  equ #6B
CMD_APP_START   equ #6C
CMD_APP_STOP    equ #6D


CMD_NET_OPEN    equ #10
CMD_NET_SEND    equ #11
CMD_NET_RECV    equ #12
CMD_NET_CLOSE   equ #13



CMD_NET_HTTP_GET equ #14
CMD_NET_PING    equ #20
EVT_FTP_CLIENT  equ #60
EVT_FTP_COMMAND equ #61
EVT_SMB_CLIENT  equ #62
EVT_SMB_COMMAND equ #63
EVT_SMB_PROGRESS equ #64
EVT_ONLINE_UPDATE_PROGRESS equ #65
EVT_WIFI_SIGNAL equ #66
EVT_WCU_STATE   equ #67
EVT_WCU_ENTRY   equ #68

RESP_SYS_INFO   equ #82
RESP_WIFI_INI   equ #83
RESP_GET_STEP   equ #85
RESP_FTP_START  equ #86
RESP_FTP_STOP   equ #87
RESP_UPDATE_START equ #88
RESP_UPDATE_STOP equ #89
RESP_SMB_START  equ #8B
RESP_SMB_STOP   equ #8C
RESP_ONLINE_UPDATE equ #8D
RESP_ONLINE_UPDATE_CHECK equ #8E
ONLINE_UPDATE_SAME      equ 0
ONLINE_UPDATE_NEWER     equ 1
ONLINE_UPDATE_OLDER     equ 2
ONLINE_UPDATE_DIFFERENT equ 3
RESP_NET_IPCONFIG equ #A0
RESP_NET_PING   equ #A1
RESP_NET_NTP    equ #A2
RESP_NET_PROXY_STATUS equ #A3
RESP_WEATHER    equ #A4
RESP_WCU_START  equ #A5
RESP_WCU_APPLY  equ #A6
RESP_WCU_STOP   equ #A7
RESP_WCU_SYNC   equ #A8
RESP_SYS_CLOCK  equ #A9
RESP_AUDIO_OPEN equ #B0
RESP_AUDIO_DATA equ #B1
RESP_AUDIO_CLOSE equ #B2
RESP_AUDIO_STATUS equ #B3
RESP_AUDIO_TONE equ #B4
RESP_AUDIO_VOLUME equ #B5
RESP_AUDIO_PAUSE equ #B6
RESP_AUDIO_PROBE equ #B7
RESP_AUDIO_SEEK equ #B8
RESP_AUDIO_FLUSH equ #B9
RESP_AUDIO_INFO equ #BA
RESP_AUDIO_COVER equ #BB
RESP_AUDIO_THUMB equ #BC
RESP_AUDIO_PICTURE equ #BD
RESP_AUDIO_VIEW equ #AF
RESP_AUDIO_TAGS equ #BF
RESP_MOD_QUERY  equ #E9
RESP_MOD_PUT    equ #EA
RESP_MOD_COMMIT equ #EB
RESP_APP_START  equ #EC
RESP_APP_STOP   equ #ED
RESP_NET_OPEN   equ #90
RESP_NET_SEND   equ #91


RESP_NET_RECV   equ #92
RESP_NET_CLOSE  equ #93

RESP_NET_HTTP_GET equ #94
RESP_READY      equ #F0
RESP_ERROR      equ #EE
RESP_ACK        equ #FE





Proto_Send:
        IFDEF ZIFI_SPI_DMA
        ld de,0
        ld (ProtoTailLen),de
        ld e,a
        xor a
        ld (ProtoHold),a
        ld a,e




Proto_Send2:
        ENDIF
        ld (ProtoCmd),a
        ld (ProtoPtr),hl
        ld (ProtoLen),bc
        xor a
        ld (ProtoCsum),a

        ld a,PROTO_SYNC
        call ZiFi_PutChar
        ret c
        ld a,(ProtoCmd)
        call Proto_PutSum
        ret c
        ld hl,(ProtoLen)
        IFDEF ZIFI_SPI_DMA
        ld de,(ProtoTailLen)
        add hl,de
        ENDIF
        ld a,l
        call Proto_PutSum
        ret c
        ld a,h
        call Proto_PutSum
        ret c

        ld hl,(ProtoPtr)
        ld bc,(ProtoLen)
        call Proto_Body
        ret c
        IFDEF ZIFI_SPI_DMA
        ld hl,(ProtoTail)
        ld bc,(ProtoTailLen)
        call Proto_Body
        ret c
        ENDIF
        ld a,(ProtoCsum)
        call ZiFi_PutChar
        ret c
        IFDEF ZIFI_SPI_DMA
        ld a,(ProtoHold)
        or a
        ret nz
        ENDIF


        jp ZiFi_Flush



Proto_Body:
        IFDEF ZIFI_SPI



.body:
        ld a,b
        or c
        ret z
        IFDEF ZIFI_SPI_DMA


        ld a,(SpiPutCan)
        or a
        jr z,.mailbox
        bit 7,h
        jr z,.mailbox
        ld a,b
        cp high LINK_BULK
        jr c,.mailbox
        bit 0,l
        jr z,.direct

        cp high LINK_BULK
        jr nz,.one
        ld a,c
        or a
        jr z,.mailbox
.one:
        ld a,(hl)
        call Proto_PutSum
        ret c
        inc hl
        dec bc
        jr .body
.direct:
        push bc
        push hl
        ld bc,LINK_BULK
        xor a
        call Proto_XorRun
        ld e,a
        ld a,(ProtoCsum)
        xor e
        ld (ProtoCsum),a
        ex (sp),hl
        ld a,e
        call SpiLink_PutBlock
        pop hl
        pop bc
        ret c
        push hl
        ld hl,-LINK_BULK
        add hl,bc
        ld b,h
        ld c,l
        pop hl
        jr .body
.mailbox:
        ENDIF
        ld a,(SpiTxLen)
        cp LINK_CHUNK
        jr c,.room
        push hl
        push bc
        ld c,LINK_CHUNK
        call SpiLink_Push
        pop bc
        pop hl
        ret c
        jr .body
.room:
        ld e,a
        ld d,0
        push hl
        ld hl,SpiTxBuf
        add hl,de
        ex de,hl
        ld a,LINK_CHUNK
        sub l
        pop hl
        inc b
        dec b
        jr nz,.take
        cp c
        jr c,.take
        ld a,c
.take:
        push bc
        ld c,a
        ld b,0
        push bc
        ld a,(ProtoCsum)
.copy:
        xor (hl)
        ldi
        jp pe,.copy
        ld (ProtoCsum),a
        pop bc
        ld a,(SpiTxLen)
        add a,c
        ld (SpiTxLen),a
        ex (sp),hl
        or a
        sbc hl,bc
        ld b,h
        ld c,l
        pop hl
        jp .body
        ELSE
.body:
        ld a,b
        or c
        ret z
        ld a,(hl)
        push hl
        push bc
        call Proto_PutSum
        pop bc
        pop hl
        ret c
        inc hl
        dec bc
        jr .body
        ENDIF

        IFDEF ZIFI_SPI_DMA



Proto_XorRun:
        push bc
        push de
        ld d,a
        ld a,c
        and 7
        ld e,a
        srl b
        rr c
        srl b
        rr c
        srl b
        rr c
        ld b,c
        ld a,d
        inc b
        dec b
        jr z,.tail
.eight:
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        xor (hl)
        inc hl
        djnz .eight
.tail:
        ld b,e
        inc b
        dec b
        jr z,.done
.one:
        xor (hl)
        inc hl
        djnz .one
.done:
        pop de
        pop bc
        ret
        ENDIF


Proto_PutSum:
        ld e,a
        ld a,(ProtoCsum)
        xor e
        ld (ProtoCsum),a
        ld a,e
        jp ZiFi_PutChar


Proto_SendEmpty:
        ld hl,0
        ld bc,0
        jp Proto_Send








Proto_Poll:
        IFDEF ZIFI_SPI


        ld a,(ProtoState)
        cp 4
        jp nz,.byte
        ld bc,(ProtoBurstLeft)
        ld a,b
        or c
        jr nz,.block
        ld hl,ProtoBurstBuf
        ld (ProtoBurstPtr),hl
        call ZiFi_ReadBurst
        ret z
        ld (ProtoBurstLeft),bc
.block:

        ld hl,(ProtoRxLen)
        ld de,(ProtoRxCount)
        or a
        sbc hl,de
        or a
        sbc hl,bc
        jr nc,.take
        add hl,bc
        ld b,h
        ld c,l
.take:

        ld hl,(ProtoBurstLeft)
        or a
        sbc hl,bc
        ld (ProtoBurstLeft),hl
        ex de,hl
        add hl,bc
        ld (ProtoRxCount),hl
        ld hl,(ProtoBurstPtr)
        ld de,(ProtoRxPtr)
        IFDEF ZIFI_SPI_DMA



        ld a,b
        or a
        jr nz,.long
        ld a,c
        cp PROTO_DMA_MIN
        jr c,.short
.long:
        ld a,l
        xor e
        rra
        jr c,.short
        ld a,(ProtoCsumTmp)
        call Proto_XorRun
        ld (ProtoCsumTmp),a
        ld hl,(ProtoBurstPtr)
        call SpiLink_Copy
        ld a,1
        ld (ProtoDmaUsed),a
        jr .copied
.short:
        ENDIF
        ld a,(ProtoCsumTmp)
.copy:
        xor (hl)
        ldi
        jp pe,.copy
        ld (ProtoCsumTmp),a
.copied:
        ld (ProtoBurstPtr),hl
        ld (ProtoRxPtr),de
        ld hl,(ProtoRxCount)
        ld de,(ProtoRxLen)
        or a
        sbc hl,de
        jp nz,Proto_Poll
        IFDEF ZIFI_SPI_DMA

        ld a,(ProtoDmaUsed)
        or a
        jr z,.fresh
        xor a
        ld (ProtoDmaUsed),a
        call SpiLink_Sweep
.fresh:
        ENDIF
        ld a,5
        ld (ProtoState),a
.byte:
        ENDIF
        call Proto_GetByte
        ret nc
        ld e,a
        ld a,(ProtoState)

        or a
        jr nz,.st1
        ld a,e
        cp PROTO_SYNC
        jp nz,Proto_Poll
        ld a,1
        ld (ProtoState),a
        jp Proto_Poll

.st1:
        dec a
        jr nz,.st2
        ld a,e
        ld (ProtoRxCmd),a
        xor a
        ld (ProtoCsumTmp),a
        ld a,(ProtoRxCmd)
        ld (ProtoCsumTmp),a
        ld a,2
        ld (ProtoState),a
        jp Proto_Poll

.st2:
        dec a
        jr nz,.st3
        ld a,e
        ld (ProtoRxLen),a
        call Proto_AddCsum
        ld a,3
        ld (ProtoState),a
        jp Proto_Poll

.st3:
        dec a
        jr nz,.st4
        ld a,e
        ld (ProtoRxLen+1),a
        call Proto_AddCsum
        ld hl,ProtoBuf
        ld (ProtoRxPtr),hl
        ld hl,0
        ld (ProtoRxCount),hl

        ld hl,(ProtoRxLen)
        ld bc,PROTO_MAX
        or a
        sbc hl,bc
        jr z,.len_ok
        jr c,.len_ok
        call Proto_ResetRx
        jp Proto_Poll
.len_ok:
        ld hl,(ProtoRxLen)
        ld a,h
        or l
        ld a,4
        jr nz,.set
        ld a,5
.set:
        ld (ProtoState),a
        jp Proto_Poll

.st4:
        IFNDEF ZIFI_SPI
        dec a
        jr nz,.st5
        ld hl,(ProtoRxPtr)
        ld (hl),e
        inc hl
        ld (ProtoRxPtr),hl
        ld a,e
        call Proto_AddCsum
        ld hl,(ProtoRxCount)
        inc hl
        ld (ProtoRxCount),hl
        ld bc,(ProtoRxLen)
        ld a,h
        cp b
        jp nz,Proto_Poll
        ld a,l
        cp c
        jp nz,Proto_Poll
        ld a,5
        ld (ProtoState),a
        jp Proto_Poll
        ENDIF

.st5:
        ld a,(ProtoCsumTmp)
        ld d,a
        call Proto_ResetRx
        ld a,e
        cp d
        jp nz,Proto_Poll
        ld a,(ProtoRxCmd)
        scf
        ret




Proto_GetByte:
        IFDEF ZIFI_SPI

        ld hl,(ProtoBurstLeft)
        ld a,h
        or l
        jr nz,.have
        ld hl,ProtoBurstBuf
        ld (ProtoBurstPtr),hl
        call ZiFi_ReadBurst
        ret z
        ld h,b
        ld l,c
.have:
        dec hl
        ld (ProtoBurstLeft),hl
        ld hl,(ProtoBurstPtr)
        ld a,(hl)
        inc hl
        ld (ProtoBurstPtr),hl
        scf
        ret
        ELSE
        ld a,(ProtoBurstLeft)
        or a
        jr nz,.have
        ld hl,ProtoBurstBuf
        call ZiFi_ReadBurst
        or a
        ret z
        ld (ProtoBurstLeft),a
        ld hl,ProtoBurstBuf
        ld (ProtoBurstPtr),hl
.have:
        ld hl,(ProtoBurstPtr)
        ld a,(hl)
        inc hl
        ld (ProtoBurstPtr),hl
        ld hl,ProtoBurstLeft
        dec (hl)
        scf
        ret
        ENDIF


Proto_AddCsum:
        ld hl,ProtoCsumTmp
        xor (hl)
        ld (hl),a
        ret


Proto_ResetRx:
        xor a
        ld (ProtoState),a
        ld hl,0
        ld (ProtoRxCount),hl
        ret






Proto_WaitCmd:
        ld (ProtoWant),a
.loop:
        push de
        call Proto_Poll
        pop de
        jr nc,.idle
        ld hl,ProtoWant
        cp (hl)
        jr z,.got
        cp RESP_ERROR
        jr nz,.idle
        ld a,1
        ld (ProtoErr),a
        call Proto_SaveErr
.idle:


        ld a,e
        and 63
        jr nz,.count
        push de
        call FtTick
        pop de
.count:
        dec de
        ld a,d
        or e
        jr nz,.loop
        scf
        ret
.got:
        or a
        ret


Proto_SaveErr:
        ld hl,ProtoBuf
        ld de,ProtoErrText
        ld bc,(ProtoRxLen)




        ld a,b
        or a
        jr nz,.clamp
        ld a,c
        cp PROTO_ERR_TEXT-1
        jr c,.check
.clamp:
        ld bc,PROTO_ERR_TEXT-1
.check:
        ld a,b
        or c
        jr z,.term
.copy:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,.copy
.term:
        xor a
        ld (de),a
        ret




Net_ProxyStatus:
        ld hl,0
        ld bc,0
        ld a,CMD_NET_PROXY_STATUS
        call Proto_Send
        ld de,10000
        ld a,RESP_NET_PROXY_STATUS
        call Proto_WaitCmd
        ret c
        ld hl,(ProtoRxLen)
        ld a,h
        or l
        scf
        ret z
        ld bc,ProtoBuf
        add hl,bc
        ld (hl),0
        ld a,(ProtoBuf)
        ld de,ProtoBuf+1
        or a
        ret



ProtoCmd:       db 0
ProtoPtr:       dw 0
ProtoLen:       dw 0
ProtoCsum:      db 0
        IFDEF ZIFI_SPI_DMA
ProtoTail:      dw 0
ProtoTailLen:   dw 0
ProtoHold:      db 0
ProtoDmaUsed:   db 0


PROTO_DMA_MIN   equ 64
        ENDIF

ProtoState:     db 0
ProtoRxCmd:     db 0
ProtoRxLen:     dw 0
ProtoRxPtr:     dw 0
ProtoRxCount:   dw 0
ProtoCsumTmp:   db 0
ProtoWant:      db 0
ProtoErr:       db 0
ProtoBurstPtr:  dw ProtoBurstBuf
        IFDEF ZIFI_SPI
ProtoBurstLeft: dw 0
        ELSE
ProtoBurstLeft: db 0
        ENDIF
ProtoErrText:   ds PROTO_ERR_TEXT



        IFDEF ZIFI_SPI
        IFDEF ZIFI_SPI_DMA
        ALIGN 2
        ENDIF
ProtoBurstBuf:  ds ZIFI_BURST_SIZE
        ELSE
ProtoBurstBuf:  ds #BF
        ENDIF





ProtoBuf:       ds PROTO_MAX+1
