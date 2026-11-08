"""Codex: один потоковый файл для неизменённого E4/HSD; внутри — пути игры и покадровая графика."""
from pathlib import Path
import os,subprocess,struct,json,hashlib,shutil,argparse
from compile_probe import ROOT
from prepare_stage1 import BASE
from pack_stage1_ft import build as build_ft
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--skip-graphics',action='store_true');ap.add_argument('--reuse-ftg',action='store_true');ap.add_argument('--clips-dir',type=Path);args=ap.parse_args()
    out=BASE/'data';out.mkdir(exist_ok=True,parents=True);gfx=out/'HMM2.GFX'
    if not args.skip_graphics:
        env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_PACK_OUT=str(gfx))
        with (out/'graphics.log').open('wb') as log:
            p=subprocess.run([str(BASE/'host32/hmm2_host.exe'),str(out/'converter')],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
        if p.returncode:raise RuntimeError((out/'graphics.log').read_text(encoding='utf-8',errors='replace')[-3500:])
    ftg=out/'HMM2.FTG' if args.reuse_ftg else build_ft()
    original=Path('E:/zx/HMM2/Assets/Original')
    files=[('data/'+p.name.lower(),p) for p in sorted((original/'DATA').glob('*.AGG'))]
    # Codex: CampaignData ищет оригинальные H2C/HXC через тот же каталог maps.
    files += [('maps/'+p.name.lower(),p) for p in sorted((original/'MAPS').iterdir()) if p.suffix.lower() in ['.mp2','.mx2','.h2c','.hxc']]
    files += [('files/data/resurrection.h2d',ROOT/'third_party/openhmm2/files/data/resurrection.h2d'),('hmm2.gfx',gfx),('hmm2.ftg',ftg)]
    # Codex: музыка — исходные OGG без перекодирования; SFX указывают в байты AGG.
    files += [('music/'+p.name.lower(),p) for p in sorted((original/'MUSIC').glob('*.ogg'))]
    # Codex: только преобразованные штатным build_stage1_clips ролики; без них действует исходная проверка отсутствия файла.
    if args.clips_dir:
        files += [('video/'+p.name.lower(),p) for p in sorted(args.clips_dir.iterdir()) if p.suffix.lower() in ('.smk','.ogg')]
    sounds={}
    for agg in sorted((original/'DATA').glob('*.AGG')):
        raw=agg.read_bytes();count=struct.unpack_from('<H',raw)[0]
        for i in range(count):
            name=raw[len(raw)-count*15+i*15:len(raw)-count*15+(i+1)*15].split(b'\0')[0].decode('ascii')
            if name.upper().endswith('.82M'):
                _,offset,size=struct.unpack_from('<3I',raw,2+i*12)
                assert offset+size<=len(raw)
                sounds[name.lower()]=('data/'+agg.name.lower(),offset,size)
    aliases=sorted(sounds.items())
    # Codex: каталог фиксированного формата не требует загрузки AGG или графики целиком.
    catalog=bytearray(struct.pack('<4I',0x31465348,1,len(files)+len(aliases),0));entries=[];offset=(16+(len(files)+len(aliases))*72+4095)&~4095
    for name,p in files:
        # Codex: FTG содержит адреса, кратные сектору; база в DAT сохраняет это выравнивание.
        if name=='hmm2.ftg' or name.startswith('video/'):offset=(offset+511)&~511
        n=name.encode('ascii');assert len(n)<64
        size=p.stat().st_size;catalog+=n.ljust(64,b'\0')+struct.pack('<II',offset,size)
        entries.append({'path':name,'source':str(p),'offset':offset,'bytes':size});offset=(offset+size+3)&~3
    parents={e['path']:e for e in entries}
    for name,(parent,relative,size) in aliases:
        alias='audio/sfx/'+name;at=parents[parent]['offset']+relative
        catalog+=alias.encode('ascii').ljust(64,b'\0')+struct.pack('<II',at,size)
        entries.append({'path':alias,'alias':parent,'offset':at,'bytes':size})
    package=out/'HMM2.DAT'
    with package.open('wb') as f:
        f.write(catalog)
        for e in entries:
            if 'alias' in e:continue
            f.seek(e['offset'])
            with Path(e['source']).open('rb') as r:shutil.copyfileobj(r,f,1024*1024)
    sha=hashlib.file_digest(package.open('rb'),'sha256').hexdigest()
    (out/'manifest.json').write_text(json.dumps({'bytes':package.stat().st_size,'sha256':sha,'files':entries},ensure_ascii=False,indent=2),encoding='utf-8')
    print('Данные:',len(files)+len(aliases),'файлов,',package.stat().st_size,'байт, SHA256',sha)
if __name__=='__main__':main()
