"""Codex: ограничение доставки — настоящее хранилище модулей ядра, не только предел ELF."""
from pathlib import Path
import sys,subprocess,json,hashlib,re
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT,FW
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix1/delivery')
ELF=OUT.parents[1]/'stage1/module/HMM2.ELF'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(FW/'src/apps/module_app.cpp').read_text(encoding='utf-8')
    assert 'kReservedBytes = 320 * 1024' in source and 'cache_->canFit(size, app_ != nullptr ? appName_ : nullptr)' in source
    cc=VC/'bin/Hostx64/x64/cl.exe';link=cc.with_name('link.exe')
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/p for p in ['ucrt','shared','um']],FW/'include']
    libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/p/'x64' for p in ['ucrt','um']]]
    def run(args):
        r=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace');return r.stdout
    objects=[]
    for p in [FW/'src/zm_cache.cpp',FW/'src/elf_image.cpp',FW/'src/path_table.cpp',ROOT/'tests/stage1_delivery.cpp']:
        obj=OUT/(p.stem+'.obj');objects.append(obj)
        run([cc,'/nologo','/O2','/MT','/EHsc','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS',*['/I'+str(x) for x in inc],'/c',p,'/Fo'+str(obj)])
    exe=OUT/'delivery.exe';run([link,'/NOLOGO','/OUT:'+str(exe),*objects,*['/LIBPATH:'+str(x) for x in libs]])
    mods=[FW.parent/'Sound/modules/out'/(n+'.ELF') for n in ['PLAYER','MP3','MIDI','OGG']]
    result=json.loads(run([exe,ELF,*mods]));result['elf_sha256']=hashlib.sha256(ELF.read_bytes()).hexdigest()
    result['firmware_elf_validation']=True;result['diagnostic_path_name_valid']=True
    result['firmware_source_sha256']={str(p.relative_to(FW)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [FW/'src/zm_cache.cpp',FW/'src/apps/module_app.cpp',FW/'include/zifi/zm_cache.hpp']}
    result['decoders']={p.name:dict(bytes=p.stat().st_size,footprint=((p.stat().st_size+4095)//4096+1)*4096,remaining_with_game=result['free_after']-((p.stat().st_size+4095)//4096+1)*4096) for p in mods[1:]}
    (ROOT/'Docs/evidence/stage1_fix1_delivery.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
