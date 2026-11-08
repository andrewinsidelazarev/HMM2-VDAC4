"""Codex: независимое исполнение DL/RAM_G с ручками, CELL, тенями и геометрией 5/8."""
from pathlib import Path
from ft_command_fifo import CommandFifo
import sys,struct,json,hashlib
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np
from PIL import Image
from check_fix4_ft import signed,expected

def render(words,ram,protected=None,handle_state=None,paletted8_single_channel=None,dry_run=False):
    width,height=(640,480) if 0x19000100 in words else (1024,768)
    rgb=None if dry_run else np.zeros((height,width,3),dtype=np.int32)
    dst_alpha=None if dry_run else np.zeros((height,width),dtype=np.int32)
    shaded=None if dry_run else np.zeros((height,width,3),dtype=bool)
    mask=15;blend_src=2;blend_dst=4;color_alpha=255;clear_alpha=0
    if paletted8_single_channel is None:paletted8_single_channel=any(v>>24==0x20 and v&15!=15 for v in words)
    handles=handle_state if handle_state is not None else [dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    scissor=(0,0,width,height);clear_color=(0,0,0)
    handle=cell=palette=0;a=e=256;tx=ty=vx=vy=0;fmt=4;primitive=1;color=(255,255,255);corner=None
    # Codex: стандартные коэффициенты FT812 и независимые COLOR_MASK для каждого канала.
    def factor(kind,sa,da):
        if kind==0:return np.zeros_like(sa)
        if kind==1:return np.full_like(sa,255)
        if kind==2:return sa
        if kind==3:return da
        if kind==4:return 255-sa
        if kind==5:return 255-da
        raise AssertionError(('BLEND_FUNC',kind))
    def paint(x,y,w,h,src,sa):
        if dry_run:return
        src=np.broadcast_to(src,(h,w,3));sa=np.broadcast_to(sa,(h,w));da=dst_alpha[y:y+h,x:x+w];fs=factor(blend_src,sa,da);fd=factor(blend_dst,sa,da)
        dest=rgb[y:y+h,x:x+w];flags=shaded[y:y+h,x:x+w];mixed=np.minimum(255,(src*fs[:,:,None])//255+(dest*fd[:,:,None])//255)
        partial=((fs>0)&(fs<255))|((fd>0)&(fd<255));opaque=(fs==255)&(fd==0)
        for channel,bit in enumerate([8,4,2]):
            if mask&bit:dest[:,:,channel]=mixed[:,:,channel];flags[:,:,channel]|=partial;flags[:,:,channel][opaque]=False
        if mask&1:da[:]=np.minimum(255,(sa*fs)//255+(da*fd)//255)
    for v in words:
        op=v>>24;d=handles[handle]
        if op==0x20:mask=v&15
        elif op==0x0f:clear_alpha=v&255
        elif op==0x10:color_alpha=v&255
        elif op==2:clear_color=(v>>16&255,v>>8&255,v&255)
        elif op==0x26 and not dry_run:
            if v&4:
                for channel,bit in enumerate([8,4,2]):
                    if mask&bit:rgb[:,:,channel]=clear_color[channel];shaded[:,:,channel]=False
                if mask&1:dst_alpha[:]=clear_alpha
        elif op==5:handle=v&31
        elif op==6:cell=v&127
        elif op==1:d['source']=v&0x3fffff
        elif op==0x2a:palette=v&0x3fffff
        elif op==7:
            d['format']=(v>>19)&31;assert d['format'] in (7,16)
            d['stride']=(d['stride']&3072)|((v>>9)&1023);d['bh']=(d['bh']&1536)|(v&511)
        elif op==0x28:d['stride']=(d['stride']&1023)|((v>>2&3)<<10);d['bh']=(d['bh']&511)|((v&3)<<9)
        elif op==8:
            assert not v&0x100000
            d['wrapx']=bool(v&0x80000);d['wrapy']=bool(v&0x40000)
            d['bw']=(d['bw']&1536)|(v>>9&511);d['oh']=(d['oh']&1536)|(v&511)
        elif op==0x29:d['bw']=(d['bw']&511)|((v>>2&3)<<9);d['oh']=(d['oh']&511)|((v&3)<<9)
        elif op==0x15:a=signed(v&0x1ffff,17)
        elif op==0x19:e=signed(v&0x1ffff,17)
        elif op==0x17:tx=signed(v&0xffffff,24)
        elif op==0x1a:ty=signed(v&0xffffff,24)
        elif op==0x27:fmt=v&7
        elif op==0x2b:vx=signed(v&0x1ffff,17)//16
        elif op==0x2c:vy=signed(v&0x1ffff,17)//16
        elif op==0x1b:scissor=((v>>11)&2047,v&2047,*scissor[2:])
        elif op==0x1c:scissor=(*scissor[:2],(v>>12)&4095,v&4095)
        elif op==0x1f:primitive=v&15;corner=None
        elif op==4:color=(v>>16&255,v>>8&255,v&255)
        elif op==0x0b:blend_src=v>>3&7;blend_dst=v&7
        elif v>>30==1 or v>>30==2:
            if v>>30==2:
                x=(v>>21&511)+vx;y=(v>>12&511)+vy;d=handles[v>>7&31];draw_cell=v&127
                fx=x*16;fy=y*16
            else:
                fx=signed(v>>15&32767,15)*16//(1<<fmt)+vx*16;fy=signed(v&32767,15)*16//(1<<fmt)+vy*16
                x=(fx+15)//16;y=(fy+15)//16;draw_cell=cell
            if primitive==9:
                if corner is None:corner=(x,y)
                else:
                    x0,y0=corner;sx0,sy0,sw,sh=scissor
                    x0=max(0,x0,sx0);y0=max(0,y0,sy0);x=min(width,x,sx0+sw);y=min(height,y,sy0+sh)
                    if x>x0 and y>y0:paint(x0,y0,x-x0,y-y0,np.array(color,dtype=np.int32),np.array(color_alpha,dtype=np.int32))
                    corner=None
                continue
            assert primitive==1
            stride,bh,bw,oh=[d[k] for k in ['stride','bh','bw','oh']]
            # Codex: FT допускает отрицательную вершину; источник отсчитывается от неё,
            # а запись ограничена экраном и SCISSOR. См. ft_rd_walk.sv:265 и ft_rd_exec.sv:754.
            sx0,sy0,sw,sh=scissor
            right=min(x+bw,width,sx0+sw);bottom=min(y+oh,height,sy0+sh)
            x=max(x,0,sx0);y=max(y,0,sy0);bw=right-x;oh=bottom-y
            if bw<=0 or oh<=0:continue
            address=d['source']+stride*bh*draw_cell
            if protected is not None:
                protected.append((address,address+stride*bh))
                # Codex: RGB565 не читает палитру; её фиктивная защита скрывает смену видеобуфера.
                if d.get('format',16)!=7:protected.append((palette,palette+1024))
            # Codex: этот проход исполняет все команды состояния и определяет точные
            # зависимости от RAM_G. Пиксельный проход можно повторить из кэша лишь
            # при совпадении команд, входного состояния и всех используемых байтов.
            if dry_run:continue
            sx=((np.arange(bw)*16+x*16-fx)*a//16+tx)//256;sy=((np.arange(oh)*16+y*16-fy)*e//16+ty)//256
            # Codex: RGB565 ролика занимает два байта на пиксель; геометрия FT остаётся в пикселях.
            pixel_bytes=2 if d.get('format',16)==7 else 1
            pixel_width=stride//pixel_bytes
            if d.get('wrapx'):sx%=pixel_width
            if d.get('wrapy'):sy%=bh
            border=(sy[:,None]<0)|(sy[:,None]>=bh)|(sx<0)|(sx>=pixel_width)
            addresses=address+np.clip(sy,0,bh-1)[:,None]*stride+np.clip(sx,0,pixel_width-1)*pixel_bytes
            if pixel_bytes==2:
                raw=np.frombuffer(ram,dtype=np.uint8)
                packed=raw[addresses].astype(np.uint16)|(raw[addresses+1].astype(np.uint16)<<8)
                src=np.stack(((packed>>11)*255//31,((packed>>5)&63)*255//63,(packed&31)*255//31),axis=2).astype(np.int32)
                alpha=np.where(border,0,color_alpha);src=src*np.array(color,dtype=np.int32)//255
                paint(x,y,bw,oh,src,alpha);continue
            indexes=np.frombuffer(ram,dtype=np.uint8)[addresses]
            pal=np.frombuffer(ram,dtype=np.uint8,count=1024,offset=palette).reshape(256,4)
            rgba=pal[indexes].astype(np.int32)
            if paletted8_single_channel:rgba=np.repeat(rgba[:,:,:1],4,axis=2)
            rgba[border]=0;alpha=rgba[:,:,3]*color_alpha//255;src=rgba[:,:,:3][:,:,::-1]*np.array(color,dtype=np.int32)//255
            paint(x,y,bw,oh,src,alpha)
    return (None,None) if dry_run else (rgb.astype(np.uint8),np.any(shaded,axis=2))

def main():
    folder=Path(sys.argv[1]);reference=Path(sys.argv[2]) if len(sys.argv)>2 else folder
    raw=(folder/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192)
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    fifo=CommandFifo();pos=0;frames=bad=0;worst=(0,0);rows=[];rgb=shaded=None
    while pos<len(raw):
        address,count=struct.unpack_from('<II',raw,pos);pos+=8;n=count*4;data=raw[pos:pos+n];pos+=n
        if address!=0xffffffff:address&=0x7fffffff
        if address==0xffffffff:
            serial=struct.unpack('<I',data)[0];ref=expected(reference,serial)
            if rgb.shape[1]==640:
                indices=np.frombuffer((reference/f'frame_{serial:04}.idx').read_bytes(),dtype=np.uint8).reshape(480,640)
                pal=np.frombuffer((reference/f'frame_{serial:04}.pal').read_bytes(),dtype=np.uint8).reshape(256,3)
                ref=pal[indices]
            different=np.any(rgb!=ref,axis=2)&~shaded;frames+=1
            points=int(different.sum());rows.append(dict(frame=serial,non_shadow_differences=points,shadow_pixels=int(shaded.sum()),rgb_sha256=hashlib.sha256(rgb.tobytes()).hexdigest()))
            if points:
                bad+=1;worst=max(worst,(points,serial))
                if bad<=3:
                    Image.fromarray(rgb).save(folder/f'fix5_fail_{serial}_ft.png');Image.fromarray(ref).save(folder/f'fix5_fail_{serial}_ref.png')
            if serial in [1,30,200,370]:Image.fromarray(rgb).save(folder/f'fix5_ft_{serial}.png')
        elif address<0x300000:
            assert address+n<=0x300000;ram[address:address+n]=data
        elif address<0x302000:dl[address-0x300000:address-0x300000+n]=data
        elif address==0x302054:
            words=[]
            for v, in struct.iter_unpack('<I',dl):
                words.append(v)
                if not v:break
            rgb,shaded=render(words,ram,handle_state=handles)
        elif fifo.write(address,data,ram,dl):pass
        elif address not in (0x302034,0x302048,0x302070):raise AssertionError(hex(address))
    result=dict(frames=frames,bad=bad,worst=worst,reference=str(reference),rows=rows)
    (folder/'fix5_ft_check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='rows'}));return int(bad>0)
if __name__=='__main__':raise SystemExit(main())
