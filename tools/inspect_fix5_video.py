"""Codex: измеряем готовые кадры SMK исходным libsmacker, не меняя оригиналы."""
from pathlib import Path
import ctypes as c,hashlib,json,os,subprocess,sys,time
from build_stage1 import VC,KIT,ROOT

OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/video')
SOURCE=ROOT/'third_party/openhmm2/src/thirdparty/libsmacker'

def decoder():
    OUT.mkdir(exist_ok=True,parents=True);dll=OUT/'smacker.dll'
    exports=['smk_open_file','smk_close','smk_info_all','smk_info_video','smk_enable_all','smk_first','smk_next','smk_get_palette','smk_get_video']
    if not dll.exists():
        inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']]]
        libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
        cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/O2','/MT','/TC','/D_CRT_SECURE_NO_WARNINGS','/LD',*['/I'+str(p) for p in inc],str(SOURCE/'smacker.c'),'/Fo'+str(OUT/'smacker.obj'),'/Fe'+str(dll),'/link',*['/LIBPATH:'+str(p) for p in libs],*['/EXPORT:'+n for n in exports]]
        p=subprocess.run(cmd,cwd=OUT,capture_output=True);(OUT/'build.log').write_bytes(p.stdout+p.stderr);p.check_returncode()
    d=c.CDLL(str(dll));ptr=c.c_void_p
    d.smk_open_file.argtypes=[c.c_char_p,c.c_ubyte];d.smk_open_file.restype=ptr
    d.smk_close.argtypes=[ptr];d.smk_close.restype=None
    d.smk_info_all.argtypes=[ptr,c.POINTER(c.c_ulong),c.POINTER(c.c_ulong),c.POINTER(c.c_double)]
    d.smk_info_video.argtypes=[ptr,c.POINTER(c.c_ulong),c.POINTER(c.c_ulong),c.POINTER(c.c_ubyte)]
    d.smk_enable_all.argtypes=[ptr,c.c_ubyte]
    for name in ['smk_first','smk_next','smk_get_palette','smk_get_video']:
        getattr(d,name).argtypes=[ptr];getattr(d,name).restype=ptr if name.startswith('smk_get_') else c.c_byte
    return d

def main():
    sys.path.insert(0,'E:/zx/cache/tmp/hmm2_esp/fix4/python');import numpy as np
    d=decoder();rows=[];started=time.monotonic()
    for path in sorted(Path('E:/zx/HMM2/Assets/Original/ANIM').glob('*.SMK')):
        handle=d.smk_open_file(str(path).encode(),1);assert handle,path
        try:
            w=c.c_ulong();h=c.c_ulong();n=c.c_ulong();us=c.c_double();scale=c.c_ubyte()
            assert d.smk_info_all(handle,None,c.byref(n),c.byref(us))>=0
            assert d.smk_info_video(handle,c.byref(w),c.byref(h),c.byref(scale))>=0
            d.smk_enable_all(handle,128);assert d.smk_first(handle)>=0
            size=(w.value*h.value+511)&~511;previous=[np.zeros((size//512,512),dtype=np.uint8) for _ in range(2)]
            total=maximum=0;frame_sizes=[];digest=hashlib.sha256()
            for frame in range(n.value):
                raw=c.string_at(d.smk_get_video(handle),w.value*h.value);pal=c.string_at(d.smk_get_palette(handle),768)
                digest.update(raw);digest.update(pal)
                a=np.frombuffer(raw.ljust(size,b'\0'),dtype=np.uint8).reshape(-1,512)
                dirty=np.any(a!=previous[frame&1],axis=1) if frame>=2 else np.ones(size//512,dtype=bool)
                count=int(dirty.sum())*512+1024;total+=count;maximum=max(maximum,count);frame_sizes.append(count)
                previous[frame&1]=a.copy()
                if frame+1<n.value:assert d.smk_next(handle)>=0
            row=dict(file=path.name,width=w.value,height=h.value,yscale=scale.value,frames=n.value,microseconds_per_frame=us.value,
                     decoded_sha256=digest.hexdigest(),raw_bytes=size*n.value,parity_delta_bytes=total,max_frame_bytes=maximum,
                     mean_KiBs=total/n.value/(us.value/1e6)/1024,frame_sizes=frame_sizes)
            rows.append(row);print({k:v for k,v in row.items() if k!='frame_sizes'},flush=True)
        finally:d.smk_close(handle)
    (OUT/'inspection.json').write_text(json.dumps(dict(rows=rows,seconds=time.monotonic()-started),indent=2),encoding='utf-8')

if __name__=='__main__':main()
