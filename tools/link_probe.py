"""Codex: связывание диагностического ELF и точный аудит совместимости с неизменённым загрузчиком."""
from pathlib import Path
import json
import re
import subprocess
import hashlib
from compile_probe import ROOT,SRC,OUT,TC

FW=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware')
def run(cmd,log):
    p=subprocess.run(cmd,capture_output=True)
    log.write_bytes(p.stdout+p.stderr)
    return p

def main():
    result=json.loads((ROOT/'Docs/evidence/compile_probe.json').read_text(encoding='utf-8'))
    if any(not r['ok'] for r in result):raise SystemExit('Сначала устраните ошибки compile_probe.py')
    c=SRC/'thirdparty/libsmacker/smacker.c';obj=OUT/'smacker.o'
    p=run([str(TC/'xtensa-esp32s3-elf-gcc.exe'),'-Os','-fPIC','-mlongcalls','-ffunction-sections','-fdata-sections','-c',str(c),'-o',str(obj)],OUT/'smacker.log')
    if p.returncode:print(p.stderr.decode(errors='replace'));return 1
    objs=[Path(r['object']) for r in result]+[obj]
    nm=subprocess.run([str(TC/'xtensa-esp32s3-elf-nm.exe'),'-u',*[str(p) for p in objs]],capture_output=True,text=True).stdout
    (ROOT/'Docs/evidence/objects_undefined.txt').write_text(nm,encoding='utf-8')
    flags=['-nostdlib','-nostartfiles','-shared','-fPIC','-Wl,-e,main','-Wl,--gc-sections','-Wl,-Bsymbolic']
    libraries=['-Wl,--start-group','-lstdc++','-lsupc++','-lc','-lm','-lgcc','-Wl,--end-group']
    for name,libs in [('no_runtime',[]),('static_runtime',libraries),('static_runtime_notext',['-Wl,-z,notext',*libraries])]:
        elf=OUT/(name+'.elf')
        args=[*flags,'-Wl,-Map,'+str(OUT/(name+'.map')),*[str(p) for p in objs],*libs,'-o',str(elf)]
        rsp=OUT/(name+'.rsp');rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args),encoding='utf-8')
        p=run([str(TC/'xtensa-esp32s3-elf-g++.exe'),'@'+str(rsp)],OUT/(name+'.log'))
        print(name,'код связывания',p.returncode,flush=True)
        evidence=ROOT/'Docs/evidence'
        (evidence/(name+'_link.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:
            (evidence/(name+'.json')).write_text(json.dumps({'link_exit':p.returncode,'valid_elf':False},indent=2),encoding='utf-8')
            print(p.stderr.decode(errors='replace')[-900:]);continue
        for option,label in [('-SW','sections'),('-Ws','symbols'),('-Wr','relocations')]:
            r=subprocess.run([str(TC/'xtensa-esp32s3-elf-readelf.exe'),option,str(elf)],capture_output=True,text=True)
            (evidence/(name+'_'+label+'.txt')).write_text(r.stdout,encoding='utf-8')
        check=FW.parent/'Sound/tools/check_module_symbols.py'
        import sys
        p=run([sys.executable,str(check),str(elf)],evidence/(name+'_loader_check.txt'))
        text=(evidence/(name+'_sections.txt')).read_text(encoding='utf-8')
        sections={}
        for line in text.splitlines():
            m=re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s*(\S*)',line)
            if m:sections[m[1]]={'bytes':int(m[3],16),'type':m[2],'flags':m[4]}
        allowed=set(re.findall(r'ZM_SYM\((\w+)\)',(FW/'src/zm_symbols.cpp').read_text(encoding='utf-8')))
        allowed.update(re.findall(r'ESP_ELFSYM_EXPORT\((\w+)\)',(FW/'components/elf_loader/src/esp_elf_symbol.c').read_text(encoding='utf-8')))
        sym=subprocess.run([str(TC/'xtensa-esp32s3-elf-readelf.exe'),'-W','--dyn-syms',str(elf)],capture_output=True,text=True).stdout
        undefined=sorted({line.split()[7] for line in sym.splitlines() if len(line.split())>=8 and line.split()[6]=='UND'})
        summary={'path':str(elf),'bytes':elf.stat().st_size,'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'loader_check_exit':p.returncode,'sections':sections,'undefined':undefined,'missing':[n for n in undefined if n not in allowed]}
        (evidence/(name+'.json')).write_text(json.dumps(summary,indent=2),encoding='utf-8')
        print('Секции', {k:v['bytes'] for k,v in sections.items() if k in ['.text','.rodata','.data','.bss','.data.rel.ro','.init_array','.eh_frame','.gcc_except_table','.tdata','.tbss']},'нет символов',len(summary['missing']),flush=True)
    return 0
if __name__=='__main__':raise SystemExit(main())
