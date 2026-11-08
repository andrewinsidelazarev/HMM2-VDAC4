"""Codex: PIC zlib и недостающие процедуры GCC; всё собирается внутри разрешённого tmp."""
from pathlib import Path
import hashlib,tarfile,urllib.request,subprocess,json
from compile_probe import ROOT,TC
from prepare_stage1 import BASE
from build_pic_runtime import SRC as GCCCPP,OUT as OLD
OUT=BASE/'runtime'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=OUT/'zlib-1.3.2.tar.gz';url='https://zlib.net/fossils/zlib-1.3.2.tar.gz'
    if not archive.exists():
        with urllib.request.urlopen(url,timeout=60) as r:archive.write_bytes(r.read())
    with tarfile.open(archive) as t:t.extractall(OUT,filter='data')
    source=OUT/'zlib-1.3.2';objects=[]
    flags=['-std=gnu11','-Os','-fPIC','-mlongcalls','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-fexceptions']
    def compile(p,inc=[],extra=[]):
        obj=OUT/(p.stem+'.o');cmd=[TC/'xtensa-esp32s3-elf-gcc.exe',*flags,*extra,*['-I'+str(x) for x in inc],'-c',p,'-o',obj]
        r=subprocess.run(list(map(str,cmd)),capture_output=True);obj.with_suffix('.log').write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
        objects.append(obj)
    for n in ['adler32','crc32','compress','deflate','trees','uncompr','inflate','inftrees','inffast','zutil']:compile(source/(n+'.c'),[source])
    gcc=GCCCPP.parent
    (OUT/'auto-target.h').write_text('',encoding='ascii')
    compile(gcc/'libgcc/unwind-c.c',[OUT,OLD/'config',gcc/'gcc',gcc/'include',gcc/'libgcc',gcc/'libgcc/config/xtensa'],['-DIN_LIBGCC2'])
    compile(gcc/'libgcc/config/xtensa/lib2funcs.S')
    asm=gcc/'libgcc/config/xtensa/lib1funcs.S'
    for name in ['bswapsi2','divdf3','cmpdf2','fixunsdfsi','fixunsdfdi','floatunsidf']:
        obj=OUT/('gcc_'+name+'.o')
        cmd=[TC/'xtensa-esp32s3-elf-gcc.exe','-Os','-fPIC','-mlongcalls','-DL_'+name,'-c',asm,'-o',obj]
        r=subprocess.run(list(map(str,cmd)),capture_output=True);obj.with_suffix('.log').write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
        objects.append(obj)
    rsp=OUT/'archive.rsp';rsp.write_text('\n'.join('"'+str(x).replace('\\','/')+'"' for x in ['rcs',OUT/'libhmm_pic.a',*objects]),encoding='utf-8')
    subprocess.run([str(TC/'xtensa-esp32s3-elf-ar.exe'),'@'+str(rsp)],check=True)
    (OUT/'sources.json').write_text(json.dumps({'zlib_url':url,'zlib_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
    print('PIC runtime:',len(objects),'файлов')
if __name__=='__main__':main()
