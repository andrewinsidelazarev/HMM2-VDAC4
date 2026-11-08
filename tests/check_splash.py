"""Codex: проверяем JPEG настоящим BL616, исполняем Z80 и loader_tick, затем рендерим их слова DL."""
from pathlib import Path
import copy
import hashlib
import io
import json
import os
import re
import struct
import subprocess
import sys
import numpy as np
from PIL import Image, JpegImagePlugin

ROOT=Path(__file__).resolve().parents[1]
TMP=Path(os.environ.get('HMM2_TMP','E:/zx/cache/tmp/hmm2_splash'))
BASE=Path(os.environ.get('HMM2_STAGE1_BASE',str(TMP/'stage1')))
OUT=TMP/'verification'
BL=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/Tang Nano 20k/BL616/copro')
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
KIT=Path('C:/Program Files (x86)/Windows Kits/10')
sys.path.insert(0,'E:/zx/esp/fat32-venv/Lib/site-packages')
sys.path.insert(0,str(ROOT/'tools'))
import z80
from make_loader_picture import layout, compose


def sha(data):return hashlib.sha256(data).hexdigest()


def execute(cmd,name):
    p=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True)
    (OUT/name).write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode('utf-8',errors='replace'))
    return p.stdout


def compile_exe(sources,name,includes=()):
    inc=[VC/'include',KIT/'Include/10.0.26100.0/ucrt',*includes]
    objects=[]
    for src in sources:
        obj=OUT/(name+'_'+src.stem+'.obj');objects.append(obj)
        lang=['/std:c11','/TC'] if src.suffix=='.c' else ['/std:c++17','/EHsc']
        execute([VC/'bin/Hostx64/x64/cl.exe','/nologo','/O2','/MT','/utf-8','/D_CRT_SECURE_NO_WARNINGS',*lang,*['/I'+str(p) for p in inc],'/c',src,'/Fo'+str(obj)],obj.stem+'.log')
    exe=OUT/(name+'.exe')
    libs=[VC/'lib/x64',KIT/'Lib/10.0.26100.0/ucrt/x64',KIT/'Lib/10.0.26100.0/um/x64']
    execute([VC/'bin/Hostx64/x64/link.exe','/NOLOGO','/OUT:'+str(exe),*objects,*['/LIBPATH:'+str(p) for p in libs]],name+'_link.log')
    return exe


def symbols(path):
    return {name:int(value,16) for name,value in re.findall(r'^([^:\s]+): EQU 0x([0-9A-Fa-f]+)',path.read_text(),re.M)}


def from_spg(folder):
    spg=(folder/'_HMM2.SPG').read_bytes();build=folder/'build';binary=(build/'hmm2_z80.bin').read_bytes()
    offset=spg.find(binary);assert offset>=0 and spg.count(binary)==1
    syms=symbols(build/'hmm2.sym')
    assert syms['GraphicsCodeEnd']<0xbf00
    return spg[offset:offset+len(binary)],syms,dict(sha256=sha(spg),bytes=len(spg),code_offset=offset,code_end=syms['GraphicsCodeEnd'],code_margin=0xbf00-syms['GraphicsCodeEnd'])


def z80_call(binary,syms,routine,values):
    m=z80.Z80Machine();m.set_memory_block(0x8000,binary);writes=[]
    for name,value,size in values:m.set_memory_block(syms[name],int(value).to_bytes(size,'little'))
    hooks={syms[name]:name for name in ['FtRd32','FtWrite','FtWrReg','FtRead']}
    for address in [*hooks,0xfffe]:m.set_breakpoint(address)
    m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0xfffe));m.pc=syms[routine]
    for _ in range(10000):
        if m.pc==0xfffe:return writes,bytes(m.memory)
        m.ticks_to_stop=1000000;m.run()
        if m.pc==0xfffe:return writes,bytes(m.memory)
        if m.pc not in hooks:continue
        name=hooks[m.pc]
        if name=='FtRd32':m.set_memory_block(syms['FtBuf'],bytes(4))
        elif name=='FtRead':m.set_memory_block(m.hl,bytes(m.b))
        elif name=='FtWrite':writes.append(((m.a<<16)|m.de,bytes(m.memory[m.hl:m.hl+m.bc])))
        address=struct.unpack_from('<H',m.memory,m.sp)[0];m.sp+=2;m.pc=address
    raise AssertionError('Z80 не вернулся: '+routine)


def rgb565(raw):
    v=np.frombuffer(raw,dtype='<u2').reshape(768,1024).astype(np.uint32)
    return np.stack([((v>>11)<<3)|((v>>11)>>2),(((v>>5)&63)<<2)|(((v>>5)&63)>>4),((v&31)<<3)|((v&31)>>2)],axis=-1).astype(np.uint8)


def render(data,ram):
    # Codex: интерпретатор команд, а не макет. Любая неизвестная команда завершает проверку ошибкой.
    pixels=np.zeros((768,1024,3),dtype=np.int32)
    s=dict(color=(255,255,255),alpha=255,clear=(0,0,0),clip=(0,0,1024,768),handle=0,primitive=1,vfmt=4,a=256,e=256,line=16)
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0,format=0) for _ in range(32)]
    stack=[];corner=None
    def signed(v,n):return v-(1<<n) if v&(1<<(n-1)) else v
    def paint(x0,y0,src,alpha):
        h,w=src.shape[:2];dst=pixels[y0:y0+h,x0:x0+w]
        a=np.broadcast_to(alpha,(h,w)).astype(np.int32)
        dst[:]=(src*a[:,:,None])//255+(dst*(255-a[:,:,None]))//255
    for word, in struct.iter_unpack('<I',data):
        op=word>>24;d=handles[s['handle']]
        if not word:break
        if op==2:s['clear']=(word>>16&255,word>>8&255,word&255)
        elif op==0x26:pixels[:]=s['clear']
        elif op==4:s['color']=(word>>16&255,word>>8&255,word&255)
        elif op==0x10:s['alpha']=word&255
        elif op==5:s['handle']=word&31
        elif op==1:d['source']=word&0x3fffff
        elif op==7:d.update(format=word>>19&31,stride=(d['stride']&3072)|(word>>9&1023),bh=(d['bh']&1536)|(word&511))
        elif op==0x28:d.update(stride=(d['stride']&1023)|((word>>2&3)<<10),bh=(d['bh']&511)|((word&3)<<9))
        elif op==8:d.update(bw=(d['bw']&1536)|(word>>9&511),oh=(d['oh']&1536)|(word&511))
        elif op==0x29:d.update(bw=(d['bw']&511)|((word>>2&3)<<9),oh=(d['oh']&511)|((word&3)<<9))
        elif op==0x15:s['a']=signed(word&0x1ffff,17)
        elif op==0x19:s['e']=signed(word&0x1ffff,17)
        elif op==0x27:s['vfmt']=word&7
        elif op==0x1b:s['clip']=(word>>11&2047,word&2047,*s['clip'][2:])
        elif op==0x1c:s['clip']=(*s['clip'][:2],word>>12&4095,word&4095)
        elif op==0xe:s['line']=word&4095
        elif op==0x1f:s['primitive']=word&15;corner=None
        elif op==0x21:corner=None
        elif op==0x22:stack.append((copy.deepcopy(s),copy.deepcopy(handles)))
        elif op==0x23:s,handles=stack.pop()
        elif word>>30==1:
            x=signed(word>>15&32767,15)/(1<<s['vfmt']);y=signed(word&32767,15)/(1<<s['vfmt'])
            cx,cy,cw,ch=s['clip']
            if s['primitive']==9:
                if corner is None:corner=(x,y);continue
                x0,y0=corner;corner=None;r=s['line']/16
                # Codex: у новой маски скругления за SCISSOR; при полном заполнении её левый край уже вне окна.
                left=max(0,cx,int(np.ceil(min(x0,x)-r-.5)));right=min(1024,cx+cw,int(np.ceil(max(x0,x)+r-.5)))
                top=max(0,cy,int(np.ceil(min(y0,y)-r-.5)));bottom=min(768,cy+ch,int(np.ceil(max(y0,y)+r-.5)))
                if right>left and bottom>top:
                    xx=np.arange(left,right);yy=np.arange(top,bottom)
                    dx=np.maximum(np.maximum(min(x0,x)-xx,xx-max(x0,x)),0);dy=np.maximum(np.maximum(min(y0,y)-yy,yy-max(y0,y)),0)
                    # Codex: полупокрытие на целой границе RECT сверено с официальным BT8XXEMU FT812.
                    alpha=(np.clip(r+.5-np.sqrt(dy[:,None]**2+dx[None,:]**2),0,1)*s['alpha']).astype(np.int32)
                    src=np.broadcast_to(np.array(s['color'],dtype=np.int32),(bottom-top,right-left,3));paint(left,top,src,alpha)
                continue
            assert s['primitive']==1
            left=max(0,cx,int(np.ceil(x)));right=min(1024,cx+cw,int(np.ceil(x+d['bw'])))
            top=max(0,cy,int(np.ceil(y)));bottom=min(768,cy+ch,int(np.ceil(y+d['oh'])))
            if right<=left or bottom<=top:continue
            sx=((np.arange(left,right)-x)*s['a']/256).astype(int);sy=((np.arange(top,bottom)-y)*s['e']/256).astype(int)
            addr=d['source']+sy[:,None]*d['stride']
            mem=np.frombuffer(ram,dtype=np.uint8)
            if d['format']==7:
                addr=addr+sx[None,:]*2;v=mem[addr].astype(np.int32)|(mem[addr+1].astype(np.int32)<<8)
                src=np.stack([((v>>11)<<3)|((v>>11)>>2),(((v>>5)&63)<<2)|(((v>>5)&63)>>4),((v&31)<<3)|((v&31)>>2)],axis=-1)
                alpha=s['alpha']
            elif d['format']==1:
                bit=(mem[addr+(sx[None,:]>>3)]>>(7-(sx[None,:]&7)))&1
                src=np.broadcast_to(np.array(s['color'],dtype=np.int32),(*bit.shape,3));alpha=bit*s['alpha']
            else:raise AssertionError(('BITMAP_FORMAT',d['format']))
            src=src*np.array(s['color'],dtype=np.int32)//255 if d['format']==7 else src
            paint(left,top,src,alpha)
        else:raise AssertionError(hex(word))
    assert not stack
    return pixels.astype(np.uint8)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    old,oldsyms,before=from_spg(TMP/'baseline_launcher');binary,syms,after=from_spg(BASE/'launcher')
    spg=(BASE/'launcher/_HMM2.SPG').read_bytes();jpeg_parts=[];offsets=[]
    for page in sorted((BASE/'launcher/build').glob('ldrpic_*.bin')):
        data=page.read_bytes();at=spg.find(data);assert at>=0 and spg.count(data)==1
        jpeg_parts.append(spg[at:at+len(data)]);offsets.append(at)
    padded=b''.join(jpeg_parts);jpeg=padded[:padded.rfind(b'\xff\xd9')+2]
    assert jpeg==(BASE/'launcher/build/loader.jpg').read_bytes() and len(jpeg)<=262144
    (OUT/'loader_from_spg.jpg').write_bytes(jpeg)
    im=Image.open(io.BytesIO(jpeg));assert im.size==(1024,768) and JpegImagePlugin.get_sampling(im)==2 and not im.info.get('progressive')
    # Codex: SOF0 проверяем по разобранным маркерам, а не по случайному совпадению в сжатом потоке.
    pos=2;markers=[]
    while jpeg[pos:pos+2]!=b'\xff\xda':
        assert jpeg[pos]==255;marker=jpeg[pos+1];markers.append(marker);length=int.from_bytes(jpeg[pos+2:pos+4],'big');pos+=2+length
    assert 0xc0 in markers and 0xc2 not in markers
    decoder=compile_exe([ROOT/'tests/splash_decode.c',BL/'ft_image.c',BL/'ft_inflate.c'],'decode',[BL])
    decode=[];decoded=None
    for opts,chunk in [(2,1),(2,4096),(10,4096)]:
        target=OUT/f'decoded_{opts}_{chunk}.rgb565'
        info=json.loads(execute([decoder,OUT/'loader_from_spg.jpg',target,opts,chunk],f'decode_{opts}_{chunk}.json'))
        data=target.read_bytes()
        if decoded is None:decoded=data
        else:assert data==decoded
        decode.append(info)
    rgb=np.array(im.convert('RGB'),dtype=np.uint16)
    expected=(((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)).astype('<u2').tobytes()
    assert decoded==expected
    ram=bytearray(2*1024*1024);ram[0x80000:]=decoded
    progress={};lists={}
    for fill in [8,9,20,381,382,761,762]:
        writes,mem=z80_call(binary,syms,'FtBar',[('FtShown',fill,2)])
        data=next(data for addr,data in writes if addr==0x300000);lists[fill]=data
        assert data==mem[syms['FtSplashDl']:syms['FtSplashEnd']]
        assert struct.unpack_from('<I',data,syms['FtSplashFill']-syms['FtSplashDl'])[0]>>30==1
        (OUT/f'spg_{fill}.dl').write_bytes(data)
        pixels=render(data,ram)
        if fill in (8,381,762):Image.fromarray(pixels).save(OUT/f'spg_{fill}.png')
        progress[fill]=pixels
    spec,c=layout();x,y,bot,width=c['LOADER_X'],c['LOADER_TOP'],c['LOADER_BOTTOM'],c['LOADER_FILL']
    for a,b in [(8,9),(381,382),(761,762)]:
        ys,xs=np.where(np.any(progress[a]!=progress[b],axis=2));assert set(xs)==set(range(x+a,min(x+b+1,x+width))) and ys.min()==y and ys.max()==bot-1
        # Codex: граница движется на один пиксель; её сглаживание затрагивает соседний столбец.
        for fill in (a,b):
            dark=np.where(np.any(progress[fill][y]!=progress[762][y],axis=1))[0]
            assert (dark.min()==x+fill) if fill<width else not len(dark)
    assert np.array_equal(progress[762],rgb565(decoded))
    area=np.zeros((768,1024),bool);area[y:bot,x:x+width]=True
    assert not np.any(progress[8][~area]!=progress[762][~area])
    source=(ROOT/'main/hmm_gpu.cpp').read_text(encoding='utf-8');start=source.index('constexpr uint32_t LOADER_MARK=');end=source.index('void video_mode_game()',start)
    actual=source[start:end];(OUT/'loader_actual.inc').write_text(actual,encoding='utf-8')
    loader=compile_exe([ROOT/'tests/splash_loader_harness.cpp',ROOT/'prototype/ft_sprite.c'],'loader',[OUT,ROOT/'main',ROOT/'prototype'])
    execute([loader,OUT/'spg_8.dl',OUT],'loader_execution.log')
    comparisons=[]
    for fill in [8,20,381,762]:
        elf=(OUT/f'elf_{fill}.dl').read_bytes();assert elf==lists[fill]
        pixels=render(elf,ram);assert np.array_equal(pixels,progress[fill])
        if fill!=20:
            Image.fromarray(pixels).save(OUT/f'elf_{fill}.png')
            assert (OUT/f'elf_{fill}.png').read_bytes()==(OUT/f'spg_{fill}.png').read_bytes()
        comparisons.append(dict(fill=fill,words=len(elf)//4,words_equal=True,pixels_equal=True,inherited=(fill==8)))
    errors=[]
    for stage,code,request,explicit in [(1,1,0,False),(3,0xe0,0,False),(7,3,1,False),(12,0xe0,1,False),(4,2,0,True)]:
        images=[];streams=[]
        for tag,b,s in [('before',old,oldsyms),('after',binary,syms)]:
            values=[('FtOk',1,1),('BootStage',stage,1),('BootError',code,1),('LastRequestValid',request,1),('BootMessage',s['MsgFailed'] if explicit else 0,2)]
            writes,mem=z80_call(b,s,'FtError',values);dl=next(data for addr,data in writes if addr==0x300000)
            font=bytearray(2*1024*1024);font[:360]=mem[s['LetterFont']:s['LetterFont']+360]
            pixels=render(dl,font);images.append(pixels);streams.append(dl)
            name=f'error_{stage}_{code:02x}_{tag}';(OUT/(name+'.dl')).write_bytes(dl);Image.fromarray(pixels).save(OUT/(name+'.png'))
        assert streams[0]==streams[1] and np.array_equal(*images)
        errors.append(dict(stage=stage,code=code,request=bool(request),explicit=explicit,words_equal=True,pixels_equal=True))
    for scene in sorted((ROOT/'tools/splash').glob('scene_*.png')):compose(scene.name).save(OUT/(scene.stem+'_composed.png'))
    report=dict(spg_before=before,spg_after=after,jpeg=dict(bytes=len(jpeg),pages=len(jpeg_parts),sha256=sha(jpeg),markers=markers,baseline=True,subsampling='4:2:0',progressive=False,spg_offsets=offsets,decoder=decode,pillow_rgb565_equal=True),
                loader_source_sha256=sha(actual.encode()),handover=comparisons,one_pixel_steps=True,frame_unchanged=True,full_equals_decoded_jpeg=True,error=errors,
                decoder_sources={p.name:sha(p.read_bytes()) for p in [BL/'ft_image.c',BL/'ft_inflate.c',BL/'ft_copro.h']})
    (OUT/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
