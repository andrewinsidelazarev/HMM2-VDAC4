"""Codex: адрес LBA из кэша ОЗУ Ево для фрагментированного файла, без чтения FAT/SD."""
from pathlib import Path
import json,os,struct
from test_stage1_joint import BASE,base
OUT=Path(os.environ.get('HMM2_LBA_OUT','E:/zx/cache/tmp/hmm2_esp/integration_audio/lba59_final_z80'))
def main():
    sym=base.symbols(BASE/'launcher/build/hmm2.sym');file_bytes=Path(os.environ.get('HMM2_LBA_DAT','E:/zx/cache/tmp/hmm2_esp/integration_audio/raw_final_data/HMM2.DAT')).stat().st_size;sectors=(file_bytes+511)//512;proof=[]
    def lba(cluster):return 0x0100fff8+((cluster*104729)%100003)*256
    for spc in [4,8,16,32,64,128]:
        count=(sectors+spc-1)//spc;assert count<=262144
        pages={}
        for cluster in range(count):
            bank=64+cluster//4096
            if bank not in pages:pages[bank]=bytearray(16384)
            struct.pack_into('<I',pages[bank],(cluster%4096)*4,lba(cluster))
        m=base.z80.Z80Machine();m.set_memory_block(0x8000,(BASE/'launcher/build/hmm2_z80.bin').read_bytes())
        m.set_memory_block(sym['ScanSectorsPerCluster'],bytes([spc]));m.set_memory_block(sym['WC_PAGE3'],bytes([56]));ports=[]
        def write(port,value):
            assert port==0x13af and value in pages,(port,value)
            ports.append((port,value));m.set_memory_block(0xc000,pages[value])
        m.set_output_callback(write);m.set_breakpoint(0x7fff)
        forbidden={sym[n] for n in ['ScanNextCluster','ScanClusterLba']}
        for pc in forbidden:m.set_breakpoint(pc)
        selected=sorted({0,1,spc-1,spc,sectors-1}|{n*4096*spc+d for n in range((count+4095)//4096) for d in [-1,0,1] if 0<=n*4096*spc+d<sectors}|{(i*65537+12345)%sectors for i in range(128)})
        for sector in selected:
            m.set_memory_block(sym['FastSector'],struct.pack('<I',sector));m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0x7fff));m.pc=sym['FastResolve']
            for _ in range(4):
                m.ticks_to_stop=100000;m.run();assert m.pc not in forbidden,'FAT re-walk'
                if m.pc==0x7fff:break
            assert m.pc==0x7fff
            actual=int.from_bytes(bytes(m.memory[sym['ScanLba']:sym['ScanLba']+4]),'little')
            assert actual==lba(sector//spc)+sector%spc,(spc,sector,actual)
        proof.append(dict(sectors_per_cluster=spc,cluster_addresses=count,index_banks=len(pages),index_bytes=len(pages)*16384,lookups=len(selected),fat_rewalks=0,sd_reads_for_lookup=0))
    OUT.mkdir(exist_ok=True);result=dict(file_bytes=file_bytes,fragmented_lba_table=True,checks=proof)
    (OUT/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
