from pathlib import Path
import sys,os,subprocess
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'));from build_stage1 import VC,KIT
out=Path(os.environ.get('HMM2_STAGE1_BASE',str(ROOT/'stage')))/'ring_copy_unit';out.mkdir(parents=True,exist_ok=True)
code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>
#include <cstdio>
#include "ft_sprite.h"
#define HMM_SIZE_RENDER
constexpr unsigned RING_BG_COLS=21,RING_BG_ROWS=16,RING_BG_BYTES=16384;
struct RingCell {uint64_t id;};
static hft_cache textures;static uint16_t map_texture_refs[HFT_ENTRIES];
static uint64_t ring_background_ids[2][RING_BG_COLS*RING_BG_ROWS];
static unsigned busy,sent,waited;static bool fail_wait;static uint8_t ram[0x300000];
struct GpuBusy {GpuBusy(){++busy;}~GpuBusy(){--busy;}};
void copro_send(const uint32_t *p,unsigned count){assert(busy==1&&count&&count<=128&&count%4==0);for(unsigned i=0;i<count;i+=4){assert(p[i]==0xffffff1d&&p[i+3]==1024&&p[i+1]<=sizeof(ram)-1024&&p[i+2]<=sizeof(ram)-1024);std::memmove(ram+p[i+1],ram+p[i+2],p[i+3]);++sent;}}
void copro_wait(){++waited;if(fail_wait)throw std::runtime_error("injected incomplete copy");}
#include "hmm_ring_copy.inc"
static void setup(unsigned slot,uint64_t id){
 const unsigned col=slot/16,entry=col+1;ring_background_entries[1][col]=entry;ring_background_ids[1][slot]=id;
 auto &e=textures.entries[entry];e.valid=1;e.key=UINT64_C(0xd000000000000000)|(RING_BG_COLS+col);e.bytes=RING_BG_BYTES;e.page=100+col*16;e.pins=4;map_texture_refs[entry]=2;
 for(unsigned i=0;i<1024;++i)ram[e.page*1024+(slot&15)*1024+i]=uint8_t(slot*13+i*37);
}
int main(){
 for(unsigned i=0;i<33;++i)setup(i,1000+i);
 {RingCopyQueue q;for(unsigned i=0;i<32;++i){assert(q.copy({1000+i},0,i,0x100000+i*1024));assert(!ring_background_ids[0][i]);}assert(!sent&&q.count==32);assert(q.copy({1032},0,32,0x108000));assert(sent==32&&waited==1&&q.count==1);q.flush();assert(sent==33&&waited==2);}
 for(unsigned i=0;i<33;++i){assert(ring_background_ids[0][i]==1000+i);for(unsigned j=0;j<1024;++j)assert(ram[0x100000+i*1024+j]==uint8_t(i*13+j*37));}
 assert(!busy);for(unsigned i=1;i<=3;++i)assert(map_texture_refs[i]==2&&textures.entries[i].pins==4);
 {RingCopyQueue q;assert(!q.copy({999},0,0,0x200000));auto &e=textures.entries[1];e.valid=0;assert(!q.copy({1000},0,0,0x200000));e.valid=1;auto key=e.key;e.key=17;assert(!q.copy({1000},0,0,0x200000));e.key=key;e.bytes=512;assert(!q.copy({1000},0,0,0x200000));e.bytes=RING_BG_BYTES;assert(q.count==0);}
 const auto prior=ring_background_ids[0][0];ring_background_ids[1][0]=9999;
 try{RingCopyQueue q;assert(q.copy({9999},0,0,0x200000));assert(map_texture_refs[1]==3);throw std::bad_alloc();}catch(const std::bad_alloc&){}
 assert(map_texture_refs[1]==2&&textures.entries[1].pins==4&&ring_background_ids[0][0]==prior);
 try{RingCopyQueue q;assert(q.copy({9999},0,0,0x200000));fail_wait=true;q.flush();assert(false);}catch(const std::runtime_error&){}
 fail_wait=false;assert(!busy&&map_texture_refs[1]==2&&textures.entries[1].pins==4&&ring_background_ids[0][0]==prior);
 map_texture_refs[1]=0;textures.entries[1].pins=0;{RingCopyQueue q;assert(q.copy({9999},0,0,0x200000));assert(map_texture_refs[1]==1&&textures.entries[1].pins==4);}assert(!map_texture_refs[1]&&!textures.entries[1].pins);
 std::puts("RING COPY PASS bytes=33792 batch32=1 commit_after_wait=1 reused_entry_rejected=1 missing_source_fallback=1 unwind_leases=1 wait_fault_leases=1 shared_pin_preserved=1");
}
'''
(out/'unit.cpp').write_text(code,encoding='utf-8')
inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/n for n in ('ucrt','shared','um')],ROOT/'main',ROOT/'prototype'];libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/n/'x64' for n in ('ucrt','um')]]
r=subprocess.run([str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/std:c++17','/EHsc',*['/I'+str(p) for p in inc],str(out/'unit.cpp'),'/Fe'+str(out/'unit.exe'),'/Fo'+str(out)+'\\','/link',*['/LIBPATH:'+str(p) for p in libs]],cwd=out,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
r=subprocess.run([str(out/'unit.exe')],cwd=out,capture_output=True);(out/'result.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace');print(r.stdout.decode())
