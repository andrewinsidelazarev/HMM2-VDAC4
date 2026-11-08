; Codex: файловый сервер DOOM VDAC4; лицензии — THIRD_PARTY.md.






ServeFile:
        ld a,56
        call FastMap
        xor a
        ld (WadSelected),a
        ld hl,(ProtoRxLen)
        ld de,35
        or a
        sbc hl,de
        jp c,Fail
        ld hl,PutBuf
        ld de,PutBuf+1
        ld bc,8
        ld (hl),0
        ldir
        ld hl,(ProtoBuf)
        ld (PutBuf),hl

        ld hl,(ProtoRxLen)
        ld de,35
        or a
        sbc hl,de
        jp nz,.invalid
        ld a,#E5
        call RememberRequest
        ld hl,ProtoBuf+3
        ld de,FileName+1
        ld bc,13
        ldir
        ld hl,ProtoBuf+16
        ld de,OtherName+1
        ld bc,13
        ldir
        ld hl,FileFx
        ld de,FileFx+1
        ld bc,31
        ld (hl),0
        ldir
        ld a,32
        ld (FileFx),a
        ld a,1
        ld (FileFx+1),a
        ld hl,ProtoBuf+29
        ld de,FileFx+4
        ld bc,6
        ldir
        ld hl,(ProtoBuf+33)
        ld (FileFx+10),hl
        ld (FileCount),hl
        ld de,7681
        or a
        sbc hl,de
        jp nc,.invalid
        ld a,(ProtoBuf+2)
        ld (FileOp),a
        cp 9
        jp nc,.invalid
        cp 6
        jp z,.sync
        cp 5
        jp z,.delete
        cp 4
        jp z,.rename
        ; Codex: FIND один раз при смене файла; DAT с готовой LBA-таблицей выбор не трогает.
        call FileSelect
        jp c,.error
        jr nz,.exists
        ld a,(FileOp)
        cp 2
        jr z,.create
        ld a,#19
        jp .error
.create:
        ld hl,FileName+1
        ld de,CreateName+5
        ld bc,13
        ldir
        ld hl,CreateName
        call FAT32_CREATE
        jp nz,.error
        jp .stamp
.exists:
        ld (PutBuf+5),hl
        ld (PutBuf+7),de
        ld a,(FileOp)
        or a
        jp z,.ok
        cp 2
        jp z,.truncate
        cp 1
        jp z,.read
        cp 7
        jp z,.stream_read
        cp 3
        jr z,.write
        cp 8
        jp nz,.invalid
.write:




        ld hl,0
        ld (PullAt),hl
.pull:
        ld hl,(PullAt)
        ld de,(FileCount)
        or a
        sbc hl,de
        jp nc,.pulled
        ld a,PULL_TRIES
        ld (PullTries),a
.pull_try:
        ld hl,(PutBuf)
        ld (PullReq),hl
        ld hl,(PullAt)
        ld (PullReq+2),hl
        ld hl,PullReq
        ld bc,4
        ld a,#76
        call Proto_Send
.pull_wait:
        ld a,#F6
        ld b,1
        call WaitLong
        jr nc,.pull_got
        ld hl,PullTries
        dec (hl)
        jr nz,.pull_try
        jp .invalid
.pull_stale:
        ld hl,PullTries
        dec (hl)
        jr nz,.pull_wait
        jp .invalid
.pull_got:
        ld hl,(ProtoBuf)
        ld de,(PutBuf)
        or a
        sbc hl,de
        jr nz,.pull_stale
        ld a,(ProtoBuf+2)
        cp 1
        jp nz,.invalid
        ld hl,(ProtoBuf+3)
        ld de,(PullAt)
        or a
        sbc hl,de
        jr c,.pull_stale
        jp nz,.invalid
        ld hl,(FileCount)
        ld de,(PullAt)
        or a
        sbc hl,de
        ld de,PIECE
        ld a,(FileOp)
        cp 8
        jr nz,.piece
        ld de,1016
.piece:
        or a
        sbc hl,de
        jr c,.pull_last
        ex de,hl
        jr .pull_want
.pull_last:
        add hl,de
.pull_want:
        ld (PullLen),hl
        ld de,(ProtoBuf+5)
        or a
        sbc hl,de
        jp nz,.invalid
        ld hl,(ProtoRxLen)
        ld de,7
        or a
        sbc hl,de
        ld de,(PullLen)
        or a
        sbc hl,de
        jp nz,.invalid
        ld hl,PutBuf+9
        ld de,(PullAt)
        add hl,de
        ex de,hl
        ld hl,ProtoBuf+7
        ld bc,(PullLen)
        ldir
        ld hl,(PullAt)
        ld de,(PullLen)
        add hl,de
        ld (PullAt),hl
        ld de,(FileCount)
        or a
        sbc hl,de
        jr nc,.pulled
        ld a,(FileOp)
        cp 8
        jp nz,.pull
        ; Codex: следующий F6 уже идёт; новый 76 нужен только для повтора после таймаута.
        ld a,PULL_TRIES
        ld (PullTries),a
        jp .pull_wait
.pulled:
        ld a,(FileOp)
        cp 8
        jr nz,.write_at
        call FileAtEnd
        jr nz,.write_at
        ; Codex: APPEND хранит конец цепочки; FILEX нужен только для произвольной записи.
        ld hl,PutBuf+9
        ld bc,(FileCount)
        call FAT32_APPEND
        jp nz,.error
        ld hl,(FileSize)
        ld de,(FileCount)
        add hl,de
        ld (FileSize),hl
        jr nc,.written
        ld hl,(FileSize+2)
        inc hl
        ld (FileSize+2),hl
.written:
        ld a,1
        ld (FileDirty),a
        jp .ok
.write_at:
        ld hl,PutBuf+9
        ld (FileFx+8),hl
        ld a,2
        jr .transfer
.read:
        ld hl,PutBuf+9
        ld (FileFx+8),hl
        ld a,1
.transfer:
        ld (FileFx+2),a
        ld hl,FileFx
        call FAT32_FILEX
        jp nz,.error
        ld hl,(FileFx+24)
        ld de,(FileCount)
        or a
        sbc hl,de
        jp nz,.invalid
        ld a,(FileOp)
        cp 1
        ; Codex: запись дату не меняет; время ставится при создании/усечении и переименовании.
        jr z,.read_done
        ld a,(FileOp)
        cp 8
        jp nz,.sync
        ; Codex: после произвольной записи размер перечитается при следующем обращении.
        xor a
        ld (FileSelected),a
        jp .written
.read_done:
        ld (PutBuf+3),de
        jp .ok
.stream_read:
        call FileRead
        jp nz,.error
        call FileSend
        jp c,FileReplyFailed
        jp .ok
.truncate:
        xor a
        ld (FileSelected),a
        ld a,3
        ld (FileFx+2),a
        ld hl,FileFx
        call FAT32_FILEX
        jp nz,.error
        jp .stamp
.rename:
        call FileClose
        jp nz,.error
        ld a,5
        ld (FileFx+2),a
        ld a,FILEX_FLAG_REPLACE+FILEX_FLAG_CURRENT_DIR
        ld (FileFx+3),a
        ld hl,FileName
        ld (FileFx+8),hl
        ld hl,OtherName
        ld (FileFx+12),hl
        ld hl,14
        ld (FileFx+10),hl
        ld (FileFx+14),hl
        ld hl,FileFx
        call FAT32_FILEX
        jp nz,.error
.stamp:
        ; Codex: время только успешного изменения; сбой RTC не меняет ответ ESP.
        call FileStamp
        jp .sync
.delete:
        call FileClose
        jp nz,.error
        ld hl,FileName
        call FAT32_DELETE
        jp nz,.error
.sync:
        call FileClose
        jp nz,.error
.ok:
        xor a
        jp .answer
.invalid:
        ld a,#10
.error:
        push af
        xor a
        ld (FileSelected),a
        pop af
        or a
        jp nz,.answer
        inc a
.answer:
        push af
        ld a,(FileOp)
        cp 7
        jr c,.legacy_answer
        ld hl,0
        ld (PutBuf+3),hl
        ld (PutBuf+5),hl
        ld (PutBuf+7),hl
.legacy_answer:
        pop af
        ld (PutBuf+2),a
        ld bc,(PutBuf+3)
        ld hl,9
        add hl,bc
        ld b,h
        ld c,l
        ld hl,PutBuf
        ld a,#74
        call Proto_Send
        jp c,FileReplyFailed
        xor a
        ret
; Codex: один выбранный файл. При смене имени и закрытии завершаем накопленные записи.
FileClose:
        xor a
        ld (FileSelected),a
        ld (FileDirty),a
        jp FAT32_CLOSE
FileSelect:
        ld a,(FileSelected)
        or a
        jr z,.find
        ld hl,FileName+1
        ld de,SelectedName
        ld b,13
.compare:
        ld a,(de)
        cp (hl)
        jr nz,.find
        inc hl
        inc de
        djnz .compare
        ld hl,(FileSize)
        ld de,(FileSize+2)
        ld a,1
        or a
        ret
.find:
        ld a,(FileDirty)
        or a
        call nz,FileClose
        scf
        ret nz
        xor a
        ld (FileSelected),a
        ld hl,FileName
        call FAT32_FIND
        ret c
        ret z
        ld (FileSize),hl
        ld (FileSize+2),de
        push hl
        push de
        ld hl,FileName+1
        ld de,SelectedName
        ld bc,13
        ldir
        xor a
        ld hl,FileReadPos
        ld (hl),a
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a
        inc a
        ld (FileSelected),a
        pop de
        pop hl
        or a
        ret
FileAtEnd:
        ld hl,(FileFx+4)
        ld de,(FileSize)
        or a
        sbc hl,de
        ret nz
        ld hl,(FileFx+6)
        ld de,(FileSize+2)
        sbc hl,de
        ret

; Codex: READ сохраняет сектор/кластер. Невыровненный seek обслуживает штатный FILEX.
FileRead:
        ld hl,(FileCount)
        ld a,h
        or l
        ret z
        ld hl,(FileFx+4)
        ld de,(FileCount)
        add hl,de
        ld bc,(FileSize)
        ld (FileNextPos),hl
        ld hl,(FileFx+6)
        ld de,0
        adc hl,de
        jr c,.short
        ld (FileNextPos+2),hl
        ld de,(FileSize+2)
        or a
        sbc hl,de
        jr c,.inside
        jr nz,.short
        ld hl,(FileNextPos)
        or a
        sbc hl,bc
        jr c,.inside
        jr z,.inside
.short:
        ld a,1
        or a
        ret
.inside:
        ld hl,(FileFx+4)
        ld de,(FileReadPos)
        or a
        sbc hl,de
        jr nz,.at
        ld hl,(FileFx+6)
        ld de,(FileReadPos+2)
        sbc hl,de
        jr nz,.at
        ld hl,(FileCount)
        ld de,511
        add hl,de
        ld a,h
        srl a
        ld b,a
        ld hl,PutBuf+6
        call FAT32_READ
        jr c,.short
        ld hl,FileNextPos
        ld de,FileReadPos
        ld bc,4
        ldir
        ; Codex: после неполного сектора следующий запрос требует позиционирования.
        ld hl,(FileCount)
        ld a,h
        and 1
        or l
        jr nz,.invalidate
        xor a
        ret
.at:
        ld hl,PutBuf+6
        ld (FileFx+8),hl
        ld a,1
        ld (FileFx+2),a
        ld hl,FileFx
        call FAT32_FILEX
        ret nz
        ld hl,(FileFx+24)
        ld de,(FileCount)
        or a
        sbc hl,de
        jr nz,.short
.invalidate:
        ld hl,#FFFF
        ld (FileReadPos),hl
        ld (FileReadPos+2),hl
        xor a
        ret

; Codex: тот же быстрый PUT, что у DAT; без PUT_READY передаём поток обычных 71.
FileSend:
        ld hl,0
        ld (PutBuf+2),hl
        ld hl,(FileCount)
        ld (PutBuf+4),hl
        ld (Chunk),hl
        ld a,(MediaEnabled)
        or a
        jr z,.plain
        call MediaSend
        ret nc
.plain:
        ld hl,0
        ld (PullAt),hl
.loop:
        ld hl,(FileCount)
        ld de,(PullAt)
        or a
        sbc hl,de
        ret z
        ld de,1014
        push hl
        or a
        sbc hl,de
        pop hl
        jr c,.count
        ex de,hl
.count:
        ld (PullLen),hl
        ld (#E004),hl
        ld hl,(PutBuf)
        ld (#E000),hl
        ld hl,(PullAt)
        ld (#E002),hl
        ld de,PutBuf+6
        add hl,de
        ld de,#E006
        ld bc,(PullLen)
        ldir
        ld hl,(PullLen)
        ld de,6
        add hl,de
        ld b,h
        ld c,l
        ld hl,#E000
        ld a,#71
        call Proto_Send
        ret c
        ld hl,(PullAt)
        ld de,(PullLen)
        add hl,de
        ld (PullAt),hl
        jr .loop
FileSelected: db 0
FileDirty: db 0
SelectedName: ds 13
FileSize: dd 0
FileReadPos: dd 0
FileNextPos: dd 0

PIECE equ 248
PULL_TRIES equ 3
PullAt: dw 0
PullLen: dw 0
PullTries: db 0
PullReq: ds 4
FileOp: db 0
FileCount: dw 0
FileName: db 0
        ds 13
OtherName: db 0
        ds 13
CreateName: db #20
        dd 0
        ds 13
FileFx: ds 32

        INCLUDE "file_time.asm"
