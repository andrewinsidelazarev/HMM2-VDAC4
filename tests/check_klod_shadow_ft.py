"""Клод: проверка кадров FT с тенями FT812 (чёрный с альфой, как в VDAC2).

check_fix4_ft.py принимает только альфу 0/255. Здесь лист исполняется со смешением
SRC_ALPHA/ONE_MINUS_SRC_ALPHA; точки, где легла полупрозрачная тень, сверяются с эталоном
CPU только по яркости (тень оригинала — таблица палитры), все остальные — точно.
Поддержан REPEAT (кольцо окна карты в RAM_G); запись в показанную память не проверяется:
кольцо обновляется на месте.
Запуск: check_klod_shadow_ft.py <папка прогона совместного стенда>
"""
from pathlib import Path
import sys,struct,json,re
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np
from PIL import Image
import check_fix4_ft as base

def render(words,ram):
    rgb=np.zeros((768,1024,3),dtype=np.int32);shaded=np.zeros((768,1024),dtype=bool)
    source=palette=stride=bh=bw=oh=0;a=e=256;tx=ty=0;fmt=4;wrapx=wrapy=0
    for v in words:
        op=v>>24
        if op==1:source=v&0x3fffff
        elif op==0x2a:palette=v&0x3fffff
        elif op==7:stride=(stride&3072)|((v>>9)&1023);bh=(bh&1536)|(v&511)
        elif op==0x28:stride=(stride&1023)|((v>>2&3)<<10);bh=(bh&511)|((v&3)<<9)
        # Клод: REPEAT — маска (размер − 1), как в растеризаторе Tang (кольцо окна карты).
        elif op==8:bw=(bw&1536)|(v>>9&511);oh=(oh&1536)|(v&511);wrapx=v>>19&1;wrapy=v>>18&1
        elif op==0x29:bw=(bw&511)|((v>>2&3)<<9);oh=(oh&511)|((v&3)<<9)
        elif op==0x15:a=base.signed(v&0x1ffff,17)
        elif op==0x19:e=base.signed(v&0x1ffff,17)
        elif op==0x17:tx=base.signed(v&0xffffff,24)
        elif op==0x1a:ty=base.signed(v&0xffffff,24)
        elif op==0x27:fmt=v&7
        elif v>>30==1:
            x=base.signed(v>>15&32767,15)//(1<<fmt);y=base.signed(v&32767,15)//(1<<fmt)
            sx=(np.arange(bw)*a+tx)//256;sy=(np.arange(oh)*e+ty)//256
            if wrapx:sx&=stride-1
            if wrapy:sy&=bh-1
            indexes=np.frombuffer(ram,dtype=np.uint8)[source+sy[:,None]*stride+sx]
            pal=np.frombuffer(ram,dtype=np.uint8,count=1024,offset=palette).reshape(256,4)
            rgba=pal[indexes].astype(np.int32);alpha=rgba[:,:,3:4];src=rgba[:,:,:3][:,:,::-1]
            dst=rgb[y:y+oh,x:x+bw]
            # Клод: смешение FT812 по умолчанию: src·a + dst·(255−a), умножение ⌊a·b/255⌋.
            dst[:]=(src*alpha)//255+(dst*(255-alpha))//255
            part=(alpha[:,:,0]>0)&(alpha[:,:,0]<255)
            shaded[y:y+oh,x:x+bw]|=part
            full=alpha[:,:,0]==255;shaded[y:y+oh,x:x+bw][full]=False
    return rgb.astype(np.uint8),shaded

def main():
    folder=Path(sys.argv[1]);raw=(folder/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192)
    pos=0;rgb=shaded=None;frames=bad=0;shade_points=0;lum=[];worst=(0,0)
    while pos<len(raw):
        address,count=struct.unpack_from('<II',raw,pos);pos+=8;n=count*4;data=raw[pos:pos+n];pos+=n
        if address==0xffffffff:
            serial=struct.unpack('<I',data)[0];ref=base.expected(folder,serial).astype(np.int32)
            diff=np.any(rgb!=ref,axis=2)&~shaded;frames+=1
            if diff.any():
                bad+=1;worst=max(worst,(int(diff.sum()),serial))
                if bad<=3:Image.fromarray(rgb).save(folder/f'klod_fail_{serial}_ft.png');Image.fromarray(ref.astype(np.uint8)).save(folder/f'klod_fail_{serial}_cpu.png')
            if shaded.any():
                shade_points+=int(shaded.sum())
                f=lambda c:0.299*c[...,0]+0.587*c[...,1]+0.114*c[...,2]
                lum.append(float(np.mean(f(rgb[shaded].astype(float)))-np.mean(f(ref[shaded].astype(float)))))
            if serial in (20,200):Image.fromarray(rgb).save(folder/f'klod_ft_{serial:04}.png')
        elif address<0x300000:ram[address:address+n]=data
        elif 0x300000<=address<0x302000:dl[address-0x300000:address-0x300000+n]=data
        elif address==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,shaded=render(words,ram)
    report=dict(frames=frames,frames_with_non_shadow_differences=bad,worst=worst,shadow_points=shade_points,
                mean_shadow_luma_difference=round(sum(lum)/len(lum),2) if lum else None)
    (folder/'klod_shadow_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
    return 1 if bad else 0
if __name__=='__main__':raise SystemExit(main())
