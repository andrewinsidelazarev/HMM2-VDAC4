"""Codex: календарь RTC против независимого datetime: форматы, високосные даты, полночь, долгий uptime."""
from pathlib import Path
import ctypes as C,datetime as D,json,random,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT
OUT=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/clock_checks')
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 fixture=OUT/'clock_fixture.cpp';fixture.write_text('#include <stdint.h>\nstatic uint64_t ticks;\nextern "C" uint64_t hmm_millis(){return ticks;}\nextern "C" void hmm_log(const char*){}\nextern "C" __declspec(dllexport) void clock_ticks(uint64_t value){ticks=value;}\n',encoding='utf-8')
 inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],ROOT/'main'];objs=[]
 for source in [ROOT/'main/hmm_clock.cpp',fixture]:
  obj=OUT/(source.stem+'.obj');objs.append(obj)
  p=subprocess.run([str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/std:c++17','/utf-8',*['/I'+str(x) for x in inc],'/c',str(source),'/Fo'+str(obj)],capture_output=True)
  assert p.returncode==0,(p.stdout+p.stderr).decode(errors='replace')
 dll=OUT/'clock.dll';libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
 p=subprocess.run([str(VC/'bin/Hostx64/x64/link.exe'),'/NOLOGO','/DLL','/OUT:'+str(dll),*map(str,objs),*['/LIBPATH:'+str(x) for x in libs],*['/EXPORT:'+n for n in ['hmm_clock_reset','hmm_clock_sync','hmm_wall_seconds','hmm_time']]],capture_output=True)
 assert p.returncode==0,(p.stdout+p.stderr).decode(errors='replace')
 lib=C.CDLL(str(dll));lib.clock_ticks.argtypes=[C.c_uint64];lib.hmm_clock_sync.argtypes=[C.POINTER(C.c_uint8),C.c_size_t];lib.hmm_clock_sync.restype=C.c_int
 lib.hmm_wall_seconds.restype=C.c_uint64;lib.hmm_time.argtypes=[C.POINTER(C.c_int64)];lib.hmm_time.restype=C.c_int64
 rng=random.Random(233);dates=[D.datetime(2000,2,29,23,59,59),D.datetime(2024,2,28,23,59,59),D.datetime(2026,10,29,14),D.datetime(2038,1,19,3,14,8),D.datetime(2099,12,31,23,59,59)]
 dates += [D.datetime(2000,1,1)+D.timedelta(seconds=rng.randrange(3155760000)) for _ in range(250)]
 cases=0
 for value in dates:
  expected=int(value.replace(tzinfo=D.timezone.utc).timestamp())
  for binary in [False,True]:
   enc=(lambda x:x) if binary else (lambda x:x//10*16+x%10)
   for day24 in [False,True]:
    hour=value.hour if day24 else value.hour%12 or 12
    packet=[(4 if binary else 0)|(2 if day24 else 0),enc(value.second),enc(value.minute),enc(hour)|(128 if not day24 and value.hour>=12 else 0),enc(value.day),enc(value.month),enc(value.year-2000),128]
    raw=(C.c_uint8*8)(*packet);lib.hmm_clock_reset();lib.clock_ticks(2**32-500)
    assert lib.hmm_clock_sync(raw,8)==1,packet
    assert lib.hmm_wall_seconds()==expected,(value,packet,lib.hmm_wall_seconds(),expected)
    lib.clock_ticks(2**32+1500);assert lib.hmm_wall_seconds()==expected+2
    out=C.c_int64();assert lib.hmm_time(C.byref(out))==out.value==expected+2;cases+=1
 for packet in [[2,0,0,0,0x30,0x02,0x26,128],[2,0x6a,0,0,1,1,0,128],[2,0,0,0,1,0x13,0,128],[2,0,0,0,1,1,0,0],[130,0,0,0,1,1,0,128]]:
  assert lib.hmm_clock_sync((C.c_uint8*8)(*packet),8)==0,packet
 lib.hmm_clock_reset();assert lib.hmm_wall_seconds()==0
 result=dict(calendar_cases=cases,invalid_cases=5,bcd=True,binary=True,hour12=True,hour24=True,leap_and_midnight=True,uptime_over_32bits=True,unsynchronized_is_unknown=True)
 (OUT/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
