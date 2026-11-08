"""Codex: исходное отсечение против таблицы ссылок, включая переполнение 512 записей."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]

def extract(s):
    return s[s.index('HMM_RENDER_NOINLINE void hmm_render_job_cull('):s.index('// Codex: геометрия считается без копирования')]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    old=subprocess.check_output(['git','show','HEAD:main/hmm_canvas.inc'],cwd=ROOT).decode('utf-8')
    new=(ROOT/'main/hmm_canvas.inc').read_text(encoding='utf-8')
    header=r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#define HMM_RENDER_NOINLINE
struct Rect{int x=0,y=0,w=0,h=0;};
Rect intersect(Rect a,Rect b){int r=std::min(a.x+a.w,b.x+b.w),d=std::min(a.y+a.h,b.y+b.h);a.x=std::max(a.x,b.x);a.y=std::max(a.y,b.y);a.w=std::max(0,r-a.x);a.h=std::max(0,d-a.y);return a;}
struct HmmOpaqueRect{uint16_t x,y,w,h;};
struct Draw{Rect r;int sx=0,sy=0,step=1;bool opaque=false;};
struct CullRect{uint64_t silhouette=0;const HmmOpaqueRect*opaque=nullptr;Rect visible;};
struct CullJob{const Draw*draws;CullRect*rects;size_t count;};
'''
    tail=r'''
int main(){std::mt19937 rng(20261104);uint64_t layers=0;
 for(unsigned test=0;test<2000;++test){unsigned count=test%5==0?1200:rng()%500;
  std::vector<Draw> d(count);std::vector<CullRect>a(count),b(count);std::vector<HmmOpaqueRect> opaque(count*4);
  for(unsigned i=0;i<count;++i){d[i].r={int(rng()%800)-80,int(rng()%600)-60,int(rng()%250)+1,int(rng()%250)+1};d[i].sx=int(rng()%128);d[i].sy=int(rng()%64);d[i].step=rng()%2?1:-1;d[i].opaque=rng()%5==0;
   a[i].silhouette=rng()%4?uint64_t(rng()):0;
   if(i&&rng()%3==0){d[i]=d[rng()%i];a[i].silhouette=a[rng()%i].silhouette;}
   if(i&&rng()%3==0){d[i]=d[i-1];a[i].silhouette=a[i-1].silhouette;}
   for(int k=0;k<4;++k)opaque[i*4+k]={uint16_t(rng()%64),uint16_t(rng()%64),uint16_t(rng()%50),uint16_t(rng()%50)};
   if(rng()%3==0)a[i].opaque=opaque.data()+i*4;
  }
  b=a;CullJob x{d.data(),a.data(),count},y{d.data(),b.data(),count};before::hmm_render_job_cull(&x);after::hmm_render_job_cull(&y);
  for(unsigned i=0;i<count;++i)if(std::memcmp(&a[i].visible,&b[i].visible,sizeof(Rect))){std::printf("mismatch %u %u\n",test,i);return 1;}
  layers+=count;
 }
 std::printf("{\"cases\":2000,\"layers\":%llu,\"different\":0}\n",layers);
}
'''
    source=a.out/'cull.cpp';source.write_text(header+'\nnamespace before{\n'+extract(old)+'}\nnamespace after{\n'+extract(new)+'}\n'+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')]];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')]]
    exe=a.out/'cull.exe'
    with (a.out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(a.out/'cull.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=a.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=a.out,text=True));(a.out/'result.json').write_text(json.dumps(result,indent=2));print(result)

if __name__=='__main__':main()
