; Codex: основа DOOM VDAC4; происхождение и лицензии — bench/README.md.








CMD_PATH_GET    equ #2A
RESP_PATH_GET   equ #AA
CMD_PATH_SET    equ #2B
RESP_PATH_SET   equ #AB


PathCfgLoad:
        xor a
        ld (PathEsp),a
        call ScanGeometry
        jp nz,.no
        ld hl,ScanBuffer+67
        ld de,PathGetMsg
        ld bc,4
        ldir
        ld hl,ScanBuffer+67
        ld de,PathSetMsg
        ld bc,4
        ldir
        ld hl,PathGetMsg
        ld bc,PathGetEnd-PathGetMsg
        ld a,CMD_PATH_GET
        call Proto_Send
        ld a,RESP_PATH_GET
        call PathWait
        jp c,.no
        ld a,1
        ld (PathEsp),a
        ld a,(ProtoBuf)
        or a
        jp z,.no
        ld hl,ProtoBuf+1
        ld de,PathDir
        call ScanCopy32

        ld hl,PathDir
        ld de,ScanCluster
        call ScanCopy32
        xor a
        ld (ScanSector),a
        call ScanClusterLba
        jr c,.no
        ld hl,PathDir
        call FAT32_SET_DIR_CLUSTER
        ld hl,ElfSpec
        call FAT32_FIND
        jr c,.no
        jr z,.no
        ld hl,Wad2
        call .wad
        jr nz,.yes
        ld hl,WadFull
        call .wad
        jr nz,.yes
        ld hl,WadShare
        call .wad
        jr z,.no
.yes:
        ld hl,PathDir
        ld de,GameDir
        call ScanCopy32
        ld a,1
        or a
        ret
.no:
        xor a
        ret
.wad:
        call FAT32_FIND
        jr c,.none
        ret
.none:
        xor a
        ret


PathCfgSave:
        ld a,(PathEsp)
        or a
        ret z
        ld hl,GameDir
        ld de,PathSetMsg+4
        call ScanCopy32
        ld hl,PathSetMsg
        ld bc,PathSetEnd-PathSetMsg
        ld a,CMD_PATH_SET
        call Proto_Send
        ld a,RESP_PATH_SET
        jp PathWait



PathWait:
        ld (PathWant),a
        ld de,#FFFF
.loop:
        push de
        call Proto_Poll
        pop de
        jr nc,.idle
        ld hl,PathWant
        cp (hl)
        ret z
        cp RESP_ERROR
        scf
        ret z
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

PathGetMsg: ds 4
        db "HSD",0
PathGetEnd:
PathSetMsg: ds 8
        db "HSD",0
PathSetEnd:
PathDir: dd 0
PathEsp: db 0
PathWant: db 0
