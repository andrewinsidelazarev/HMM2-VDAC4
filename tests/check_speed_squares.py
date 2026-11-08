"""Codex: координаты и порядок частей оригинала против прямого заполнения списка."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from patch_map_squares import HELPER

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    original=(ROOT/'third_party/openhmm2/src/engine/image.cpp').read_text(encoding='utf-8')
    original=original[original.index('    void DivideImageBySquares('):original.index('    Image ExtractCommonPattern(')]
    header=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <tuple>
#include <vector>
static uint64_t allocations;
void*operator new(size_t n){++allocations;void*p=std::malloc(n);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
namespace fheroes2{
constexpr int32_t tileWidthPx=32;
struct Point{int32_t x=0,y=0;Point()=default;Point(int32_t a,int32_t b):x(a),y(b){}Point operator+(Point b)const{return{x+b.x,y+b.y};}};
struct Rect{int32_t x,y,width,height;Rect(int32_t a,int32_t b,int32_t c,int32_t d):x(a),y(b),width(c),height(d){}
Rect operator^(Rect b)const{int32_t l=std::max(x,b.x),t=std::max(y,b.y),r=std::min(x+width,b.x+b.width),d=std::min(y+height,b.y+b.height);return{l,t,std::max(0,r-l),std::max(0,d-t)};}};
struct Image{int32_t w,h;bool empty()const{return w<=0||h<=0;}int32_t width()const{return w;}int32_t height()const{return h;}};
struct ObjectRenderingInfo{Point square,offset;Rect roi;int id;uint32_t index;bool flip;uint8_t alpha;
ObjectRenderingInfo(Point a,Point b,Rect c,int d,uint32_t e,bool f,uint8_t g):square(a),offset(b),roi(c),id(d),index(e),flip(f),alpha(g){}
auto values()const{return std::make_tuple(square.x,square.y,offset.x,offset.y,roi.x,roi.y,roi.width,roi.height,id,index,flip,alpha);}};
}
'''
    tail=r'''
static uint32_t rng=0xc0d2;
unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(){uint64_t parts=0,old_alloc=0,new_alloc=0;
for(int test=0;test<10000;++test){std::vector<fheroes2::ObjectRenderingInfo>a,b;
for(int append=0;append<2;++append){
 const fheroes2::Image im{int32_t(next()%130),int32_t(next()%130)};
 const fheroes2::Point offset{int32_t(next()%2001)-1000,int32_t(next()%2001)-1000};
 auto start=allocations;
 {std::vector<fheroes2::Point>s;std::vector<std::pair<fheroes2::Point,fheroes2::Rect>>p;
 fheroes2::DivideImageBySquares(offset,im,32,s,p);
 for(size_t i=0;i<s.size();++i)a.emplace_back(s[i],p[i].first,p[i].second,3,test,append!=0,255);}
 old_alloc+=allocations-start;start=allocations;
 hmmAppendSquares(b,offset,im,3,test,append!=0);new_alloc+=allocations-start;
}
if(a.size()!=b.size())return 1;for(size_t i=0;i<a.size();++i)if(a[i].values()!=b[i].values())return 2;parts+=a.size();
}
std::printf("{\"cases\":10000,\"parts\":%llu,\"old_allocations\":%llu,\"new_allocations\":%llu,\"different\":0}\n",parts,old_alloc,new_alloc);
}
'''
    source=a.out/'squares.cpp';source.write_text(header+'namespace fheroes2{\n'+original+'}\n'+HELPER+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')]];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')]]
    exe=a.out/'squares.exe'
    with (a.out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(a.out/'squares.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=a.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=a.out,text=True));(a.out/'result.json').write_text(json.dumps(result,indent=2));print(result)

if __name__=='__main__':main()
