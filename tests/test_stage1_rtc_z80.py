"""Codex: исполнение машинного кода чтения RTC: UIP, смена секунды, отсутствие часов, сохранность FIFO/NVRAM."""
from pathlib import Path
import json,struct
from test_stage1_joint import BASE,base
OUT=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/clock_checks')
def main():
 symbols=base.symbols(BASE/'launcher/build/hmm2.sym');proof=[]
 for label,uip,roll in [('normal',0,False),('uip_then_second_change',8,True),('rtc_missing',10000,False)]:
  m=base.z80.Z80Machine();m.set_memory_block(0x8000,(BASE/'launcher/build/hmm2_z80.bin').read_bytes());mem=m.memory
  state=dict(reg=0,uip=uip,seconds=0);writes=[]
  def out(port,value):
   writes.append((port,value))
   if port==0xdff7:state['reg']=value
  def inp(port):
   assert port==0xbff7,hex(port);r=state['reg']
   if r==10:
    if state['uip']:state['uip']-=1;return 128
    return 32
   if r==0:
    state['seconds']+=1;return 0x59 if roll and state['seconds']==1 else 0
   return {11:2,2:0,4:0x14,7:0x29,8:0x10,9:0x26,13:128}.get(r,0)
  m.set_input_callback(inp);m.set_output_callback(out);m.pc=symbols['RtcRead'];m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0xc000));m.set_breakpoint(0xc000)
  for _ in range(30):
   m.ticks_to_stop=100000;m.run()
   if m.pc==0xc000:break
  assert m.pc==0xc000,'RTC wait must be bounded'
  packet=bytes(mem[symbols['RtcBuffer']:symbols['RtcBuffer']+8])
  if label=='rtc_missing':assert packet[7]==0
  else:assert packet==bytes([2,0,0,0x14,0x29,0x10,0x26,128]),packet
  assert not any(port==0xbff7 for port,_ in writes) and state['reg']==0xf0
  proof.append(dict(case=label,packet=packet.hex(),bounded=True,rtc_and_nvram_not_written=True,ps2_fifo_selection_restored=True))
 OUT.mkdir(exist_ok=True);(OUT/'z80_result.json').write_text(json.dumps(proof,indent=2));print(proof)
if __name__=='__main__':main()
