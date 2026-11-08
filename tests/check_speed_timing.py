"""Codex: таймеры порта до/после, включая pass, границы задержек и перенос 32-битных часов."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from patch_timing import patch


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    original=(ROOT/'third_party/openhmm2/src/engine/timing.h').read_text(encoding='utf-8')
    before=original.replace('std::chrono::steady_clock::now()','std::chrono::steady_clock::time_point(std::chrono::milliseconds(hmm_millis()))').replace('namespace fheroes2','namespace before')
    after=patch(original).replace('namespace fheroes2','namespace after')
    (out/'hmm_platform.h').write_text('#include <cstdint>\nuint64_t hmm_millis();\n')
    header='''#include <cstdint>
#include <cstdio>
#include <cstring>
static uint64_t clock_ms,reads;
uint64_t hmm_millis(){++reads;return clock_ms;}
'''
    tail=r'''
static uint64_t rng=0xc0d3;
static uint64_t next(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(){
 for(unsigned test=0;test<100000;test++){
  clock_ms=test<16?test:next()%(UINT64_C(1)<<42);
  before::Time a;after::Time b;
  const uint64_t delay=next()%1000000;
  before::TimeDelay c(delay);after::TimeDelay d(delay);
  for(unsigned step=0;step<10;step++){
   if(step==1){c.pass();d.pass();}
   if(step==3){c.reset();d.reset();a.reset();b.reset();}
   if(step==4){c.setDelay(delay+1);d.setDelay(delay+1);}
   clock_ms+=step<3?step:next()%1000;
   if(a.getMs()!=b.getMs()||a.getS()!=b.getS()||c.isPassed()!=d.isPassed()||c.isPassed(0)!=d.isPassed(0)||c.getDelay()!=d.getDelay())return 1;
   const auto elapsed=a.getMs();
   if(c.isPassed(elapsed)!=d.isPassed(elapsed))return 2;
   auto r=reads;a.getMs();if(reads!=r+1)return 3;r=reads;b.getMs();if(reads!=r+1)return 4;
  }
 }
 clock_ms=UINT64_C(0xffffffff)-3;before::Time a;after::Time b;clock_ms+=100;
 if(a.getMs()!=100||b.getMs()!=100)return 5;
 std::puts("{\"cases\":100000,\"steps\":1000000,\"different\":0,\"clock_reads_preserved\":true,\"wrap32\":true}");
}
'''
    source=out/'timing.cpp';source.write_text(header+before+'\n'+after+'\n'+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')]];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')]]
    exe=out/'timing.exe'
    with (out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(out/'timing.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=out,text=True));(out/'result.json').write_text(json.dumps(result,indent=2));print(result)


if __name__=='__main__':main()
