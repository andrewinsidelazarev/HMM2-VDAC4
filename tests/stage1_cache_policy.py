"""Codex: настоящий LRU кэша прошивки, включая старый OGG и конкурирующий ELF."""
from pathlib import Path
import ctypes as C,hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_stage1 import ROOT,BASE,FW,VC,KIT

def library():
 out=BASE/'joint/cache_policy';out.mkdir(exist_ok=True)
 sources=[ROOT/'tests/stage1_cache_policy.cpp',FW/'src/zm_cache.cpp'];header=FW/'include/zifi/zm_cache.hpp'
 signature=hashlib.sha256(b''.join(p.read_bytes() for p in sources+[header])).hexdigest();dll=out/('cache-'+signature[:12]+'.dll')
 if not dll.exists():
  inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],FW/'include'];objects=[]
  for i,src in enumerate(sources):
   obj=out/(str(i)+'.obj');objects.append(obj)
   cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],'/c',str(src),'/Fo'+str(obj)]
   r=subprocess.run(cmd,capture_output=True);(out/(str(i)+'.log')).write_bytes(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
  libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
  cmd=[str(VC/'bin/Hostx64/x64/link.exe'),'/NOLOGO','/DLL','/OUT:'+str(dll),*map(str,objects),*['/LIBPATH:'+str(p) for p in libs]]
  r=subprocess.run(cmd,capture_output=True);(out/'link.log').write_bytes(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 (out/'sources.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[header]},indent=2),encoding='utf-8')
 d=C.CDLL(str(dll));d.cache_reset.argtypes=[C.c_uint];d.cache_has.argtypes=[C.c_char_p,C.c_uint,C.c_uint];d.cache_fits.argtypes=[C.c_uint,C.c_char_p];d.cache_store.argtypes=[C.c_char_p,C.c_uint,C.c_void_p,C.c_uint,C.c_char_p];d.cache_touch.argtypes=d.cache_contains.argtypes=[C.c_char_p]
 return d
class Policy:
 def __init__(self,ogg,elf,mode):
  self.d=library();self.d.cache_reset(1703936);self.events=[];stamp=0x5b0555aa
  if mode=='eviction':self.store('OGG.ELF',stamp,ogg,None);self.store('OTHER.ELF',stamp,bytes(1048576),None)
  elif mode=='warm':
   # Codex: ELF сборки 88 больше; прежние 80 КиБ вытесняли его ещё при создании «тёплой» фикстуры.
   self.store('HMM2.ELF',stamp,elf,None);self.store('OTHER.ELF',stamp,bytes(32768),None)
   assert self.contains('HMM2.ELF'),'warm fixture evicted the game before the test'
  else:assert mode=='fresh'
 def has(self,n,size,stamp):return bool(self.d.cache_has(n.encode(),size,stamp))
 def fits(self,size,busy):return bool(self.d.cache_fits(size,busy.encode() if busy else None))
 def store(self,n,stamp,data,busy):
  assert self.d.cache_store(n.encode(),stamp,C.create_string_buffer(data),len(data),busy.encode() if busy else None)
  self.events.append(dict(stored=n,busy=busy,ogg_present=self.contains('OGG.ELF'),game_present=self.contains('HMM2.ELF')))
 def touch(self,n):assert self.d.cache_touch(n.encode()),('missing cached app',n)
 def contains(self,n):return bool(self.d.cache_contains(n.encode()))
