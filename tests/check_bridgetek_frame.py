"""Codex: реальный DL игры в официальном FT812. Только адреса активного кадра уплотняются до1МиБ."""
from pathlib import Path
import sys,struct,json,hashlib,argparse
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np
from PIL import Image
from ft_command_fifo import CommandFifo
from check_fix5_ft import render
from bridgetek_driver import Bridgetek

def main():
 ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('out',type=Path);ap.add_argument('--frame',type=int,default=200);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 raw=(a.source/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=0;found=False
 while at<len(raw):
  addr,n=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+n*4];at+=n*4
  if addr==0xffffffff:
   if int.from_bytes(data,'little')==a.frame:found=True;break
   continue
  addr&=0x7fffffff
  if addr<0x300000:ram[addr:addr+len(data)]=data
  elif addr<0x302000:dl[addr-0x300000:addr-0x300000+len(data)]=data
  else:fifo.write(addr,data,ram,dl)
 assert found
 words=[]
 for v, in struct.iter_unpack('<I',dl):
  words.append(v)
  if not v:break
 expected,shaded=render(words,ram,paletted8_single_channel=True)
 # Codex: FT812 имеет1МиБ; игра по разрешённому исключению использует3МиБ.
 # Перенумеровываются только адреса активных texel/palette и CELL; команды цвета неизменны.
 packed=bytearray(1024*1024);palettes={};textures={};palette_at=0;texture_at=65536;h=0;primitive=1;handles=[dict(source=0,stride=0,height=0) for _ in range(32)];actual=[None]*32;compact=[]
 for v in words:
  op=v>>24;d=handles[h]
  if op==5:h=v&31;compact.append(v);continue
  if op==1:d['source']=v&0x3fffff;continue
  if op==7:d['stride']=(d['stride']&3072)|(v>>9&1023);d['height']=(d['height']&1536)|(v&511)
  if op==0x28:d['stride']=(d['stride']&1023)|((v>>2&3)<<10);d['height']=(d['height']&511)|((v&3)<<9)
  if op==0x1f:primitive=v&15
  if op==0x2a:
   addr=v&0x3fffff
   if addr not in palettes:
    assert palette_at+1024<=65536;packed[palette_at:palette_at+1024]=ram[addr:addr+1024];palettes[addr]=palette_at;palette_at+=1024
   compact.append(0x2a000000|palettes[addr]);continue
  if v>>30==2 and primitive==1:
   vertex_handle=v>>7&31;d=handles[vertex_handle];size=d['stride']*d['height'];addr=d['source']+size*(v&127);key=(addr,size)
   if key not in textures:
    alignment=1024 if d['stride']==d['height']==32 else 4;texture_at=(texture_at+alignment-1)&~(alignment-1);assert texture_at+size<=len(packed);packed[texture_at:texture_at+size]=ram[addr:addr+size];textures[key]=texture_at;texture_at+=size
   addr=textures[key]
   if d['stride']==d['height']==32:bank=addr&~131071;cell=(addr//1024)&127
   else:bank=addr;cell=0
   if actual[vertex_handle]!=bank:
    if h!=vertex_handle:compact.append(0x05000000|vertex_handle)
    compact.append(0x01000000|bank);actual[vertex_handle]=bank
    if h!=vertex_handle:compact.append(0x05000000|h)
   compact.append((v&~127)|cell);continue
  compact.append(v)
 assert len(compact)<=2048
 relocated,relocated_shaded=render(compact,packed,paletted8_single_channel=True);assert np.array_equal(expected,relocated)
 payload=struct.pack('<'+'I'*len(compact),*compact);(a.out/'frame.dl').write_bytes(payload);(a.out/'active_ram.bin').write_bytes(packed[:texture_at]);Image.fromarray(expected).save(a.out/'independent.png')
 driver=Bridgetek(a.out)
 try:raw=driver.capture(packed[:texture_at],payload)
 finally:driver.close()
 official=np.asarray(Image.frombytes('RGBA',(640,480),raw,'raw','BGRA').convert('RGB'));Image.fromarray(official).save(a.out/'official.png');different=np.any(official!=expected,axis=2);delta=np.abs(official.astype(int)-expected.astype(int));result=dict(source=str(a.source),frame=a.frame,version=driver.version,dll_sha256=driver.digest,dl_words=len(compact),active_ram_bytes=texture_at,original_ram_g_bytes=3*1024*1024,addresses_relocated_only=True,pixels=640*480,different_pixels=int(different.sum()),different_non_shadow_pixels=int((different&~shaded).sum()),maximum_channel_difference=int(delta.max()),logs=driver.logs)
 (a.out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='logs'},indent=2));assert not result['different_non_shadow_pixels'] and result['maximum_channel_difference']<=1
if __name__=='__main__':main()
