"""Codex: цветной scanout VDAC4 сравнивается с независимым исполнителем FT, включая фазу палитры."""
from pathlib import Path
import json,struct,sys,hashlib
sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python')
import numpy as np
from PIL import Image
from check_fix5_ft import render
from ft_command_fifo import CommandFifo

def main():
 source=Path(sys.argv[1]);out=Path(sys.argv[2]);raw=(source/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)];at=0;rows=[];rgb=shaded=None
 while at<len(raw):
  address,count=struct.unpack_from('<II',raw,at);at+=8;n=count*4;data=raw[at:at+n];at+=n;assert len(data)==n
  if address==0xffffffff:
   serial=struct.unpack('<I',data)[0];image=out/f'frame_{serial}.ppm'
   if image.exists():
    full=Image.open(image).convert('RGB');full.save(out/f'frame_{serial}.png');model=np.asarray(full)[:rgb.shape[0],:rgb.shape[1]];assert model.shape==rgb.shape
    d=np.any(model!=rgb,axis=2);delta=np.abs(model.astype(int)-rgb.astype(int));row=dict(frame=serial,pixels=int(d.size),shadow_pixels=int(shaded.sum()),different_pixels=int(d.sum()),different_non_shadow_pixels=int((d&~shaded).sum()),maximum_channel_difference=int(delta.max()),colored_pixels=int(np.any(model!=model[:,:,:1],axis=2).sum()),rgb_sha256=hashlib.sha256(model.tobytes()).hexdigest());rows.append(row);Image.fromarray(model).save(out/f'game_{serial}.png')
   continue
  address&=0x7fffffff
  if address<0x300000:ram[address:address+n]=data
  elif address<0x302000:dl[address-0x300000:address-0x300000+n]=data
  elif address==0x302054:
   words=[]
   for v, in struct.iter_unpack('<I',dl):
    words.append(v)
    if not v:break
   rgb,shaded=render(words,ram,handle_state=handles)
  else:fifo.write(address,data,ram,dl)
 result=dict(source=str(source),frames=len(rows),rows=rows,graphics_replay_only=True)
 (out/'color_comparison.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
 assert rows and all(not r['different_non_shadow_pixels'] and r['maximum_channel_difference']<=1 for r in rows),'Различия вне альфа-тени или более одного уровня'
if __name__=='__main__':main()
