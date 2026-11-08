"""Клод: ролик на совместном стенде: настоящие SPG (Z80, FAT32, SD, E6), ShowVideo и hmm_gpu; разбор журнала FT.
Проверяется: байты каждого кадра в media FIFO равны записи в DAT, порядок кадров по таблице, команды сопроцессора,
слой RGB565 в опубликованном DL, запрос OGG. Сам JPEG проверяет test_clip_copro.py."""
from pathlib import Path
import argparse,json,os,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))
CLIPS=Path('E:/zx/cache/tmp/hmm2_esp/video/clips')

def records(path):
    data=path.read_bytes();at=0
    while at+8<=len(data):
        a,n=struct.unpack_from('<II',data,at);at+=8
        if a==0xffffffff:yield ('frame',0,data[at:at+4*n]);at+=4*n
        elif a&0x80000000:yield ('evo',a&0x7fffffff,data[at:at+4*n]);at+=4*n
        else:yield ('link',a,data[at:at+4*n]);at+=4*n

def analyse(trace,clip_tables):
    fifo=None;base=size=0;decoded=[];current=bytearray();written=0;ring=bytearray(4096);position=0;targets=[];dlmem={};layers=0;layer_sources=set();appends=0;swaps=0
    # Клод: плата теряет байты E6, если они идут во время декодирования (FT_STAT 25); модель стенда этого не видит.
    decoding=False;e6_during_decode=0;short_register=0
    def close():
        nonlocal current
        if fifo is not None and current:decoded.append(bytes(current))
        current=bytearray()
    for kind,a,p in records(trace):
        if kind=='evo':
            if decoding:e6_during_decode+=1
            if fifo is not None and base<=a<base+size:
                at=a-base
                if at>len(current):current.extend(bytes(at-len(current)))
                current[at:at+len(p)]=p
            continue
        if kind!='link':continue
        if 0x308000<=a<0x309000:
            ring[a-0x308000:a-0x308000+len(p)]=p
        elif a==0x3020fc:
            end=struct.unpack('<I',p[:4])[0];words=[]
            while position!=end:words.append(struct.unpack_from('<I',ring,position)[0]);position=(position+4)&4095
            i=0
            while i<len(words):
                w=words[i]
                if w==0xffffff39:
                    close();fifo=base=words[i+1];size=words[i+2];decoding=False;i+=3;continue
                if w==0xffffff41:
                    targets.append(words[i+1]);decoding=True
                    if written!=len(current):short_register+=1
                    i+=3;continue
                if w==0xffffff1e:
                    start,count=words[i+1],words[i+2];appends+=1
                    dl=[dlmem.get(start+k,0) for k in range(0,count,4)]
                    for k,word in enumerate(dl):
                        if word>>24==7 and (word>>19)&31==7:
                            layers+=1
                            for back in range(max(0,k-3),k):
                                if dl[back]>>24==1:layer_sources.add(dl[back]&0x3fffff)
                    i+=3;continue
                i+=1
        elif a==0x309018:written=struct.unpack('<I',p[:4])[0]
        elif a==0x302054:swaps+=1
        elif a<0x300000:
            for k in range(0,len(p),4):dlmem[a+k]=struct.unpack_from('<I',p,k)[0]
    close()
    return dict(e6_during_decode=e6_during_decode,short_register=short_register,decoded=decoded,targets=targets,layers=layers,layer_sources=layer_sources,appends=appends,swaps=swaps,last_written=written)

def expected(name):
    raw=(CLIPS/(name[:-4].lower()+'.smk')).read_bytes()
    count=struct.unpack_from('<I',raw,12)[0];rows=[struct.unpack_from('<II',raw,24+i*8) for i in range(count)]
    out=[];last=None
    for offset,size in rows:
        if offset!=last:out.append(raw[offset:offset+size]);last=offset
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('clip');ap.add_argument('--timeout',type=int,default=280);ap.add_argument('--dat',default='E:/zx/cache/tmp/hmm2_esp/video/dat/HMM2.DAT');ap.add_argument('--spg');a=ap.parse_args()
    name='clip_'+a.clip.split('+')[0].split('.')[0].lower()
    env=dict(os.environ,HMM2_TEST_CLIP=a.clip,HMM2_JOINT_DAT=a.dat,HMM2_JOINT_SPC='64',HMM2_TEST_REALTIME='1')
    cmd=[sys.executable,str(ROOT/'tests/test_stage1_joint.py'),'--name',name,'--scene-map','KINGDTH.MX2','--timeout',str(a.timeout)]
    if a.spg:cmd+=['--spg',a.spg]
    p=subprocess.run(cmd,env=env,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
    print(p.stdout[-400:]);print(p.stderr[-800:])
    out=BASE/'joint'/name;result=analyse(out/'frames.ft',None)
    names=[x.split(':')[0] for x in a.clip.split('+')];want=[f for n in names for f in expected(n)]
    decoded=result['decoded']
    # Клод: фон и ролик поверх чередуются, поэтому порядок проверяем внутри каждого ролика, а байты — по множеству.
    missing=[i for i,f in enumerate(decoded) if f not in set(want)]
    single=len(names)==1
    # Клод: по настоящим часам отстающие кадры пропускаются, поэтому порядок — подпоследовательность таблицы.
    def subsequence(part,whole):
        it=iter(whole);return all(f in it for f in part)
    order=subsequence(decoded,expected(names[0])) if single else None
    report=dict(e6_during_decode=result['e6_during_decode'],short_register=result['short_register'],clip=a.clip,decoded=len(decoded),expected=len(want),bad_bytes=len(missing),in_order=order,targets=sorted(set(result['targets'])),layers=result['layers'],layer_sources=sorted(result['layer_sources']),appends=result['appends'],swaps=result['swaps'],seconds=None)
    res=out/'result.json'
    if res.exists():
        r=json.loads(res.read_text(encoding='utf-8'));report.update(frames=r['frames'],exit=r['exit'],seconds=round(r['elapsed_s'],1),invalid_ft=r['lifecycle']['invalid_ft'])
    print(report)
    fails=[]
    if result['e6_during_decode']:fails.append('E6 во время декодирования')
    if result['short_register']:fails.append('REG_MEDIAFIFO_WRITE меньше кадра')
    if missing:fails.append('байты кадра не совпали')
    if not decoded:fails.append('нет кадров')
    if single and not order:fails.append('порядок кадров')
    if len(decoded)<len(want)//4:fails.append('пропущено больше трёх четвертей кадров')
    if not result['layers']:fails.append('нет слоя RGB565 в DL')
    if not set(result['layer_sources'])<=set(result['targets']):fails.append('DL рисует не буфер кадра')
    print('ОШИБКИ',fails) if fails else print('OK')
    return 1 if fails else 0
if __name__=='__main__':raise SystemExit(main())
