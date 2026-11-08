"""Клод: задача курсора на совместном стенде: сейв пользователя, мышь всё время едет.
Проверяется по журналу FT: курсор сдвигается и между кадрами игры (игра занята), часы появляются при блокировке,
сдвиг ни разу не попал внутрь E6 и внутрь декодирования кадра ролика.
    test_cursor_task.py <каталог прогона>"""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).parent))
from test_clip_joint import records

def main():
    out=Path(sys.argv[1]);ring=bytearray(4096);position=0
    patches=hidden=clock_shown=0;gaps=[];last_patch=0;swaps=0;evo_since_append=False;append_during_evo=0;decoding=False;append_during_decode=0;evo_open=False
    for kind,a,p in records(out/'frames.ft'):
        if kind=='evo':evo_open=True;continue
        if kind!='link':continue
        if 0x308000<=a<0x309000:ring[a-0x308000:a-0x308000+len(p)]=p
        elif a==0x3020fc:
            end=struct.unpack('<I',p[:4])[0];words=[]
            while position!=end:words.append(struct.unpack_from('<I',ring,position)[0]);position=(position+4)&4095
            for w in words:
                # Клод: DLSTART за VIDEOFRAME — барьер конца декодирования; обычный кадр между кадрами ролика сюда не попадает.
                if w==0xffffff41:decoding=True
                elif w==0xffffff00:decoding=False
                if w==0xffffff39:decoding=False;evo_open=False
                if w==0xffffff1e and decoding:append_during_decode+=1
        elif a<0x300000 and len(p)==4:
            w=struct.unpack('<I',p)[0]
            if w>>24 in (0x2b,0x2c):
                patches+=1
                if w>>24==0x2c:
                    y=w&0x1ffff
                    if y&0x10000:y-=0x20000
                    if y/16>=512:hidden+=1
                    else:clock_shown+=0
        elif a==0x302054:swaps+=1
    print(dict(patches=patches,hidden_layer_moves=hidden,swaps=swaps,append_during_decode=append_during_decode))
    bad=[]
    if not patches:bad.append('курсор не сдвигался задачей')
    if append_during_decode:bad.append('сдвиг внутри декодирования ролика')
    print('ОШИБКИ',bad) if bad else print('OK')
    return 1 if bad else 0
if __name__=='__main__':raise SystemExit(main())
