"""Codex: --check по умолчанию; --apply только после разрешения менять исходники прошивки."""
from pathlib import Path
import argparse,hashlib,json,shutil,datetime
R=Path(__file__).resolve().parents[1];P=R/'patches/slot0_autounload'
F=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    a=argparse.ArgumentParser();a.add_argument('--apply',action='store_true');args=a.parse_args()
    rows=json.loads((P/'sources.json').read_text(encoding='utf-8'))
    for name,row in rows.items():
        assert sha(F/name)==row['before'],'Исходник изменился после проверки: '+name
        assert sha(P/'after'/name)==row['after'],'Кандидат изменился: '+name
    new='include/zifi/ft_detach_guard.hpp';assert not (F/new).exists(),'Новый заголовок уже существует'
    if not args.apply:print('CHECK OK: исходные файлы совпадают; прошивка не изменена');return
    backup=Path('E:/zx/cache/tmp/hmm2_esp/slot0_autounload')/('backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
    for name in rows:
        dst=backup/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(F/name,dst)
    for name in [*rows,new]:
        dst=F/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(P/'after'/name,dst)
    (backup/'applied.json').write_text(json.dumps({name:sha(F/name) for name in [*rows,new]},indent=2),encoding='utf-8')
    print('Применено только к исходникам. Резервная копия:',backup)
if __name__=='__main__':main()
