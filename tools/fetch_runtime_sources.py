"""Codex: загружаем официальный исходный GCC 15.2.0 и извлекаем только runtime в разрешённый временный каталог."""
from pathlib import Path
import hashlib
import json
import tarfile
import urllib.request

OUT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/runtime_sources')
URL='https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz'
def main():
    OUT.mkdir(parents=True,exist_ok=True);archive=OUT/'gcc-15.2.0.tar.xz'
    if not archive.exists():
        partial=archive.with_suffix('.partial')
        with urllib.request.urlopen(URL,timeout=60) as response,partial.open('wb') as output:
            while chunk:=response.read(1024*1024):output.write(chunk)
        partial.replace(archive)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    print('Архив GCC:',archive.stat().st_size,digest,flush=True)
    wanted=('gcc-15.2.0/libstdc++-v3/','gcc-15.2.0/libgcc/','gcc-15.2.0/include/','gcc-15.2.0/gcc/config/xtensa/')
    count=0
    with tarfile.open(archive,'r:xz') as tar:
        for item in tar:
            if item.isfile() and item.name.startswith(wanted):
                dest=OUT/item.name
                assert dest.resolve().is_relative_to(OUT.resolve())
                if not dest.exists():
                    dest.parent.mkdir(parents=True,exist_ok=True)
                    with tar.extractfile(item) as source,dest.open('wb') as output:output.write(source.read())
                count+=1
    (OUT/'source.json').write_text(json.dumps(dict(url=URL,sha256=digest,files=count),indent=2),encoding='utf-8')
    print('Файлов runtime:',count,flush=True)
if __name__=='__main__':main()
