"""Codex: проверяем PIC runtime отдельно от платформенного слоя и фиксируем остаточные импорты."""
from pathlib import Path
import json,subprocess
from compile_probe import TC,ROOT,OUT as OLD
from build_pic_runtime import OUT,BASE
from link_probe import FW

def main():
    evidence={}
    for mode in ['min','engine']:
        objects=[BASE/'runtime_probe/min.o'] if mode=='min' else sorted((BASE/'lto').glob('*.o'))+[OLD/'smacker.o']
        entry='app_entry' if mode=='min' else 'main'
        cmd=['-nostdlib','-nostartfiles','-shared','-fPIC','-Os','-flto','-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,--exclude-libs,ALL','-Wl,-e,'+entry,
             *map(str,objects),'-Wl,--start-group',str(OUT/'libstdcpp_pic.a'),str(OUT/'libunwind_pic.a'),'-Wl,--end-group','-o',str(OUT/(mode+'_pic.elf')),'-Wl,-Map,'+str(OUT/(mode+'_pic.map'))]
        rsp=OUT/(mode+'_pic.rsp');rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in cmd),encoding='utf-8')
        r=subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),'@'+str(rsp)],capture_output=True)
        (OUT/(mode+'_pic.log')).write_bytes(r.stdout+r.stderr);print(mode,r.returncode,r.stderr.decode(errors='replace')[-2500:],flush=True)
        if not r.returncode:
            for option,label in [('-u','undefined')]:
                out=subprocess.check_output([str(TC/'xtensa-esp32s3-elf-nm.exe'),option,str(OUT/(mode+'_pic.elf'))],text=True)
                (OUT/(mode+'_'+label+'.txt')).write_text(out,encoding='utf-8');print(out if mode=='min' else 'Импортов: '+str(len(out.splitlines())),flush=True)
            sec=subprocess.check_output([str(TC/'xtensa-esp32s3-elf-size.exe'),'-A',str(OUT/(mode+'_pic.elf'))],text=True)
            (OUT/(mode+'_pic_size.txt')).write_text(sec,encoding='utf-8');print(sec,flush=True)
if __name__=='__main__':main()
