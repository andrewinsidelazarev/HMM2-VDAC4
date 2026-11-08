"""Codex: исполняем исходный генератор объектов и проверяем обратное восстановление таблиц."""
from pathlib import Path
import subprocess,json,hashlib,sys
from build_stage1 import VC,KIT
from compile_probe import ROOT,SRC,SDK
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix1/object_table')

def transform(source):
    start=source.index('    void populateRoads(');end=source.index('    void populateObjectData()',start)
    tables=(ROOT/'main/hmm_objects.inc').read_text(encoding='utf-8')+'\n'+(ROOT/'main/hmm_objects_loader.inc').read_text(encoding='utf-8')
    source=source[:start]+'#ifdef HMM2_PACKED\n'+tables+'\n#else\n'+source[start:end]+'#endif\n'+source[end:]
    start=source.index('        populateRoads( objectData[');end=source.index('        for ( const auto & objects : objectData )',start)
    return source[:start]+'#ifdef HMM2_PACKED\n        hmmPopulateObjects();\n#else\n'+source[start:end]+'#endif\n'+source[end:]

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=SRC/'fheroes2/maps/map_object_info.cpp';original=source.read_text(encoding='utf-8')
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/p for p in ['ucrt','shared','um']],SRC/'engine',SDK,*[p for p in (SRC/'fheroes2').iterdir() if p.is_dir()]]
    libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/p/'x64' for p in ['ucrt','um']]]
    cc=VC/'bin/Hostx64/x64/cl.exe';link=cc.with_name('link.exe')
    def run(args):
        r=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace');return r.stdout.decode(errors='replace')
    def dump(label,text,packed=False):
        cpp=OUT/(label+'.cpp');cpp.write_text(text+'\n'+(ROOT/'tests/stage1_object_dump.cpp').read_text(encoding='utf-8'),encoding='utf-8')
        obj=OUT/(label+'.obj');exe=OUT/(label+'.exe')
        run([cc,'/nologo','/O2','/MT','/EHsc','/DNDEBUG','/Gy','/Gw','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS',*(['/DHMM2_PACKED'] if packed else []),*['/I'+str(x) for x in inc],'/c',cpp,'/Fo'+str(obj)])
        run([link,'/NOLOGO','/OPT:REF','/OUT:'+str(exe),obj,*['/LIBPATH:'+str(x) for x in libs]])
        counts=run([exe,OUT/(label+'.bin')]);return (OUT/(label+'.bin')).read_bytes(),counts
    raw,counts=dump('original',original)
    text='/* Codex: таблицы OpenHMM2 1.1.17, автоматически из tools/build_stage1_object_table.py.\n * SPDX-License-Identifier: GPL-2.0-or-later */\nstatic const unsigned char hmm_object_table[]={\n'
    text+='\n'.join(','.join(str(x) for x in raw[i:i+32])+',' for i in range(0,len(raw),32))+'\n};\n'
    (ROOT/'main/hmm_objects.inc').write_text(text,encoding='utf-8')
    decoded,again=dump('restored',transform(original),True);assert decoded==raw and counts==again
    report=dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),counts=counts.strip(),all_fields_and_order_exact=True)
    (ROOT/'Docs/evidence/stage1_fix1_objects.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
if __name__=='__main__':main()
