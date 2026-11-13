from pathlib import Path
import os,sys,json,subprocess
s=Path(__file__).resolve().parents[1];base=Path(os.environ.get('HMM2_STAGE1_BASE',str(s/'stage')));sys.path.insert(0,str(s/'tools'))
from build_stage1 import VC,KIT,SDK,FW
reference=os.environ.get('HMM2_GRAPHICS_IO_REFERENCE_SOURCE');baseline=bool(reference)
out=base/('graphics_io_before' if baseline else 'graphics_io');out.mkdir(parents=True,exist_ok=True)
prefix=(s/'tests/stage1_input.c').read_text(encoding='utf-8').split('int main(void){')[0]
prefix=prefix.replace('void hmm_gpu_busy_enter(void){}void hmm_gpu_busy_leave(void){}','static unsigned gpu_busy;void hmm_gpu_busy_enter(void){++gpu_busy;}void hmm_gpu_busy_leave(void){assert(gpu_busy);--gpu_busy;}')
body=r'''
#ifndef GRAPHICS_REQUEST_LIMIT
#define GRAPHICS_REQUEST_LIMIT 32768
#endif
static unsigned calls,acks,rejects,bytes_done,expected_source,expected_target;static uint8_t memory[0x300000];
static uint8_t pattern(unsigned at){return (uint8_t)(at*37u+(at>>9)*13u);}
static int gfx_reply(uint8_t cmd,const void *p,uint16_t n){assert(cmd==0xf7&&n==1);if(*(const uint8_t*)p)++acks;else ++rejects;return ZM_OK;}
static int gfx_status(uint32_t at,uint32_t *p,uint32_t n){assert(at==0x5c&&n==1);*p=0;return ZM_OK;}
static unsigned u32(const uint8_t *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int gfx_event(uint8_t cmd,const void *data,uint16_t n,uint32_t timeout){
 const uint8_t *p=data;assert(cmd==0xe6&&n==12&&timeout==1000&&gpu_busy==1);
 const unsigned seq=p[0]|p[1]<<8,source=u32(p+2),target=u32(p+6),size=p[10]|p[11]<<8;
 assert(source==expected_source+bytes_done&&target==expected_target+bytes_done&&size&&size<=GRAPHICS_REQUEST_LIMIT&&!(size&511));
 for(unsigned at=0;at<size;at+=8192){const unsigned count=size-at<8192?size-at:8192;for(unsigned i=0;i<count;++i)memory[target+at+i]=pattern(source+at+i);}
 uint8_t answer[3]={(uint8_t)(seq+1),(uint8_t)((seq+1)>>8),0};
 const unsigned accepted=acks;hmm_command(0x77,answer,3);assert(acks==accepted);
 answer[0]=(uint8_t)seq;answer[1]=(uint8_t)(seq>>8);hmm_command(0x77,answer,3);assert(acks==accepted+1);
 bytes_done+=size;++calls;return ZM_OK;
}
int main(void){
 zm_core_t core={0};core.millis=millis;core.mutex_create=core.signal_create=create;core.mutex_lock=core.mutex_unlock=lock;core.signal_give=lock;core.signal_wait=signal_wait;core.reply=gfx_reply;core.event=gfx_event;core.link_read=gfx_status;hmm_core=&core;assert(hmm_io_init());hmm_package_size=547492852;
 const unsigned sizes[]={512,1536,8192,8704,16384,16896,32768,65536,131584,1024};unsigned total=0,expected_calls=0;
 for(unsigned c=0;c<sizeof(sizes)/sizeof(sizes[0]);++c){
  const unsigned size=sizes[c],target=c==9?0x300000-size:0x20000;expected_source=c*1024;expected_target=target;bytes_done=0;memset(memory,0x5a,sizeof(memory));
  hmm_remote_graphics(expected_source,target,size);assert(bytes_done==size&&!gpu_busy);
  for(unsigned i=0;i<size;++i)assert(memory[target+i]==pattern(expected_source+i));
  if(target)assert(memory[target-1]==0x5a);if(target+size<sizeof(memory))assert(memory[target+size]==0x5a);
  total+=size;expected_calls+=(size+GRAPHICS_REQUEST_LIMIT-1)/GRAPHICS_REQUEST_LIMIT;
 }
 assert(calls==expected_calls&&acks==calls&&rejects==calls);printf("GRAPHICS IO PASS cases=10 bytes=%u E6=%u internal8KiBsteps=1 sequence_guards=1 busy_balanced=1 bounds_canaries=1\n",total,calls);return 0;
}
'''
source=out/'graphics_io.c';source.write_text(prefix+body,encoding='utf-8')
inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/n for n in ('ucrt','shared','um')],s/'main',SDK,SDK/'SDL2',FW/'modules/sdk/include'];libs=[VC/'lib/x86',*[KIT/'Lib/10.0.26100.0'/n/'x86' for n in ('ucrt','um')]]
objects=[]
for p in ((Path(reference) if baseline else s/'main/hmm_io.c'),s/'main/hmm_input.c',source):
 obj=out/(p.stem+'.obj');objects.append(obj)
 cmd=[str(VC/'bin/Hostx64/x86/cl.exe'),'/nologo','/O2','/TC','/std:c11','/MT','/utf-8','/DSDL_MAIN_HANDLED','/DGRAPHICS_REQUEST_LIMIT='+str(8192 if baseline else 32768),'/D_CRT_SECURE_NO_WARNINGS','/FI'+str(s/'tests/stage1_atomic.h'),*['/I'+str(p) for p in inc],'/c',str(p),'/Fo'+str(obj)]
 r=subprocess.run(cmd,cwd=out,capture_output=True);(out/(p.stem+'.log')).write_bytes(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
r=subprocess.run([str(VC/'bin/Hostx64/x86/link.exe'),'/NOLOGO','/SUBSYSTEM:CONSOLE','/OUT:'+str(out/'test.exe'),*map(str,objects),*['/LIBPATH:'+str(p) for p in libs]],cwd=out,capture_output=True);(out/'link.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace')
r=subprocess.run([str(out/'test.exe')],cwd=out,capture_output=True);(out/'result.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace');print(r.stdout.decode())
