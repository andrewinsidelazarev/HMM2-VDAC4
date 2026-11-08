"""Codex: компилируем тот же модуль для ПК и сверяем обе контрольные суммы независимо."""
from pathlib import Path
import subprocess,sys,re,json
from build_profile import VC,KIT,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from build_sd_bench import OUT
from link_probe import FW
def main():
    flags=['/nologo','/O2','/MT','/utf-8',*['/I'+str(p) for p in [VC/'include',KIT/'Include/10.0.26100.0/ucrt',KIT/'Include/10.0.26100.0/shared',KIT/'Include/10.0.26100.0/um',FW/'modules/sdk/include']]]
    objects=[]
    for p in [ROOT/'bench/sd_bench.c',ROOT/'tests/sd_bench_host.c']:
        obj=OUT/(p.stem+'.obj');args=[str(VC/'bin/Hostx64/x86/cl.exe'),*flags,*(['/Dmain=sd_module_entry'] if p.stem=='sd_bench' else []),'/c',str(p),'/Fo'+str(obj)]
        r=subprocess.run(args,capture_output=True);print((r.stdout+r.stderr).decode(errors='replace'));assert not r.returncode;objects.append(obj)
    args=[str(VC/'bin/Hostx64/x86/link.exe'),'/NOLOGO','/OUT:'+str(OUT/'sd_host.exe'),*['/LIBPATH:'+str(p) for p in [VC/'lib/x86',KIT/'Lib/10.0.26100.0/ucrt/x86',KIT/'Lib/10.0.26100.0/um/x86']],*map(str,objects)]
    subprocess.run(args,check=True,cwd=OUT)
    asset=Path('E:/zx/HMM2/Assets/Original/DATA/HEROES2.AGG')
    r=subprocess.run([str(OUT/'sd_host.exe'),str(asset)],capture_output=True);log=r.stdout.decode('utf-8',errors='replace');print(log);assert not r.returncode,r.returncode
    data=asset.read_bytes()
    def fnv(blob,value=2166136261):
        for b in blob:value=((value^b)*16777619)&0xffffffff
        return value
    hashes=[fnv(data)];value=2166136261;seed=0x484d4d32
    for i in range(2048):
        seed^=seed<<13&0xffffffff;seed^=seed>>17;seed^=seed<<5&0xffffffff
        at=seed%(len(data)-1016+1)
        if i<3:at=33554431+i
        if i==3:at=len(data)-1016
        value=fnv(data[at:at+1016],value)
    hashes.append(value)
    assert [int(s,16) for s in re.findall('FNV1a=([0-9a-f]+)',log)]==hashes
    (ROOT/'Docs/evidence/sd_bench_host.log').write_text('Codex: ВИРТУАЛЬНЫЕ часы; показатели скорости этого журнала не относятся к плате.\n'+log,encoding='utf-8')
    (ROOT/'Docs/evidence/sd_bench_hashes.json').write_text(json.dumps(dict(sequential=hashes[0],random=hashes[1],bytes=len(data),requests=(len(data)+1015)//1016+2048),indent=2),encoding='utf-8')
if __name__=='__main__':main()
