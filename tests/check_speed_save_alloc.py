"""Codex: реальный инициализатор SAV с отказом крупных блоков; совместимость потока и отсутствие утечек."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from compile_probe import SDK
from check_speed_save import decode_save


def main():
    ap=argparse.ArgumentParser();ap.add_argument('save',type=Path);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    raw,_=decode_save(args.save);(out/'input.bin').write_bytes(raw)
    source=(ROOT/'main/hmm_save.inc').read_text(encoding='utf-8')
    helper=source[source.index('static int hmm_save_init('):source.index('class HmmZipWriter')]
    header=r'''
#include <zlib.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
struct Budget{size_t limit=0,live=0,peak=0,denied=0;std::map<void*,size_t>blocks;};
static voidpf alloc(voidpf opaque,uInt count,uInt width){
 auto &b=*static_cast<Budget*>(opaque);size_t n=size_t(count)*width;
 if(b.limit&&n>b.limit){++b.denied;return nullptr;}
 void*p=std::calloc(count,width);if(p){b.blocks[p]=n;b.live+=n;b.peak=std::max(b.peak,b.live);}return p;
}
static void release(voidpf opaque,voidpf p){if(!p)return;auto &b=*static_cast<Budget*>(opaque);auto i=b.blocks.find(p);assert(i!=b.blocks.end());b.live-=i->second;b.blocks.erase(i);std::free(p);}
'''
    tail=r'''
int main(){
 FILE*f=std::fopen("input.bin","rb");assert(f);std::fseek(f,0,SEEK_END);size_t n=std::ftell(f);std::rewind(f);
 std::vector<unsigned char>raw(n),packed(n*2+65536),decoded(n);assert(std::fread(raw.data(),1,n,f)==n);std::fclose(f);
 std::puts("[");unsigned test=0;
 for(size_t limit: {size_t(0),size_t(65535),size_t(32767),size_t(16383)}){
  Budget b;b.limit=limit;z_stream z{};z.zalloc=alloc;z.zfree=release;z.opaque=&b;
  int result=hmm_save_init(&z);size_t bytes=0;unsigned window=0;
  if(limit==16383){assert(result==Z_MEM_ERROR&&z.state==nullptr);}
  else{
   assert(result==Z_OK);z.next_in=raw.data();z.avail_in=unsigned(raw.size());z.next_out=packed.data();z.avail_out=unsigned(packed.size());
   assert(deflate(&z,Z_FINISH)==Z_STREAM_END);bytes=z.total_out;window=8+(packed[0]>>4);assert(window==(limit==32767?13:limit?14:15));
   uLongf length=decoded.size();assert(uncompress(decoded.data(),&length,packed.data(),bytes)==Z_OK&&length==raw.size()&&decoded==raw);
   assert(deflateEnd(&z)==Z_OK);
   char name[40];std::snprintf(name,sizeof(name),"window%u.z",window);f=std::fopen(name,"wb");assert(f);assert(std::fwrite(packed.data(),1,bytes,f)==bytes);std::fclose(f);
  }
  assert(b.live==0&&b.blocks.empty());if(test++)std::puts(",");
  std::printf("{\"maximum_block\":%zu,\"peak_encoder_bytes\":%zu,\"denied\":%zu,\"result\":%d,\"compressed_bytes\":%zu,\"window_bits\":%u,\"leaked_bytes\":%zu}",limit,b.peak,b.denied,result,bytes,window,b.live);
 }
 std::puts("\n]");
}
'''
    cpp=out/'save_alloc.cpp';cpp.write_text(header+helper+tail,encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');kit=Path('C:/Program Files (x86)/Windows Kits/10')
    inc=[vc/'include',*[kit/'Include/10.0.26100.0'/p for p in ('ucrt','um','shared')],SDK];libs=[vc/'lib/x64',*[kit/'Lib/10.0.26100.0'/p/'x64' for p in ('ucrt','um')],SDK.parent/'lib/x64']
    exe=out/'save_alloc.exe'
    with (out/'build.log').open('wb') as log:subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(cpp),'/Fo'+str(out/'save_alloc.obj'),'/Fe'+str(exe),'/link',*['/LIBPATH:'+str(p) for p in libs],'zlib.lib'],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    for dll in (SDK.parent/'lib/x64').glob('*.dll'):shutil.copy2(dll,out/dll.name)
    result=json.loads(subprocess.check_output([str(exe)],cwd=out,text=True));(out/'result.json').write_text(json.dumps(result,indent=2));print(result)


if __name__=='__main__':main()
