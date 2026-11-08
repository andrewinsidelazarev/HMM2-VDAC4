"""Клод: ролики SMK оригинала для FT812: каждый кадр — отдельный мини-AVI с JPEG, выровненный по сектору.

Формат video/<имя>.smk в HMM2.DAT:
  'HMV1', ширина, высота, кадров, мкс на кадр, флаги (1 — есть video/<имя>.ogg, 2 — только звук),
  затем на кадр [смещение от начала записи, длина кратно 512]. Заголовок дополнен до 512.
Кадр читает E6 прямо в media FIFO (кратно 512, без ESP). Мини-AVI самодостаточен: CMD_MEDIAFIFO,
CMD_VIDEOSTART, CMD_VIDEOFRAME на каждый кадр, поэтому работают цикл и два ролика сразу.
Качество подбирается на ролик: средний поток не выше MAX_RATE, кадр меньше FIFO.
Звук — OGG 44100 Гц: hmm_audio.cpp принимает только эту частоту; дорожки SMK сводятся в одну."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys

FFMPEG=r'C:\Users\andre\AppData\Local\FTViewConvert\ffmpeg-9.0.2.exe'
ROOT=Path(__file__).resolve().parent.parent
ANIM=Path('E:/zx/HMM2 VDAC4/Assets/ANIM')
SOURCES=ROOT/'third_party/openhmm2/src/fheroes2'
OUT=Path('E:/zx/cache/tmp/hmm2_esp/video/clips')
# Клод: матрица FTViewConvert, проверенная на плате с FT812 и BL616.
MATRIX=('8,10,11,20,23,24,46,54,10,10,20,22,24,46,54,59,11,20,23,24,46,54,54,61,'
        '20,20,23,43,46,54,59,96,20,23,43,46,51,56,96,115,23,43,46,51,56,96,115,139,'
        '42,43,46,54,91,110,134,166,43,46,56,91,110,134,166,199')
FIFO_BYTES=256*1024
MAX_RATE=380*1024
QUALITIES=(2,3,4,5,6,8,10,12,15,20,26,31)

def used_clips():
    """Клод: только ролики, которые называет игра; без видео — те, что звучат через PLAY_AUDIO или CHOOSEW."""
    video,audio=set(),{'CHOOSEW.SMK'}
    for p in SOURCES.rglob('*.cpp'):
        s=p.read_text(encoding='utf-8')
        for name,control in re.findall(r'"([A-Za-z0-9_]+\.SMK)"(?:\s*,\s*Video::VideoControl::(\w+))?',s):
            (audio if control=='PLAY_AUDIO' else video).add(name.upper())
    audio-=video-{'CHOOSEW.SMK'}
    video.discard('CHOOSEW.SMK')
    return sorted(video),sorted(audio)

def find_smk(name):
    for p in ANIM.iterdir():
        if p.name.upper()==name:return p
    return None

def smk_header(path):
    b=path.read_bytes()[:104]
    if b[:3]!=b'SMK':raise ValueError(path)
    w,h,frames,rate,flags=struct.unpack('<IIIiI',b[4:24])
    us=rate*1000 if rate>0 else -rate*10 if rate<0 else 100000
    tracks=sum(1 for a in struct.unpack('<7I',b[72:100]) if a&0x40000000)
    return w,h,frames,us,flags,tracks

def split_jpegs(data):
    # Клод: в сжатых данных FF экранирован, поэтому FFD9 встречается только как конец кадра.
    frames,at=[],0
    while at<len(data):
        end=data.find(b'\xff\xd9',at)
        if end<0:raise ValueError('JPEG без EOI')
        frames.append(data[at:end+2]);at=end+2
    return frames

def encode(path,q):
    cmd=[FFMPEG,'-hide_banner','-loglevel','error','-nostdin','-i',str(path),'-map','0:v:0','-fps_mode','passthrough',
         '-vf','format=yuvj420p','-c:v','mjpeg','-huffman','default','-intra_matrix',MATRIX,'-q:v',str(q),'-f','image2pipe','-']
    return split_jpegs(subprocess.run(cmd,check=True,capture_output=True).stdout)

def mini_avi(jpeg,w,h):
    # Клод: раскладка обычного AVI (88 байт до movi): её ждут и BL616, и настоящий FT812.
    avih=struct.pack('<14I',0,0,0,0x10,1,0,1,0,w,h,0,0,0,0)
    hdrl=b'hdrl'+b'avih'+struct.pack('<I',len(avih))+avih
    chunk=b'00dc'+struct.pack('<I',len(jpeg))+jpeg+bytes(len(jpeg)&1)
    movi=b'movi'+chunk
    body=b'AVI '+b'LIST'+struct.pack('<I',len(hdrl))+hdrl+b'LIST'+struct.pack('<I',len(movi))+movi
    data=b'RIFF'+struct.pack('<I',len(body))+body
    return data+bytes(-len(data)&511)

def build_video(path,us):
    w,h=smk_header(path)[:2]
    for q in QUALITIES:
        frames=encode(path,q)
        size=sum(len(j) for j in frames);seconds=max(len(frames)*us/1e6,1e-3);peak=max(len(j) for j in frames)
        if size/seconds<=MAX_RATE and peak+1024<FIFO_BYTES:break
    else:raise RuntimeError(f'{path.name}: нет качества под поток')
    return frames,q,int(size/seconds),peak,w,h

def encode_audio(path,tracks,out):
    cmd=[FFMPEG,'-hide_banner','-loglevel','error','-nostdin','-y','-i',str(path)]
    if tracks>1:
        cmd+=['-filter_complex',''.join(f'[0:a:{i}]' for i in range(tracks))+f'amix=inputs={tracks}:normalize=0[a]','-map','[a]']
    else:cmd+=['-map','0:a:0']
    cmd+=['-ar','44100','-c:a','libvorbis','-q:a','3','-f','ogg',str(out)]
    subprocess.run(cmd,check=True)

def main():
    global ANIM
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,default=OUT);ap.add_argument('--anim-dir',type=Path,default=ANIM);ap.add_argument('--only',nargs='*');args=ap.parse_args()
    ANIM=args.anim_dir
    args.out.mkdir(parents=True,exist_ok=True)
    video,audio=used_clips();report=[];missing=[]
    for name,video_on in [(n,True) for n in video]+[(n,False) for n in audio]:
        if args.only and name not in args.only:continue
        path=find_smk(name)
        if not path:missing.append(name);continue
        w,h,count,us,flags,tracks=smk_header(path);stem=name[:-4].lower()
        if tracks:encode_audio(path,tracks,args.out/(stem+'.ogg'))
        table=bytearray();blobs=[];item=dict(name=name,tracks=tracks)
        if video_on:
            frames,q,rate,peak,w,h=build_video(path,us);count=len(frames)
            header=(24+count*8+511)&~511;at=header;seen={}
            for jpeg in frames:
                key=hashlib.sha1(jpeg).digest()
                if key in seen:table+=struct.pack('<II',*seen[key]);continue
                blob=mini_avi(jpeg,w,h);seen[key]=(at,len(blob));table+=struct.pack('<II',at,len(blob));blobs.append(blob);at+=len(blob)
            item.update(q=q,rate=rate,peak=peak,frames=count,unique=len(seen))
        record=struct.pack('<4s5I',b'HMV1',w,h,count,us,(1 if tracks else 0)|(0 if video_on else 2))+bytes(table)
        record+=bytes(-len(record)&511)
        (args.out/(stem+'.smk')).write_bytes(record+b''.join(blobs))
        item['bytes']=len(record)+sum(len(b) for b in blobs);report.append(item)
        print(item,flush=True)
    (args.out/'manifest.json').write_text(json.dumps(dict(missing=missing,clips=report),indent=1),encoding='utf-8')
    total=sum(i['bytes'] for i in report)+sum(p.stat().st_size for p in args.out.glob('*.ogg'))
    print('Клипов',len(report),'МиБ',total//1048576,'нет файла',missing)
if __name__=='__main__':main()
