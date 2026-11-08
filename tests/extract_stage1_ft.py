"""Codex: сохраняем выбранные кадры полного буфера из трассы стенда для осмотра."""
from pathlib import Path
import struct,sys,zipfile
from test_ft_trace import png
from check_stage1_gpu import render
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1/qemu_stage1')
if '--trace' in sys.argv:
    i=sys.argv.index('--trace');trace=Path(sys.argv[i+1]);del sys.argv[i:i+2]
else:trace=BASE/'menu.ft'
data=zipfile.ZipFile(trace).read('menu.ft') if trace.suffix=='.zip' else trace.read_bytes()
ram=bytearray(3*1024*1024);pos=frame=0;dl=[]
chosen=set(map(int,sys.argv[1:])) or {30,60,90,120,150,180,200}
while pos<len(data):
    addr,count=struct.unpack_from('<II',data,pos);pos+=8;raw=data[pos:pos+count*4];pos+=len(raw)
    if addr<0x300000:ram[addr:addr+len(raw)]=raw
    elif addr==0x300000:dl=list(struct.unpack('<'+'I'*count,raw))
    elif addr==0x302054:
        frame+=1
        if frame in chosen:
            idx,pal,_,_=render(dl,ram);rgb=b''.join(pal[k*3:k*3+3] for k in idx);png(BASE/f'inspect_{frame:04}.png',rgb,640,480)
            print(frame,BASE/f'inspect_{frame:04}.png')
