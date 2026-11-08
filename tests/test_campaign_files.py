"""Codex: чтение всех файлов FS1/FS3 реальным C-слоем и отдельные границы FS1/FS2/FS3."""
from pathlib import Path
import json,os,re,struct,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from build_stage1 import VC,KIT
from compile_probe import SDK
from link_probe import FW
base=Path(os.environ['HMM2_STAGE1_BASE']).resolve().parent;out=base/'files_test';out.mkdir(exist_ok=True)
inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],root/'main',FW/'modules/sdk/include',SDK,SDK/'SDL2']
libs=[VC/'lib/x64',KIT/'Lib/10.0.26100.0/ucrt/x64',KIT/'Lib/10.0.26100.0/um/x64']
os.environ['PATH']=str(VC/'bin/Hostx64/x64')+os.pathsep+os.environ['PATH']
exe=out/'files_test.exe'
cmd=['E:/zx/llvm/bin/clang-cl.exe','/nologo','/O2','/MT','/utf-8','/W0','/std:c11','/DHMM2_HOST','/D_CRT_SECURE_NO_WARNINGS',*['/I'+str(p) for p in inc],str(root/'tests/campaign_files_test.c'),'/Fe'+str(exe),'/Fo'+str(out)+'\\','/link',*['/LIBPATH:'+str(p) for p in libs]]
result=subprocess.run(cmd,cwd=out,capture_output=True);(out/'build.log').write_bytes(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr
checks=[]
def run(path,valid=True):
    result=subprocess.run([str(exe),str(path)],cwd=out,capture_output=True,text=True)
    assert result.returncode==(0 if valid else 2),(path,result.returncode,result.stdout,result.stderr)
    if path.name=='HMM2_campaign.DAT':
        # Codex: регрессия сохранения GateHell — каталог кампаний должен занимать меньше старых 453 строк по 72 байта.
        assert int(re.search(r'XTENSA_CATALOG=(\d+)',result.stdout)[1])<453*72
    checks.append(dict(file=str(path),valid=valid,result=result.stdout.strip()))
for path in [base/'stage/data/HMM2.DAT',base/'stage/data/HMM2_campaign.DAT',base/'stage/data/HMM2_maps_only.DAT',base/'rebuild/data/HMM2.DAT']:run(path)
for version in (1,2,3):
    data=bytearray(8192);name=b'maps/'+b'x'*(54 if version==2 else 58)
    pointer=7168 if version==3 else 16
    struct.pack_into('<4I',data,0,0x31465348,version,1,pointer if version==3 else 0)
    data[pointer:pointer+len(name)]=name;struct.pack_into('<II',data,pointer+64,2048,1024)
    data[2048:3072]=bytes(range(256))*4
    if version==2:struct.pack_into('<I',data,pointer+60,4096);data[4096:4608]=b'P'*512
    path=out/f'fs{version}.dat';path.write_bytes(data);run(path)
    broken=bytearray(data);broken[pointer:pointer+(60 if version==2 else 64)]=b'X'*(60 if version==2 else 64)
    path=out/f'fs{version}_bad_name.dat';path.write_bytes(broken);run(path,False)
    broken=bytearray(data);struct.pack_into('<I',broken,pointer+68,0xffffffff)
    path=out/f'fs{version}_bad_size.dat';path.write_bytes(broken);run(path,False)
    if version==2:
        broken=bytearray(data);struct.pack_into('<I',broken,pointer+60,8192)
        path=out/'bad_prefix.dat';path.write_bytes(broken);run(path,False)
    if version==3:
        broken=bytearray(data);struct.pack_into('<I',broken,12,8190)
        path=out/'bad_table.dat';path.write_bytes(broken);run(path,False)
(out/'result.json').write_text(json.dumps(checks,indent=2),encoding='utf-8');print(json.dumps(checks,indent=2))
