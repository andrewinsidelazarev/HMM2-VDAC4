"""Codex: дифференциальная проверка нормализации против исходной копии, включая владение Bitmap."""
from pathlib import Path
import argparse,json,os,subprocess

ROOT=Path(__file__).resolve().parents[1]

def functions(source):
    return source[source.index('void repeat_fog('):source.index('struct CullRect')]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--reference',type=Path)
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    old=args.reference.read_text(encoding='utf-8') if args.reference else subprocess.check_output(['git','show','HEAD:main/hmm_canvas.inc'],cwd=ROOT).decode('utf-8')
    new=(ROOT/'main/hmm_canvas.inc').read_text(encoding='utf-8')
    header=r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <vector>
static uint64_t allocations,copies,bitmap_copies;
void* operator new(size_t n){++allocations;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
struct Rect{int x=0,y=0,w=0,h=0;};
Rect intersect(Rect a,Rect b){int r=std::min(a.x+a.w,b.x+b.w),d=std::min(a.y+a.h,b.y+b.h);a.x=std::max(a.x,b.x);a.y=std::max(a.y,b.y);a.w=std::max(0,r-a.x);a.h=std::max(0,d-a.y);return a;}
struct Bitmap{unsigned id;int w=128,h=128;bool single=true,frame=false;};
struct Data{Rect r;int sx=0,sy=0,step=1;uint32_t resource=0;unsigned mapping=0;bool raw=false,opaque=false,fill=false,repeat=false,cpu=false,layer=false;uint8_t color=0,mask=0;};
struct Draw:Data{
 std::shared_ptr<Bitmap> bitmap;
 Draw()=default;Draw(Draw&&)=default;Draw&operator=(Draw&&)=default;
 Draw(const Draw&d):Data(d),bitmap(d.bitmap){++copies;bitmap_copies+=bool(bitmap);}
 Draw&operator=(const Draw&d){static_cast<Data&>(*this)=d;bitmap=d.bitmap;++copies;bitmap_copies+=bool(bitmap);return *this;}
};
struct Ft{unsigned flags;};Ft hmm_resource_ft(uint32_t key,bool){return {(key>>16)==1?4u:0u};}
bool equal(const Draw&a,const Draw&b){return a.r.x==b.r.x&&a.r.y==b.r.y&&a.r.w==b.r.w&&a.r.h==b.r.h&&a.sx==b.sx&&a.sy==b.sy&&a.step==b.step&&a.resource==b.resource&&a.mapping==b.mapping&&a.raw==b.raw&&a.opaque==b.opaque&&a.fill==b.fill&&a.repeat==b.repeat&&a.cpu==b.cpu&&a.layer==b.layer&&a.color==b.color&&a.mask==b.mask&&a.bitmap==b.bitmap;}
'''
    tail=r'''
int main(){
 std::mt19937 rng(20261103);std::array<std::shared_ptr<Bitmap>,8> images;
 for(unsigned i=0;i<images.size();++i){images[i]=std::make_shared<Bitmap>();images[i]->id=i;images[i]->frame=i==7;}
 uint64_t old_alloc=0,new_alloc=0,old_copy=0,new_copy=0,old_bitmap=0,new_bitmap=0,layers=0;
 for(unsigned test=0;test<5000;++test){
  std::vector<Draw> a;unsigned n=test%11==0?2048:rng()%400;
  for(unsigned i=0;i<n;++i){Draw d;d.r={int(rng()%20)*32,int(rng()%15)*32,32,32};
   d.resource=((rng()%3+1)<<16)|(rng()%4);d.sx=int(rng()%4)*32;d.sy=0;d.step=rng()%9==0?-1:1;
   d.mapping=rng()%3;d.opaque=rng()%3==0;d.fill=rng()%11==0;d.raw=rng()%13==0;d.color=rng()%17==0?128:0;d.layer=rng()%29==0;
   if(rng()%3==0){d.bitmap=images[rng()%images.size()];d.resource=0;}
   if(i&&rng()%3==0){d=a.back();d.r.x+=d.r.w;d.sx+=d.r.w*d.step;}
   if(i%31==0&&d.bitmap){d.r.w=d.r.h=128;d.opaque=true;}
   a.push_back(std::move(d));
  }
  layers+=n;auto b=a;
  allocations=copies=bitmap_copies=0;before::repeat_fog(a);before::join_layers(a);old_alloc+=allocations;old_copy+=copies;old_bitmap+=bitmap_copies;
  allocations=copies=bitmap_copies=0;after::repeat_fog(b);after::join_layers(b);new_alloc+=allocations;new_copy+=copies;new_bitmap+=bitmap_copies;
  if(a.size()!=b.size()||b.capacity()>a.capacity()){std::printf("size/capacity mismatch case=%u\n",test);return 1;}
  for(size_t i=0;i<a.size();++i)if(!equal(a[i],b[i])){std::printf("layer mismatch case=%u index=%zu\n",test,i);return 1;}
 }
 std::printf("{\"cases\":5000,\"layers\":%llu,\"old_allocations\":%llu,\"new_allocations\":%llu,\"old_Draw_copies\":%llu,\"new_Draw_copies\":%llu,\"old_Bitmap_ref_copies\":%llu,\"new_Bitmap_ref_copies\":%llu}\n",layers,old_alloc,new_alloc,old_copy,new_copy,old_bitmap,new_bitmap);
 return new_alloc>=old_alloc||new_copy?2:0;
}
'''
    source=args.out/'layers.cpp';source.write_text(header+'\nnamespace before {\n'+functions(old)+'\n}\nnamespace after {\n'+functions(new)+'\n}\n'+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
    kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',kit/'Include/10.0.26100.0/ucrt',kit/'Include/10.0.26100.0/um',kit/'Include/10.0.26100.0/shared']
    libs=[vc/'lib/x64',kit/'Lib/10.0.26100.0/ucrt/x64',kit/'Lib/10.0.26100.0/um/x64']
    exe=args.out/'layers.exe'
    cmd=[str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(source),'/Fo'+str(args.out/'layers.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]]
    with (args.out/'build.log').open('wb') as log:subprocess.run(cmd,cwd=args.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.check_output([str(exe)],cwd=args.out,text=True)
    data=json.loads(result);(args.out/'result.json').write_text(json.dumps(data,indent=2));print(result)

if __name__=='__main__':main()
