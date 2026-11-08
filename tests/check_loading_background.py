"""Codex: края Map loading совпадают с полным исходным HEROES, без чёрных полей ROI."""
from pathlib import Path
import hashlib,json,struct,sys,zlib
from native_ft_frames import frames
from check_fix5_ft import np,Image

def main():
    folder=Path(sys.argv[1]);target=int(sys.argv[2]) if len(sys.argv)>2 else 59
    gfx=Path('E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.GFX').read_bytes()
    group,count=struct.unpack_from('<II',gfx,16+279*8);assert count>0
    offset,size,w,h,x,y,single=struct.unpack_from('<IIiiiiI',gfx,group)
    assert (w,h,x,y,single)==(640,480,0,0,1)
    original=zlib.decompress(gfx[offset:offset+size]);assert len(original)==w*h
    for serial,rgb,shadow in frames(folder):
        if serial!=target:continue
        palette=np.frombuffer((folder/f'frame_{serial:04}.pal').read_bytes(),dtype=np.uint8).reshape(256,3)
        expected=palette[np.frombuffer(original,dtype=np.uint8).reshape(h,w)]
        edge=np.zeros((h,w),dtype=bool);edge[:8]=edge[-8:]=True;edge[:,:8]=edge[:,-8:]=True;edge[:40,:40]=False
        different=int((edge&np.any(rgb!=expected,axis=2)).sum())
        result=dict(frame=serial,edge_pixels=int(edge.sum()),edge_differences=different,source='original HEROES frame0, 640x480',source_sha256=hashlib.sha256(original).hexdigest(),full_background=different==0)
        (folder/'loading_background_check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        Image.fromarray(rgb).save(folder/'loading_fullscreen.png');print(result);return int(different!=0)
    raise AssertionError('Missing loading frame')
if __name__=='__main__':raise SystemExit(main())
