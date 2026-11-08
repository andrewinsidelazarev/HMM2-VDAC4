"""Codex: независимая проверка границ маски в официальном FT812 без окна и без Unreal."""
from pathlib import Path
import ctypes as C
import hashlib
import json
import os
import shutil
import struct
import time
import numpy as np
from PIL import Image
from check_splash import render

OUT=Path(os.environ.get('HMM2_TMP','E:/zx/cache/tmp/hmm2_splash'))/'verification'
DLL=OUT/'bt8xxemu.dll'
shutil.copy2('E:/zx/cache/tmp/hmm2_esp/integration_audio/bridgetek_pal8/bt8xxemu.dll',DLL)
lib=C.CDLL(str(DLL));lib.BT8XXEMU_version.restype=C.c_char_p
G=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p,C.c_int,C.POINTER(C.c_uint32),C.c_uint32,C.c_uint32,C.c_int)
L=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_int,C.c_char_p)
class Parameters(C.Structure):
    _fields_=[('Main',C.c_void_p),('Flags',C.c_int),('Mode',C.c_int),('MousePressure',C.c_uint32),('ExternalFrequency',C.c_uint32),('ReduceGraphicsThreads',C.c_uint32),('MCUSleep',C.c_void_p),('RomFilePath',C.c_wchar*260),('OtpFilePath',C.c_wchar*260),('CoprocessorRomFilePath',C.c_wchar*260),('Graphics',G),('Log',L),('Close',C.c_void_p),('UserContext',C.c_void_p),('Flash',C.c_void_p)]
frames=[];logs=[]
@G
def graphics(sender,context,output,buf,w,h,flags):
    if output and flags&2 and w==1024 and h==768:
        frames.append(C.string_at(buf,w*h*4))
        if len(frames)>4:frames.pop(0)
    return 1
@L
def log(sender,context,kind,msg):logs.append([kind,msg.decode(errors='replace')])
lib.BT8XXEMU_defaults.argtypes=[C.c_uint32,C.POINTER(Parameters),C.c_int]
lib.BT8XXEMU_run.argtypes=[C.c_uint32,C.POINTER(C.c_void_p),C.POINTER(Parameters)]
lib.BT8XXEMU_chipSelect.argtypes=[C.c_void_p,C.c_int]
lib.BT8XXEMU_transfer.argtypes=[C.c_void_p,C.c_ubyte];lib.BT8XXEMU_transfer.restype=C.c_ubyte
lib.BT8XXEMU_destroy.argtypes=[C.c_void_p]
p=Parameters();lib.BT8XXEMU_defaults(12,C.byref(p),0x812)
p.Graphics=graphics;p.Log=log;p.Flags=4|0x1000;p.ReduceGraphicsThreads=1
emulator=C.c_void_p();lib.BT8XXEMU_run(12,C.byref(emulator),C.byref(p));assert emulator.value
def spi(data):
    lib.BT8XXEMU_chipSelect(emulator,1)
    result=bytes(lib.BT8XXEMU_transfer(emulator,b) for b in data)
    lib.BT8XXEMU_chipSelect(emulator,0);return result
def write(address,data):spi(bytes([0x80|(address>>16),(address>>8)&255,address&255])+data)
def put(address,value):write(address,struct.pack('<I',value))
def read(address):return int.from_bytes(spi(bytes([(address>>16)&63,(address>>8)&255,address&255,0,0,0,0,0]))[-4:],'little')
try:
    spi(bytes([0x68,0,0]));spi(bytes([0,0,0]));deadline=time.monotonic()+5
    while read(0x302000)&255!=0x7c:
        if time.monotonic()>deadline:raise RuntimeError('FT812 ID timeout')
        time.sleep(.005)
    put(0x302070,0)
    for address,value in {0x30202c:1344,0x302030:296,0x302034:1024,0x302038:0,0x30203c:136,0x302040:806,0x302044:35,0x302048:768,0x30204c:0,0x302050:6,0x302068:0,0x30206c:0}.items():put(address,value)
    checks=[]
    for fill in [8,381,762]:
        original=(OUT/f'spg_{fill}.dl').read_bytes();words=list(struct.unpack('<26I',original))
        # Codex: официальный FT812 имеет только 1 МиБ RAM_G; проверяем настоящие команды маски поверх сплошного фона.
        test=[0x025070a0,0x26000007,0x27000004,0x04ffffff,0x100000ff,*words[13:]]
        data=struct.pack('<'+'I'*len(test),*test);write(0x300000,data);put(0x302054,2);put(0x302070,8)
        deadline=time.monotonic()+8
        while read(0x302054):
            if time.monotonic()>deadline:raise RuntimeError('FT812 swap timeout')
            time.sleep(.005)
        frames.clear()
        while len(frames)<3:
            if time.monotonic()>deadline:raise RuntimeError('FT812 frame timeout')
            time.sleep(.01)
        image=np.array(Image.frombytes('RGBA',(1024,768),frames[-1],'raw','BGRA').convert('RGB'))
        expected=render(data,bytearray(2*1024*1024));difference=np.abs(image.astype(int)-expected.astype(int))
        a=np.any(image!=np.array([80,112,160]),axis=2);b=np.any(expected!=np.array([80,112,160]),axis=2)
        ys,xs=np.where(a)
        info=dict(fill=fill,mask_shape_equal=bool(np.array_equal(a,b)),max_channel_difference=int(difference.max()),pixels=int(a.sum()),bbox=None if not len(xs) else [int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1)])
        checks.append(info);Image.fromarray(image).save(OUT/f'official_mask_{fill}.png')
        assert info['mask_shape_equal'] and info['max_channel_difference']<=1,info
    (OUT/'official_ft812.json').write_text(json.dumps(dict(version=lib.BT8XXEMU_version().decode(),dll_sha256=hashlib.sha256(DLL.read_bytes()).hexdigest(),checks=checks,logs=logs),indent=2)+'\n',encoding='utf-8')
    print(checks)
finally:lib.BT8XXEMU_destroy(emulator)
