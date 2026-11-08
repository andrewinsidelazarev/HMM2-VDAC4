# Codex: контроль PAL8 в официальной DLL Bridgetek, без окна и без изменений игры.
from pathlib import Path
import ctypes as C,shutil,struct,time,json,hashlib,sys,threading
base=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/bridgetek_pal8');base.mkdir(exist_ok=True)
source=Path('E:/zx/HMM2/Diagnostics/unreal-wc-current-20260925-224835/bt8xxemu.dll');dll=base/'bt8xxemu.dll';shutil.copy2(source,dll)
lib=C.CDLL(str(dll));lib.BT8XXEMU_version.restype=C.c_char_p;version=lib.BT8XXEMU_version().decode(errors='replace');print('Bridgetek',version,flush=True)
G=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p,C.c_int,C.POINTER(C.c_uint32),C.c_uint32,C.c_uint32,C.c_int)
L=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_int,C.c_char_p)
class P(C.Structure):
 _fields_=[('Main',C.c_void_p),('Flags',C.c_int),('Mode',C.c_int),('MousePressure',C.c_uint32),('ExternalFrequency',C.c_uint32),('ReduceGraphicsThreads',C.c_uint32),('MCUSleep',C.c_void_p),('RomFilePath',C.c_wchar*260),('OtpFilePath',C.c_wchar*260),('CoprocessorRomFilePath',C.c_wchar*260),('Graphics',G),('Log',L),('Close',C.c_void_p),('UserContext',C.c_void_p),('Flash',C.c_void_p)]
frames=[];logs=[]
@G
def graphics(sender,context,output,buf,w,h,flags):
 if output and flags&2 and w==320 and h==120:
  frames.append(C.string_at(buf,w*h*4))
  if len(frames)>4:frames.pop(0)
 return 1
@L
def log(sender,context,kind,msg):logs.append([kind,msg.decode(errors='replace')])
lib.BT8XXEMU_defaults.argtypes=[C.c_uint32,C.POINTER(P),C.c_int];lib.BT8XXEMU_run.argtypes=[C.c_uint32,C.POINTER(C.c_void_p),C.POINTER(P)];lib.BT8XXEMU_chipSelect.argtypes=[C.c_void_p,C.c_int];lib.BT8XXEMU_transfer.argtypes=[C.c_void_p,C.c_ubyte];lib.BT8XXEMU_transfer.restype=C.c_ubyte;lib.BT8XXEMU_destroy.argtypes=[C.c_void_p]
p=P();lib.BT8XXEMU_defaults(12,C.byref(p),0x812);p.Graphics=graphics;p.Log=log;p.Flags=4|0x1000;p.ReduceGraphicsThreads=1;e=C.c_void_p();lib.BT8XXEMU_run(12,C.byref(e),C.byref(p));assert e.value
print('started',flush=True)
def spi(data):
 lib.BT8XXEMU_chipSelect(e,1);r=bytes(lib.BT8XXEMU_transfer(e,b) for b in data);lib.BT8XXEMU_chipSelect(e,0);return r
def write(a,data):spi(bytes([0x80|(a>>16),(a>>8)&255,a&255])+data)
def put(a,v):write(a,struct.pack('<I',v))
def read(a):return int.from_bytes(spi(bytes([(a>>16)&63,(a>>8)&255,a&255,0,0,0,0,0]))[-4:],'little')
try:
 spi(bytes([0x68,0,0]));spi(bytes([0,0,0]));until=time.monotonic()+5
 while read(0x302000)&255!=0x7c:
  if time.monotonic()>until:raise RuntimeError('FT812 ID timeout')
  time.sleep(.005)
 print('ID ready',flush=True)
 mode={0x302070:0,0x30202c:408,0x302030:70,0x302034:320,0x302038:0,0x30203c:10,0x302040:132,0x302044:12,0x302048:120,0x30204c:0,0x302050:2,0x302068:0,0x30206c:0,0x302070:8}
 put(0x302070,0)
 for a,v in mode.items():
  if a!=0x302070:put(a,v)
 colors=[(240,32,32,255),(32,220,64,255),(64,64,220,255),(240,220,48,255)];pal=bytearray(1024)
 for i,(r,g,b,a) in enumerate(colors):pal[i*4:i*4+4]=bytes([b,g,r,a])
 write(0x2000,pal);write(0,bytes(x//32 for y in range(64) for x in range(128)))
 def vertex(x,y):return 0x80000000|(x<<21)|(y<<12)
 dl=[0x02000000,0x26000007,0x04ffffff,0x05000000,0x01000000,0x07000000|(16<<19)|(128<<9)|64,0x08000000|(128<<9)|64,0x1f000001,0x2000000f,0x0b000014,0x2a002000,vertex(16,28),0x0b000008,0x20000001,0x2a002003,vertex(176,28),0x0b00001d,0x20000008,0x2a002002,vertex(176,28),0x20000004,0x2a002001,vertex(176,28),0x20000002,0x2a002000,vertex(176,28),0x21000000,0]
 write(0x300000,struct.pack('<'+'I'*len(dl),*dl));put(0x302054,2);put(0x302070,8);until=time.monotonic()+8
 while len(frames)<3:
  if time.monotonic()>until:raise RuntimeError('Complete frame timeout '+str(logs))
  time.sleep(.01)
 raw=frames[-1];(base/'frame.bgra').write_bytes(raw);samples=[]
 for x in [32,64,96,128,192,224,256,288]:
  b,g,r,a=raw[(48*320+x)*4:(48*320+x)*4+4];samples.append([r,g,b,a])
 assert all(r==g==b for r,g,b,a in samples[:4])
 assert samples[4:]==[list(c) for c in colors]
 result=dict(version=version,dll_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),mode='FT812',width=320,height=120,one_pass=samples[:4],four_pass=samples[4:],source_colors=colors,logs=logs)
 (base/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result),flush=True)
 sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python');from PIL import Image
 Image.frombytes('RGBA',(320,120),raw,'raw','BGRA').convert('RGB').save(base/'pal8_official.png')
finally:lib.BT8XXEMU_destroy(e)
