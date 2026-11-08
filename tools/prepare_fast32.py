"""Codex: расширяем карту кусков до 32-битного номера сектора; длина куска остаётся u16."""
from prepare_sd_launcher import REF,OUT,clean

def change(s,a,b):
    assert a in s,a
    return s.replace(a,b,1)

def main():
    s=clean((REF/'launcher/fastread.asm').read_text(encoding='utf-8'))
    s=s.replace('FastMaxExtents equ (#10000-FastExtents)/8','FastMaxExtents equ (#10000-FastExtents)/12')
    s=change(s,'        ld (FastSector),hl','        ld (FastSector),hl\n        ld (FastSector+2),hl')
    a=s.index('        ld a,d\n        cp 2');b=s.index('        ld hl,(#3868+26)',a)
    s=s[:a]+'''        ld bc,511
        add hl,bc
        jr nc,.rounded
        inc de
        ld a,d
        or e
        jr z,.fallback
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
        jr z,.fallback
'''+s[b:]
    a=s.index('        ld hl,(FastSectors)\n',s.index('.cluster:'));b=s.index('        ld (FastPart),hl',a)
    s=s[:a]+'''        ld hl,(FastSectors)
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
        jr nz,.full_cluster
        ld a,(ScanSectorsPerCluster)
        ld e,a
        ld d,0
        push hl
        or a
        sbc hl,de
        pop hl
        jr c,.part
.full_cluster:
        ld a,(ScanSectorsPerCluster)
        ld l,a
        ld h,0
.part:
'''+s[b:]
    s=change(s,'''        ld (FastSector),hl
        ld de,(FastSectors)
        or a
        sbc hl,de
        jr z,.ready''','''        ld (FastSector),hl
        ld hl,(FastSector+2)
        ld de,0
        adc hl,de
        ld (FastSector+2),hl
        ld hl,(FastSector)
        ld de,(FastSectors)
        or a
        sbc hl,de
        jr nz,.more
        ld hl,(FastSector+2)
        ld de,(FastSectors+2)
        or a
        sbc hl,de
        jr z,.ready
.more:''')
    # Codex: запись 12 байт: начало u32, длина u16, резерв u16, LBA u32.
    a=s.index('FastAppend:');b=s.index('FastAddress:',a)
    block=s[a:b]
    for old,new in [(7,11),(6,10),(5,9),(4,8),(3,5),(2,4)]:block=block.replace('(ix+'+str(old)+')','(ix+X'+str(new)+')')
    block=block.replace('(ix+X','(ix+')
    block=change(block,'        add hl,de\n        ld (ix+4),l','        add hl,de\n        jr c,.new\n        ld (ix+4),l')
    block=change(block,'        ld (ix+1),h\n        ld hl,(FastPart)','        ld (ix+1),h\n        ld hl,(FastSector+2)\n        ld (ix+2),l\n        ld (ix+3),h\n        ld hl,(FastPart)')
    s=s[:a]+block+s[b:]
    s=change(s,'''        push hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,FastExtents''','''        push hl
        add hl,hl
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        add hl,hl
        ld de,FastExtents''')
    a=s.index('        ld hl,(Fx+4)\n        ld de,(Fx+6)',s.index('FastReadAt:'));b=s.index('.loop:',a)
    s=s[:a]+'''        ld hl,(Fx+4)
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
'''+s[b:]
    s=change(s,'        inc hl\n        ld (FastSector),hl\n        jr .loop','''        inc hl
        ld (FastSector),hl
        ld a,h
        or l
        jr nz,.loop
        ld hl,(FastSector+2)
        inc hl
        ld (FastSector+2),hl
        jr .loop''')
    s=change(s,'        sbc hl,de\n        ret z\n.lookup:','''        sbc hl,de
        jr nz,.lookup
        ld hl,(FastSector+2)
        ld de,(FastCached+2)
        or a
        sbc hl,de
        ret z
.lookup:''')
    a=s.index('        ld hl,(FastSector)',s.index('        push hl\n',s.index('.find:'))+1);b=s.index('        pop hl\n        ld bc,ScanBuffer',a)
    # Codex: ищем u32 разность; кусок заведомо короче 65536 секторов.
    s=s[:a]+'''        ld hl,(FastSector)
        ld e,(ix+0)
        ld d,(ix+1)
        or a
        sbc hl,de
        ld (FastDelta),hl
        ld hl,(FastSector+2)
        ld e,(ix+2)
        ld d,(ix+3)
        sbc hl,de
        jr c,.next
        ld a,h
        or l
        jr nz,.next
        ld hl,(FastDelta)
        ld e,(ix+4)
        ld d,(ix+5)
        or a
        sbc hl,de
        jr nc,.next
        ld hl,(FastDelta)
        ld e,(ix+8)
        ld d,(ix+9)
        add hl,de
        ld (ScanLba),hl
        ld l,(ix+10)
        ld h,(ix+11)
        jr nc,.lba
        inc hl
.lba:
        ld (ScanLba+2),hl
'''+s[b:]
    s=change(s,'        ld (FastCached),hl','        ld (FastCached),hl\n        ld hl,(FastSector+2)\n        ld (FastCached+2),hl')
    for n in ['FastSector','FastSectors','FastCached']:s=s.replace(n+': dw 0',n+': dd 0')
    s+='FastDelta: dw 0\n'
    # Codex: расширение увеличило расстояния переходов, используем полные адреса.
    s=s.replace('        jr ', '        jp ')
    (OUT/'fastread.asm').write_text('; Codex: карта до 1280 кусков; индексы u32; при переполнении таблицы штатный FILEX.\n'+s,encoding='utf-8')
if __name__=='__main__':main()
