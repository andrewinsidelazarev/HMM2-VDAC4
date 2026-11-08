"""Codex: официальный BT8XXEMU v4.8.2, FT812/API12, driverless. Всё хранится на E:."""
from pathlib import Path
import ctypes as C,shutil,time,hashlib
G=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p,C.c_int,C.POINTER(C.c_uint32),C.c_uint32,C.c_uint32,C.c_int)
L=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_int,C.c_char_p)
class Params(C.Structure):
 _fields_=[('Main',C.c_void_p),('Flags',C.c_int),('Mode',C.c_int),('MousePressure',C.c_uint32),('ExternalFrequency',C.c_uint32),('ReduceGraphicsThreads',C.c_uint32),('MCUSleep',C.c_void_p),('RomFilePath',C.c_wchar*260),('OtpFilePath',C.c_wchar*260),('CoprocessorRomFilePath',C.c_wchar*260),('Graphics',G),('Log',L),('Close',C.c_void_p),('UserContext',C.c_void_p),('Flash',C.c_void_p)]
class Bridgetek:
 def __init__(self,out,width=640,height=480):
  out=Path(out);out.mkdir(parents=True,exist_ok=True);source=Path('E:/zx/HMM2/Diagnostics/unreal-wc-current-20260925-224835/bt8xxemu.dll');dll=out/'bt8xxemu.dll';shutil.copy2(source,dll);self.digest=hashlib.sha256(dll.read_bytes()).hexdigest();self.lib=C.CDLL(str(dll));self.frames=[];self.logs=[]
  @G
  def graphics(sender,context,output,buffer,w,h,flags):
   if output and flags&2 and w==width and h==height:self.frames.append(C.string_at(buffer,w*h*4));self.frames[:]=self.frames[-4:]
   return 1
  @L
  def log(sender,context,kind,msg):self.logs.append([kind,msg.decode(errors='replace')])
  self.graphics,self.log=graphics,log;lib=self.lib;lib.BT8XXEMU_version.restype=C.c_char_p;self.version=lib.BT8XXEMU_version().decode(errors='replace')
  lib.BT8XXEMU_defaults.argtypes=[C.c_uint32,C.POINTER(Params),C.c_int];lib.BT8XXEMU_run.argtypes=[C.c_uint32,C.POINTER(C.c_void_p),C.POINTER(Params)];lib.BT8XXEMU_chipSelect.argtypes=[C.c_void_p,C.c_int];lib.BT8XXEMU_transfer.argtypes=[C.c_void_p,C.c_ubyte];lib.BT8XXEMU_transfer.restype=C.c_ubyte;lib.BT8XXEMU_destroy.argtypes=[C.c_void_p]
  p=Params();lib.BT8XXEMU_defaults(12,C.byref(p),0x812);p.Graphics=graphics;p.Log=log;p.Flags=4|0x1000;p.ReduceGraphicsThreads=1;self.e=C.c_void_p();lib.BT8XXEMU_run(12,C.byref(self.e),C.byref(p));assert self.e.value
  self.spi(bytes([0x68,0,0]));self.spi(bytes([0,0,0]));until=time.monotonic()+5
  while self.read(0x302000)&255!=0x7c:
   if time.monotonic()>until:raise RuntimeError('FT812 ID timeout')
   time.sleep(.005)
 def spi(self,data):
  self.lib.BT8XXEMU_chipSelect(self.e,1);r=bytes(self.lib.BT8XXEMU_transfer(self.e,b) for b in data);self.lib.BT8XXEMU_chipSelect(self.e,0);return r
 def write(self,a,data):self.spi(bytes([0x80|(a>>16),(a>>8)&255,a&255])+data)
 def put(self,a,value):self.write(a,int(value).to_bytes(4,'little'))
 def read(self,a):return int.from_bytes(self.spi(bytes([(a>>16)&63,(a>>8)&255,a&255,0,0,0,0,0]))[-4:],'little')
 def capture(self,ram,dl):
  self.put(0x302070,0)
  for a,v in {0x30202c:800,0x302030:144,0x302034:640,0x302038:0,0x30203c:96,0x302040:525,0x302044:35,0x302048:480,0x30204c:0,0x302050:2,0x302068:0,0x30206c:0}.items():self.put(a,v)
  self.write(0,ram);self.write(0x300000,dl);self.frames.clear();self.put(0x302054,2);self.put(0x302070,2);until=time.monotonic()+10
  while len(self.frames)<3:
   if time.monotonic()>until:raise RuntimeError('Complete FT812 frame timeout '+str(self.logs))
   time.sleep(.01)
  return self.frames[-1]
 def close(self):self.lib.BT8XXEMU_destroy(self.e)
