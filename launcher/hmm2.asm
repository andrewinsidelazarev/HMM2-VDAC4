; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.
; SPDX-License-Identifier: GPL-2.0-or-later




        DEVICE ZXSPECTRUM128
        DEFINE ZIFI_SPI
        IFNDEF HSD_NO_DMA
        DEFINE ZIFI_SPI_DMA
        ENDIF
        INCLUDE "fat32.inc"
        INCLUDE "filex.inc"
        ORG #8000
Start:
        di
        ld sp,#BFF0
        ; Клод: старт как у DOOM VDAC4 и исправления 3, проверенных на плате: частоту задаёт
        ; заголовок SPG (Clock = 2), кэш TS-Conf (#2BAF) не включаем. Сборка 28.10 с явными
        ; 14 МГц и кэшем #06 на плате не прошла этапы 3–4 (SD, FAT32) и встала в Halt.
        ld bc,#21AF
        ld a,#0E
        out (c),a
        ld bc,#10AF
        ld a,#0F
        out (c),a
        inc b
        ld a,#0E
        out (c),a
        ld bc,#13AF
        ld a,#20
        out (c),a
        xor a
        out (#FE),a
        ld a,1
        call BootMark
        call ZiFi_Init
        jp c,Fail
        call MediaProbe

        ld a,2
        call BootMark
        call StopApp
        jp c,StopFailed

        call FtScreen_Start
        ; Клод: сразу картинка и полоса, как в DOOM VDAC4; надписи — только на экране ошибки.
        call FtPicture
        ld a,3
        call BootMark
        ld hl,#3900
        xor a
        call FAT32_BIND
        call FAT32_DEVICE_INIT
        jp nz,Fail
        ld a,4
        call BootMark
        call FAT32_MOUNT
        jp nz,Fail
        ld hl,FT_AT_MOUNT
        call FtProgress
        ld a,5
        call BootMark
        call PathCfgLoad
        jr nz,.located
        ld a,4
        ld (FtGap),a
        call LocateGame
        push af
        ld a,2
        ld (FtGap),a
        pop af
        jp c,Fail
        call PathCfgSave
.located:
        jp LoadGameModule
LoadAudioModule:
        ; Codex: игровой ELF уже запущен и защищён от вытеснения. OGG проверяется
        ; после его записи: MOD_QUERY существующего файла не обновляет LRU кэша.
        call FAT32_SET_ROOT
        ld hl,AudioWcSpec
        call FAT32_FIND
        jp c,AudioMissing
        jp z,AudioMissing
        call FAT32_SET_DIR
        jp c,AudioMissing
        ld hl,AudioEspSpec
        call FAT32_FIND
        jp c,AudioMissing
        jp z,AudioMissing
        call FAT32_SET_DIR
        jp c,AudioMissing
        ld hl,AudioSpec
        call FAT32_FIND
        jp c,AudioMissing
        jp z,AudioMissing
        push hl
        push de
        ld a,1
        ld (LoadingAudio),a
        ld hl,AudioSpec+1
        ld de,Query+9
        ld bc,8
        ldir
        ld hl,17
        ld (QueryBytes),hl
        pop de
        pop hl
        jr FoundModule
LoadGameModule:
        xor a
        ld (LoadingAudio),a
        ld hl,GameDir
        call FAT32_SET_DIR_CLUSTER
        ld hl,ElfSpec+1
        ld de,Query+9
        ld bc,9
        ldir
        ld hl,18
        ld (QueryBytes),hl
        ld hl,FT_AT_GAME
        call FtProgress
        ld a,6
        call BootMark
        ld hl,ElfSpec
        call FAT32_FIND
        jp c,FileFindError
        jp z,Fail
FoundModule:
        push hl
        ld hl,0
        ld (Offset),hl
        ld (Offset+2),hl
        pop hl
        ld (ElfSize),hl
        ld (ElfSize+2),de
        ld (Query+1),hl
        ld (Query+3),de



        ld hl,#3868+22
        ld de,Query+5
        ld bc,4
        ldir
        ; Codex: OGG мал и читается FILEX; таблица LBA DAT уже построена и сохраняется.
        ld a,(LoadingAudio)
        or a
        jr nz,.mapped
        call FastOpen
        jr z,.mapped
        ld hl,MsgReadError
        ld (BootMessage),hl
        jp Fail
.mapped:
        ; Клод: до загрузки ELF — APP_STOP, как плагин плеера (Esp_StopApp): ESP выгружает приложение, которое держит её
        ; память (PLAYER с декодерами, брошенный HMM2), и отдаёт всё, что оно занимало. Приложения нет — ESP просто отвечает;
        ; отказ — не ошибка. 04.11 игра не стартовала после ядра MD: STAGE 08 CODE 00 (журнал ESP: HMM2-E0800).
        ; Только перед HMM2.ELF: этой же веткой после APP_START игры грузится OGG.ELF (LoadingAudio) — там APP_STOP выгрузил бы игру.
        ld a,(LoadingAudio)
        or a
        call z,StopApp
        ld hl,Query
        ld a,7
        call BootMark
        ld bc,(QueryBytes)
        ld a,CMD_MOD_QUERY
        call Proto_Send
        ld a,RESP_MOD_QUERY
        ld b,16
        call WaitLong
        jp c,Fail
        ld hl,FT_AT_QUERY
        call FtProgress
        ld a,(ProtoBuf)
        or a
        jp z,Uploaded
        cp 1
        jp nz,QueryFailed
Upload:
        ld a,8
        call BootMark

        ld hl,(ElfSize)
        ld de,(Offset)
        or a
        sbc hl,de
        push hl
        ld hl,(ElfSize+2)
        ld de,(Offset+2)
        sbc hl,de
        ld a,h
        or l
        pop hl
        ld bc,1016
        push af
        call ModuleFast
        or a
        jr z,.limit
        ld bc,8192
.limit:
        pop af
        jr nz,.length
        ld a,h
        or l
        jp z,Commit
        push hl
        or a
        sbc hl,bc
        pop hl
        jr nc,.length
        ld b,h
        ld c,l
.length:
        ld (Chunk),bc
        ld (Fx+10),bc
        ld hl,(Offset)
        ld (Fx+4),hl
        ld (PutBuf),hl
        ld hl,(Offset+2)
        ld (Fx+6),hl
        ld (PutBuf+2),hl
        ld hl,PutBuf+4
        call ModuleFast
        or a
        jr z,.buffer
        ld a,56
        call FastMap
        ld hl,#C000
.buffer:
        ld (Fx+8),hl
        call ReadAt
        jp nz,Fail
        call ModuleFast
        or a
        jr z,.legacy
        call MediaModuleSend
        jr nc,.media_sent
        ; Клод: быстрая передача куска не пошла (ESP не дал PUT_READY) — модуль дальше проверенным MOD_PUT по 1016 байт с того
        ; же смещения, как плагин плеера шлёт ELF всегда. Прежде — сразу STAGE 08 CODE 00, и игра не стартовала.
        ld a,32
        call FastMap
        ld a,1
        ld (ModuleLegacy),a
        jp Upload
.media_sent:
        ld a,32
        call FastMap
        jr .reply
.legacy:
        ld hl,PutBuf
        ld bc,(Chunk)
        inc bc
        inc bc
        inc bc
        inc bc
        ld a,CMD_MOD_PUT
        call Proto_Send
.reply:
        ld a,RESP_MOD_PUT
        ld b,16
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld hl,(Offset)
        ld bc,(Chunk)
        add hl,bc
        ld (Offset),hl
        ld hl,(Offset+2)
        ld bc,0
        adc hl,bc
        ld (Offset+2),hl
        call FtUpload
        jp Upload
Commit:
        ld a,9
        call BootMark
        ld a,CMD_MOD_COMMIT
        call Proto_SendEmpty
        ld a,RESP_MOD_COMMIT
        ld b,64
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld hl,FT_AT_COMMIT
        call FtProgress
Uploaded:
        ld a,(LoadingAudio)
        or a
        jp nz,ConfigureGame
        ld hl,FT_AT_WAD
        call FtProgress

        call ChooseWad
        jr nz,.wad
        ld hl,MsgMissingWad
        ld (BootMessage),hl
        ld a,#F1
        jp Fail
.wad:
        ld a,10
        call BootMark

        call FtHandover

        ld bc,#00AF
        ld a,4
        out (c),a
        ld hl,ElfSpec+1
        ld bc,9
        ld a,CMD_APP_START
        call Proto_Send
        ld a,RESP_APP_START
        ld b,32
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        or a
        jr z,.app_started
        ; Codex: отказ APP_START допускает ровно один перезапуск ESP за запуск SPG.
        ld hl,EspResetTried
        bit 0,(hl)
        jp z,ResetEspForStart
        ld hl,MsgStart
        cp #FE
        jr nz,.start_error
        ld hl,MsgEspMemory
.start_error:
        ld (BootMessage),hl
        jp Fail
.app_started:
        ld a,1
        ld (AppStarted),a
        ld a,11
        call BootMark
        ; Codex: календарь RTC Ево передаётся до main, отдельно от монотонных часов ESP.
        call RtcRead
        ld hl,RtcBuffer
        ld bc,8
        ld a,#72
        call Proto_Send
        ld a,#F2
        ld b,8
        call WaitLong
        jp LoadAudioModule
ConfigureGame:
        ; Codex: OGG готов; только теперь запускаем main. Долгая таблица DAT
        ; была построена до APP_START, без ожидающего ELF во время сброса Ево.
        xor a
        ld (LoadingAudio),a
        ld hl,GameDir
        call FAT32_SET_DIR_CLUSTER
        ld hl,WadConfig
        ld bc,(ConfigLength)
        ld a,#70
        call Proto_Send
        ld a,#F0
        ld b,8
        call WaitLong
        jp c,Fail
        ld a,(ProtoBuf)
        cp 1
        jp nz,Fail
        ld a,12
        call BootMark
Run:
        call Proto_Poll
        jr nc,.idle
        cp #F5
        call z,KeysAcknowledged
        cp #E4
        call z,ServeRead
        cp #E5
        call z,ServeFile
        cp #E6
        call z,ServeGraphics
        cp #E7
        call z,ServeMusic
        cp #F3
        jr nz,.keys
        xor a
        ld (StatusPending),a
        ld a,(ProtoBuf)
        cp #FF
        jp z,AppFailed
        cp 3
        jp z,Stop
        jr .keys
.idle:
        ; Клод: только когда от ESP ничего нет — Z80 читает HMM2.DAT вперёд
        ; в кольцо 2 МиБ; поток запросов подряд не задерживается.
        call FastIdle
.keys:
        ld hl,KeyDivider
        dec (hl)
        jr nz,Run
        call SendKeys
        jp Run

AppFailed:
        call CaptureAppError
        ld a,#FF
        jp Fail









PS2_DRAIN       equ 24
; Клод: сколько вызовов SendKeys (раз в 256 проходов Run) ждать F5 и сколько опросов статуса — F3.
KEYS_WAIT       equ 4
STATUS_WAIT     equ 4
SendKeys:
        ; Codex: максимум один пакет ввода до F5. Иначе быстрый Z80 заполняет
        ; входное кольцо ESP, и ответ SD ждёт за сотнями пакетов 75.
        ; Клод: F5 ждём не дольше KEYS_WAIT вызовов, потом пакет ввода посылается заново. На плате
        ; в игре F5 терялось: счётчик команд ESP замирал, ввод и опрос статуса прекращались навсегда
        ; (мышь в игре не двигалась, 28.10). Лишний пакет 75 ESP просто примет.
        ld a,(KeysPending)
        or a
        jr z,.fresh
        dec a
        ld (KeysPending),a
        ret nz
.fresh:
        ld a,KEYS_WAIT
        ld (KeysPending),a
        ld hl,KeyBuf
        in a,(#1F)
        cp #FF
        jr nz,.kempston
        xor a
.kempston:
        and 31
        ld (hl),a





        inc hl
        ld bc,#FADF
        in a,(c)
        ld (hl),a
        inc hl
        inc b
        in a,(c)
        ld (hl),a
        inc hl
        ld b,#FF
        in a,(c)
        ld (hl),a
        inc hl
        call Ps2Open
        ld e,PS2_DRAIN
.drain:
        in a,(c)
        or a
        jr z,.send
        ld (hl),a
        inc hl
        dec e
        jr nz,.drain
.send:
        ld de,KeyBuf
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ld hl,KeyBuf
        ld a,#75
        call Proto_Send
        jp c,Fail


        ld hl,StatusDivider
        dec (hl)
        ret nz
        ld (hl),32
        ; Клод: ответ F3 на опрос статуса ждём не дольше STATUS_WAIT опросов, потом спрашиваем снова.
        ld a,(StatusPending)
        or a
        jr z,.status
        dec a
        ld (StatusPending),a
        ret nz
.status:
        ld a,STATUS_WAIT
        ld (StatusPending),a
        ld a,#73
        jp Proto_SendEmpty

KeysAcknowledged:
        push af
        xor a
        ld (KeysPending),a
        pop af
        ret



Ps2Open:
        ld bc,#EFF7
        ld a,#80
        out (c),a
        ld a,(Ps2Ready)
        or a
        jr nz,.queue
        inc a
        ld (Ps2Ready),a
        ld b,#DF
        ld a,#0C
        out (c),a
        ld b,#BF
        ld a,1
        out (c),a
        ld b,#DF
        ld a,#F0
        out (c),a
        ld b,#BF
        ld a,2
        out (c),a
.queue:
        ld b,#DF
        ld a,#F0
        out (c),a
        ld b,#BF
        ret



ServeRead:
        ld hl,(ProtoRxLen)
        ld de,8
        or a
        sbc hl,de
        jp nz,Fail
        ld a,#E4
        call RememberRequest
        ld a,56
        call FastMap
        ld hl,(ProtoBuf)
        ld (PutBuf),hl
        ld hl,(ProtoBuf+2)
        ld (Fx+4),hl
        ld hl,(ProtoBuf+4)
        ld (Fx+6),hl
        ld hl,(ProtoBuf+6)
        ld (StreamLeft),hl
        ld de,32769
        or a
        sbc hl,de
        jp nc,Fail
        ld hl,0
        ld (StreamAt),hl
        ld hl,(StreamLeft)
        ld de,1017
        or a
        sbc hl,de
        ld a,0
        jr c,.mode
        inc a
.mode:
        ld (StreamMode),a
        or a
        jr z,.plain
        ld a,(MediaEnabled)
        or a
        jr z,.plain
        ld a,2
        ld (StreamMode),a
.plain:
        ld a,(WadSelected)
        or a
        jr nz,.read
        ; Клод: таблица кластеров DAT уже построена — FastReadAt не зависит от
        ; текущего файла драйвера. Повторный FAT32_FIND после каждого E5 стоил ~3 с.
        ld a,(WadMapped)
        or a
        jr nz,.read
        ; Codex: редкий путь без LBA-таблицы меняет выбранный файл драйвера.
        call FileClose
        jp nz,.bad
        ld hl,WadFind
        call FAT32_FIND
        jr c,.bad
        jr z,.bad
        ld a,(WadMapped)
        or a
        jr nz,.selected
        call FastOpen
        jr nz,.bad
        ld a,1
        ld (WadMapped),a
.selected:
        ld a,1
        ld (WadSelected),a
.read:
        ; Codex: весь E4 обслуживается потоком; полного круга E4/F1 на 1 КиБ нет.
        ; Заголовок большого ответа включает смещение для проверки порядка на ESP.
        ld hl,(StreamLeft)
        ld de,1014
        ld a,(StreamMode)
        cp 2
        jr nz,.legacy_limit
        ld de,7680
        jr .limit
.legacy_limit:
        or a
        jr nz,.limit
        ld de,1016
.limit:
        push hl
        or a
        sbc hl,de
        pop hl
        jr c,.length
        ex de,hl
.length:
        xor a
        ld (PacketMedia),a
        ld a,(StreamMode)
        cp 2
        jr nz,.chosen
        ld a,(Fx+4)
        and 1
        jr z,.aligned
        ; Codex: нечётное начало — один байт обычным кадром, затем выровненный DMA.
        ld hl,1
        jr .chosen
.aligned:
        ld a,1
        ld (PacketMedia),a
.chosen:
        ld (Chunk),hl
        ld (Fx+10),hl
        ld hl,PutBuf+4
        ld a,(StreamMode)
        or a
        jr z,.dest
        inc hl
        inc hl
.dest:
        ld (Fx+8),hl
        ld a,(PacketMedia)
        or a
        jr z,.load
        ld a,56
        call FastMap
        ld hl,#C006
        ld (Fx+8),hl
.load:
        call ReadAt
        jr z,.ok
.bad:
        ld hl,0
        jr .answer
.ok:
        ld a,(PacketMedia)
        or a
        jr z,.ordinary
        ld hl,(PutBuf)
        ld (#C000),hl
        ld hl,(StreamAt)
        ld (#C002),hl
        ld hl,(Chunk)
        ld (#C004),hl
        call MediaSend
        push af
        ld a,32
        call FastMap
        pop af
        jp c,FileReplyFailed
        jr .advance
.ordinary:
        ld hl,(Chunk)
.answer:
        ld (PutBuf+2),hl
        ld b,h
        ld c,l
        ld a,(StreamMode)
        or a
        jr z,.header
        ld (PutBuf+4),hl
        ld hl,(StreamAt)
        ld (PutBuf+2),hl
        inc bc
        inc bc
.header:
        inc bc
        inc bc
        inc bc
        inc bc
        ld hl,PutBuf
        ld a,#71
        call Proto_Send
        jp c,FileReplyFailed
        ld a,(StreamMode)
        or a
        ret z
        ld hl,(PutBuf+4)
        ld a,h
        or l
        ret z
.advance:
        ld de,(Chunk)
        ld hl,(StreamAt)
        add hl,de
        ld (StreamAt),hl
        ld hl,(Fx+4)
        add hl,de
        ld (Fx+4),hl
        ld hl,(Fx+6)
        ld de,0
        adc hl,de
        ld (Fx+6),hl
        ld hl,(StreamLeft)
        ld de,(Chunk)
        or a
        sbc hl,de
        ld (StreamLeft),hl
        ld a,h
        or l
        jp nz,.read
        xor a
        ret

StreamLeft: dw 0
StreamAt: dw 0
StreamMode: db 0
PacketMedia: db 0
WadMapped: db 0

FileReplyFailed:
        ld hl,MsgFileReply
        ld (BootMessage),hl
        ld a,#E1
        jp Fail

ReadAt:
        ; Codex: только короткая доставка OGG использует FILEX; все чтения игры
        ; по-прежнему идут по готовой таблице LBA, без обхода FAT.
        ld a,(LoadingAudio)
        or a
        jp z,FastReadAt
        ld hl,Fx
        jp FAT32_FILEX

FindWad:
        push hl
        call FAT32_FIND
        pop bc
        jp c,FileFindError
        ret z
        ld (WadConfig),hl
        ld (WadConfig+2),de
        inc bc
        ld h,b
        ld l,c
        ld de,WadConfig+4
        ld bc,4
.copy:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        inc bc
        or a
        jr nz,.copy
        ld (ConfigLength),bc
        ld hl,WadConfig+4
        ld de,WadFind+1
        ld bc,13
        ldir
        ; Codex: таблица DAT готовится до APP_START, пока игрового ELF в PSRAM ещё нет.
        call FastOpen
        jp nz,FileFindError
        ld a,1
        ld (WadMapped),a
        ld (WadSelected),a
        or a
        ret

FileFindError:
        ld hl,MsgFileRead
        ld (BootMessage),hl
        jp Fail

Stop:
        call StopApp
        jr c,StopFailed
        xor a
        jr Exit
; Codex: успешен только ED с одним байтом 01; отправка и ожидание тоже могут отказать.
; Клод: передача модуля быстрым путём (A≠0, NZ) — если он включён и не отказал в этот запуск (ModuleLegacy).
ModuleFast:
        ld a,(ModuleLegacy)
        or a
        ld a,0
        ret nz
        ld a,(MediaEnabled)
        or a
        ret
StopApp:
        ld a,CMD_APP_STOP
        call Proto_SendEmpty
        ret c
        ld a,RESP_APP_STOP
        ld b,64
        call WaitLong
        ret c
        ld hl,(ProtoRxLen)
        dec hl
        ld a,h
        or l
        jr nz,.bad
        ld a,(ProtoBuf)
        cp 1
        jr nz,.bad
        xor a
        ld (AppStarted),a
        ret
.bad:
        ld a,#E1
        scf
        ret
; Codex: при отказе остановки не открываем FT812 для экрана ошибки; отпускаем M_SEL.
StopFailed:
        ld (BootError),a
        ld a,2
        jr Exit
Fail:
        ld (BootError),a
        call BootTrace
        ld a,(AppStarted)
        or a
        jr z,.border
        call StopApp
        jr c,StopFailed
.border:
        call ShowBootError
        call FtScreen_Start
        call FtError
        call BootTrace
        call LogBootError
        ld a,2
        out (#FE),a
        jp Halt
Exit:
        out (#FE),a
        ld bc,#00AF
        xor a
        out (c),a
Halt:
        jr Halt
; Codex: ACK предшествует ESP.restart() на 100 мс; не цепляем старый сеанс.
; После паузы повторяем штатный ZiFi_Init, включая SESSION|DROP и возможности DMA.
ResetEspForStart:
        ld a,1
        ld (EspResetTried),a
        ld a,CMD_SYS_RESET
        call Proto_SendEmpty
        jp c,Fail
        ld a,RESP_ACK
        ld b,8
        call WaitLong
        ; Codex: ACK может пропасть при сбросе SPI; готовность проверит ZiFi_Init.
        ld d,8
.delay:
        ld bc,#FFFF
        call FtDelay
        dec d
        jr nz,.delay
        call Proto_ResetRx
        ld (ProtoBurstLeft),hl
        ld (ProtoErr),a
        IFDEF ZIFI_SPI_DMA
        ld (ProtoDmaUsed),a
        ENDIF
        ld b,64
.connect:
        push bc
        call ZiFi_Init
        pop bc
        jr nc,.ready
        djnz .connect
        ld a,#E0
        jp Fail
.ready:
        call MediaProbe
        ; Codex: заново проверяем/загружаем ELF и строим DAT LBA; сеансы ESP утрачены.
        xor a
        ld (WadMapped),a
        ld (WadSelected),a
        jp LoadGameModule
WaitLong:
        ld (WaitWanted),a
.again:
        push bc
        ld a,(WaitWanted)
        ld de,#FFFF
        call Proto_WaitCmd
        pop bc
        ret nc
        djnz .again
        ld a,#E0
        scf
        ret

QueryFailed:
        ld hl,MsgCache
        cp 2
        jr nz,.invalid
        ld hl,MsgCacheFull
        jr .fail
.invalid:
        cp 3
        jr nz,.fail
        ld hl,MsgQueryInvalid
.fail:
        ld (BootMessage),hl
        jp Fail

WC_PAGE2: db 2
WC_PAGE3: db #20
GameDir: dd 0
WadFind: db 0
        ds 13
AudioMissing:
        ld hl,AudioMissingText
        ld (BootMessage),hl
        jp Fail
AudioMissingText: db "MISSING /WC/ESP/OGG.ELF",0
AudioWcSpec: db #10,"WC",0
AudioEspSpec: db #10,"ESP",0
AudioSpec: db 0,"OGG.ELF",0
LoadingAudio: db 0
QueryBytes: dw 18
ElfSpec: db 0,"HMM2.ELF",0
Wad2: db 0,"HMM2.DAT",0
WadFull: db 0,"HMM2.DAT",0
WadShare: db 0,"HMM2.DAT",0
Query: db 0
        ds 8
        db "HMM2.ELF",0
QueryEnd:
WadConfig: ds 17
ConfigLength: dw 0
ElfSize: dd 0
Offset: dd 0
Chunk: dw 0
WaitWanted: db 0
BootStage: db 0
BootError: db 0
ModuleLegacy: db 0
AppStarted: db 0
EspResetTried: db 0
WadSelected: db 0
KeyDivider: db 0
StatusDivider: db 32
KeysPending: db 0
StatusPending: db 0
Ps2Ready: db 0
KeyBuf: ds 4+PS2_DRAIN
Fx: db 32,1,1,0
        ds 28
; Codex: пакеты живут в свободной странице 56; банк кода отдан менеджеру предвыборки.
PutBuf equ #C000
        INCLUDE "diagnostic.asm"
        INCLUDE "display.asm"
        INCLUDE "directory.asm"
        INCLUDE "pathcfg.asm"
        INCLUDE "fastread.asm"
        INCLUDE "music_cache.asm"
        INCLUDE "media.asm"
        INCLUDE "sd_fast.asm"
        INCLUDE "files.asm"




        INCLUDE "zifi_link.asm"
        INCLUDE "proto.asm"
CodeEnd:
        ASSERT CodeEnd < #B800
        ; Codex: B800..B9FF — сектор FAT, BA00..BBFF — метки кольца; код их не занимает.
        ORG #BC00
        INCLUDE "graphics.asm"
        ; Codex: построитель таблицы занимает свободный хвост страницы кода, ниже стека.
        INCLUDE "fastopen.asm"
; Codex: снимок Gluk RTC без записи в ячейки часов/NVRAM. Ограниченные повторы с ожиданием UIP, без зависания.
RtcRead:
        ld bc,#EFF7
        ld a,#80
        out (c),a
        ld d,64
.retry:
        ld a,#0A
        call RtcRegister
        bit 7,a
        jr nz,.again
        ld hl,RtcRegisters
        ld ix,RtcBuffer
        ld e,8
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
        ld hl,RtcBuffer+1
        cp (hl)
        jr z,.done
.again:
        djnz $
        dec d
        jr nz,.retry
        xor a
        ld (RtcBuffer+7),a
.done:
        ; Codex: вернуть выбор PS/2 FIFO; C=F7, настройка и очередь не изменяются.
        ld b,#DF
        ld a,#F0
        out (c),a
        ret
RtcRegister:
        ld b,#DF
        out (c),a
        ld b,#BF
        in a,(c)
        ret
RtcRegisters: db #0B,0,2,4,7,8,9,#0D
RtcBuffer equ PutBuf

GraphicsCodeEnd:
        ASSERT GraphicsCodeEnd < #BF00
        SAVEBIN "build/hmm2_z80.bin",Start,GraphicsCodeEnd-Start
