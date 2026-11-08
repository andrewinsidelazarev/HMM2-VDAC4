"""Codex: извлекаем закреплённый FAT32 через git archive; рабочая копия драйвера только читается."""
from pathlib import Path
import hashlib,json,os,subprocess,tarfile
REV='5d54fc4f0177977d56a799117201b04b9e638040'
REPO=Path('E:/zx/FAT32 Driver')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/fat32_5d54fc4')

def prepare():
    OUT.parent.mkdir(parents=True,exist_ok=True)
    stamp=OUT/'revision.txt'
    if not stamp.exists():
        archive=OUT.with_suffix('.tar')
        subprocess.run(['git','-C',str(REPO),'archive','--format=tar','--output='+str(archive),REV],check=True)
        OUT.mkdir(exist_ok=True)
        with tarfile.open(archive) as f:f.extractall(OUT,filter='data')
        stamp.write_text(REV,encoding='ascii')
    assert stamp.read_text()==REV
    env=os.environ.copy();env['SJASMPLUS']='E:/zx/z80/zuma/sjasmplus.exe'
    import sys
    with (OUT/'build.log').open('wb') as log:
        subprocess.run([sys.executable,str(OUT/'build.py')],env=env,cwd=OUT,stdout=log,stderr=subprocess.STDOUT,check=True)
    manifest=json.loads((OUT/'build/manifest.json').read_text())
    assert manifest['low_code']==0x2200
    assert manifest['filex_points']['address']==0x3600 and manifest['filex_data_cache']['address']==0x3e00
    # Codex: обе части берём из одной сборки; отдельная замена рабочей страницы запрещена.
    for name in ['fat32.bin','fat32-work.bin']:
        assert hashlib.sha256((OUT/'build'/name).read_bytes()).hexdigest()==manifest['artifacts'][name]['sha256']
    return OUT

# Клод: драйвер FAT32 из DOOM VDAC4 — тот, с которым исправление 3 и DOOM работают на плате.
# Сборка 28.10 с 5d54fc4 на плате упала на этапах 3–4 (SD, FAT32). Порт SD у обоих одинаковый
# (символы SD_* совпадают), рабочая страница старого драйвера меньше и с SPG не пересекается.
DOOM=Path('E:/zx/DOOM VDAC4/third_party/fat32')
DOOM_REV='DOOM VDAC4 third_party/fat32 (WC c98c4f5 + fixes, as in fix3)'
def prepare_doom():
    manifest=json.loads((DOOM/'build/manifest.json').read_text())
    for name in ['fat32.bin','fat32-work.bin']:
        assert hashlib.sha256((DOOM/'build'/name).read_bytes()).hexdigest()==manifest['artifacts'][name]['sha256']
    return DOOM

if __name__=='__main__':print(prepare())
