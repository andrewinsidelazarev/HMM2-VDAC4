"""Клод: все кадры всех роликов проходят настоящий код сопроцессора BL616, а выбранные сверяются с SMK оригинала.
Порог PSNR проверяет геометрию и цвета; потери JPEG при подобранном качестве ниже него не опускаются."""
from pathlib import Path
import concurrent.futures,json,math,os,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT
from build_stage1_clips import FFMPEG,find_smk
COPRO=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/Tang Nano 20k/BL616/copro')
CLIPS=Path(os.environ.get('HMM2_CLIPS','E:/zx/cache/tmp/hmm2_esp/video/clips'))
# Codex: отдельный каталог позволяет проверить клипы, не меняя результаты других стадий.
OUT=Path(os.environ.get('HMM2_CLIP_COPRO_OUT','E:/zx/cache/tmp/hmm2_esp/video/copro'));OUT.mkdir(parents=True,exist_ok=True)
MIN_PSNR=24.0

def build():
    exe=OUT/'clip_copro.exe'
    inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],COPRO,COPRO/'test']
    libs=[VC/'lib/x64',KIT/'Lib/10.0.26100.0/ucrt/x64',KIT/'Lib/10.0.26100.0/um/x64']
    # Клод: ft_3d.c написан для gcc/clang, cl его не собирает; компоновщик остаётся из VC.
    os.environ['PATH']=str(VC/'bin/Hostx64/x64')+os.pathsep+os.environ['PATH']
    cmd=['E:/zx/llvm/bin/clang-cl.exe','/nologo','/O2','/MT','/utf-8','/w','/std:c11','/D_CRT_SECURE_NO_WARNINGS','/DFTC3D_PROBE',*['/I'+str(p) for p in inc],
         str(ROOT/'tests/clip_copro.c'),str(COPRO/'ft_copro.c'),str(COPRO/'ft_inflate.c'),str(COPRO/'ft_image.c'),str(COPRO/'ft_3d.c'),'/Fe'+str(exe),'/Fo'+str(OUT)+'\\','/link',*['/LIBPATH:'+str(p) for p in libs]]
    r=subprocess.run(cmd,capture_output=True,cwd=OUT)
    if r.returncode:raise SystemExit(r.stdout.decode(errors='replace')[-3000:])
    return exe

def reference(name,frames,w,h):
    path=find_smk(name)
    select='+'.join(f'eq(n\\,{n})' for n in frames)
    raw=subprocess.run([FFMPEG,'-hide_banner','-loglevel','error','-nostdin','-i',str(path),'-map','0:v:0','-vf',f'select={select}','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','rgb24','-'],check=True,capture_output=True).stdout
    size=w*h*3;assert len(raw)==size*len(frames),(name,len(raw),size,frames)
    return {n:raw[i*size:(i+1)*size] for i,n in enumerate(frames)}

def psnr(rgb565,rgb):
    total=0;count=len(rgb)//3
    for i in range(count):
        v=rgb565[i*2]|rgb565[i*2+1]<<8
        r,g,b=(v>>11)<<3,((v>>5)&63)<<2,(v&31)<<3
        a=rgb[i*3:i*3+3]
        total+=(r-a[0])**2+(g-a[1])**2+(b-a[2])**2
    mse=max(total/(count*3),1e-3)
    return 10*math.log10(255*255/mse)

def check(exe,path):
    h=struct.unpack('<6I',path.read_bytes()[:24]);w,height,count,flags=h[1],h[2],h[3],h[5]
    if flags&2:return None
    frames=sorted({0,count//2,count-1});folder=OUT/path.stem;folder.mkdir(exist_ok=True)
    r=subprocess.run([str(exe),str(path),str(folder),*map(str,frames)],capture_output=True,text=True,timeout=900)
    if r.returncode:return dict(name=path.stem,error=r.stdout.strip() or r.returncode)
    ref=reference(path.stem.upper()+'.SMK',frames,w,height)
    worst=min(psnr((folder/f'frame_{n:04d}.565').read_bytes(),ref[n]) for n in frames)
    return dict(name=path.stem,frames=count,psnr=round(worst,1),result=r.stdout.strip())

def main():
    exe=build();paths=sorted(CLIPS.glob('*.smk'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=[r for r in pool.map(lambda p:check(exe,p),paths) if r]
    bad=[r for r in rows if 'error' in r or r['psnr']<MIN_PSNR]
    for r in sorted(rows,key=lambda r:r.get('psnr',0))[:12]:print(r)
    print('роликов',len(rows),'кадров',sum(r.get('frames',0) for r in rows),'плохих',len(bad))
    (OUT/'report.json').write_text(json.dumps(rows,indent=1),encoding='utf-8')
    return 1 if bad else 0
if __name__=='__main__':raise SystemExit(main())
