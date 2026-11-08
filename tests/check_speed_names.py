"""Codex: одинаковые имена и обращения к переводчику для всех кодов объектов."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from patch_speed import patch

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    old=(ROOT/'third_party/openhmm2/src/fheroes2/maps/mp2.cpp').read_text(encoding='utf-8');new=patch('mp2.cpp',old)
    def body(s,name):
        return s[s.index('const char * MP2::StringObject('):s.index('\nbool MP2::isDayLife(')].replace('MP2::StringObject(',name+'(',1)
    source=r'''
#include "mp2.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
using namespace MP2;
std::string event;
const char *translate(const char*p){event="T:"+std::string(p);return p;}
const char *plural(const char*a,const char*b,size_t n){event="N:"+std::string(a)+"|"+b+"|"+std::to_string(n);return n==1?a:b;}
#define _(x) translate(x)
#define _n(a,b,n) plural(a,b,n)
#define DEBUG_LOG(...)
'''+body(old,'before')+body(new,'after')+r'''
int main(){unsigned cases=0;for(unsigned code=0;code<1024;++code)for(int count:{-1,0,1,2,5,INT_MAX}){
 event.clear();const char*a=before(static_cast<MapObjectType>(code),count);auto old_event=event;
 event.clear();const char*b=after(static_cast<MapObjectType>(code),count);
 if(bool(a)!=bool(b)||(a&&std::string(a)!=b)||event!=old_event){std::printf("mismatch code=%u count=%d\n",code,count);return 1;}++cases;
 }std::printf("{\"cases\":%u,\"same_names\":true,\"same_translation_calls\":true}\n",cases);}
'''
    cpp=a.out/'names.cpp';cpp.write_text(source,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',kit/'Include/10.0.26100.0/ucrt',kit/'Include/10.0.26100.0/um',kit/'Include/10.0.26100.0/shared',ROOT/'third_party/openhmm2/src/fheroes2/maps']
    libs=[vc/'lib/x64',kit/'Lib/10.0.26100.0/ucrt/x64',kit/'Lib/10.0.26100.0/um/x64'];exe=a.out/'names.exe'
    cmd=[str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/DNDEBUG','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(cpp),'/Fo'+str(a.out/'names.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs]]
    with (a.out/'build.log').open('wb') as log:subprocess.run(cmd,cwd=a.out,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.check_output([str(exe)],cwd=a.out,text=True);(a.out/'result.json').write_text(json.dumps(json.loads(result),indent=2));print(result)

if __name__=='__main__':main()
