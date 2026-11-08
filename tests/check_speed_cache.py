"""Codex: исходный и ускоренный кэш выполняют одинаковые запросы, включая вытеснение и закрепление."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    old=subprocess.check_output(['git','show','HEAD:prototype/ft_sprite.c'],cwd=ROOT).decode('utf-8')
    new=(ROOT/'prototype/ft_sprite.c').read_text(encoding='utf-8')
    header='''#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include "ft_sprite.h"
'''
    tail=r'''
static hft_cache a,b;
static unsigned rng=0x49a72105;
static unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(){before::hft_cache_init(&a);after::hft_cache_init(&b);unsigned requests=0,misses=0,failures=0;
 for(unsigned frame=0;frame<4000;++frame){
  before::hft_cache_begin(&a,frame&1);after::hft_cache_begin(&b,frame&1);
  for(unsigned i=0;i<HFT_ENTRIES;++i)if(!(i%17)){a.entries[i].pins&=~4u;b.entries[i].pins&=~4u;}
  for(unsigned j=0;j<96;++j){
   const unsigned id=next()%1800;const uint64_t key=(uint64_t(id%7)<<60)|id;
   const uint32_t bytes=j%23?1+(id%13)*1024:(id%2?307200:65536);
   uint32_t x=0xfedcba98,y=x;int u=-1,v=-1;
   const int r=before::hft_cache_get(&a,key,bytes,&x,&u),s=after::hft_cache_get(&b,key,bytes,&y,&v);
   ++requests;misses+=r&&u;failures+=!r;
   if(r!=s||x!=y||u!=v||a.last_entry!=b.last_entry){std::printf("mismatch %u/%u\n",frame,j);return 1;}
   if(r&&!(id%17)){a.entries[a.last_entry].pins|=4u;b.entries[b.last_entry].pins|=4u;}
  }
  if(std::memcmp(&a,&b,offsetof(hft_cache,free_entry))){std::printf("state mismatch %u\n",frame);return 2;}
 }
 std::printf("{\"frames\":4000,\"requests\":%u,\"misses\":%u,\"capacity_failures_each\":%u,\"different\":0}\n",requests,misses,failures);
}
'''
    source=a.out/'cache.cpp';source.write_text(header+'namespace before{\n'+old+'}\nnamespace after{\n'+new+'}\n'+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[ROOT/'prototype',vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')]];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')]]
    exe=a.out/'cache.exe'
    with (a.out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(a.out/'cache.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=a.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=a.out,text=True));(a.out/'result.json').write_text(json.dumps(result,indent=2));print(result)

if __name__=='__main__':main()
