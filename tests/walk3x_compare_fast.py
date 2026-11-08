"""Codex: полный RGB каждого DLSWAP; ускоренный исполнитель проверяется старым оракулом."""
from pathlib import Path
import argparse,ctypes as C,hashlib,itertools,json,os,struct,sys,subprocess
from ft_command_fifo import CommandFifo

def library():
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
    from build_stage1 import ROOT,VC,KIT
    out=Path(os.environ['HMM2_TMP'])/'ft_rgb';out.mkdir(exist_ok=True)
    src=ROOT/'tests/ft_rgb_replay.cpp';digest=hashlib.sha256(src.read_bytes()).hexdigest()
    # Codex: запущенный оракул Windows держит DLL открытой. Новая версия имеет
    # своё имя и не пытается перезаписать библиотеку другого сравнения.
    dll=out/('ft_rgb_'+digest[:16]+'.dll');stamp=dll.with_suffix('.sha256')
    if not dll.exists() or not stamp.exists() or stamp.read_text()!=digest:
        inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/v for v in ['ucrt','shared','um']]]
        libs=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/v/'x64' for v in ['ucrt','um']]]
        cmd=[str(VC/'bin/Hostx64/x64/cl.exe'),'/nologo','/LD','/O2','/MT','/EHsc','/std:c++17','/utf-8',*['/I'+str(p) for p in inc],str(src),'/Fo'+str(out/'ft_rgb.obj'),'/Fe'+str(dll),'/link',*['/LIBPATH:'+str(p) for p in libs]]
        r=subprocess.run(cmd,cwd=out,capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr);assert r.returncode==0,(r.stdout+r.stderr).decode(errors='replace');stamp.write_text(digest)
    d=C.CDLL(str(dll));d.ft_rgb.argtypes=[C.c_void_p,C.c_uint,C.c_void_p,C.c_void_p,C.c_void_p];d.ft_rgb.restype=C.c_int
    return d

def frames(path,d,game_frames=False):
    dl_bytes=32768 if os.environ.get('HMM2_ORACLE_WIDE_DL') else 8192
    ram,dl=bytearray(0x300000),bytearray(dl_bytes);rp=(C.c_uint8*len(ram)).from_buffer(ram);dp=(C.c_uint32*(dl_bytes//4)).from_buffer(dl)
    handles=(C.c_uint32*(32*8))();rgb=C.create_string_buffer(1024*768*3);fifo=CommandFifo()
    active=bytearray(dl_bytes);ap=(C.c_uint32*(dl_bytes//4)).from_buffer(active);ready=False;frame=0
    with path.open('rb') as f:
        while h:=f.read(8):
            address,count=struct.unpack('<II',h);raw=f.read(count*4);assert len(raw)==count*4
            if address==0xffffffff:
                if not game_frames:continue
                frame+=1;assert count==1 and struct.unpack('<I',raw)[0]==frame
                assert ready,('no display list',path,frame)
                n=1
                while n<dl_bytes//4 and ap[n-1]:n+=1
                assert ap[n-1]==0
                size=d.ft_rgb(ap,n,rp,handles,rgb);assert size>0,(path,frame,size)
                yield hashlib.sha256(rgb.raw[:size]).hexdigest()
                continue
            address&=0x7fffffff
            if address<0x300000:ram[address:address+len(raw)]=raw
            elif address<0x302000:dl[address-0x300000:address-0x300000+len(raw)]=raw
            elif address==0x302054:
                # Codex: кадр игры может изменить RAM_G без DLSWAP; исполняем
                # последний опубликованный DL на каждой метке игрового кадра.
                if game_frames:
                    active[:]=dl;ready=True;continue
                n=1
                while n<dl_bytes//4 and dp[n-1]:n+=1
                assert dp[n-1]==0
                size=d.ft_rgb(dp,n,rp,handles,rgb);assert size>0,(path,size)
                yield hashlib.sha256(rgb.raw[:size]).hexdigest()
            else:fifo.write(address,raw,ram,dl)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('a',type=Path);ap.add_argument('b',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--oracle-json',action='store_true');ap.add_argument('--game-frames',action='store_true');p=ap.parse_args()
    d=library();result=dict(a=str(p.a),b=str(p.b),frames=0,mismatch_frames=[],pixel_hashes=[],oracle_json=p.oracle_json,game_frames=p.game_frames)
    reference=(r[0] for r in json.loads(p.b.read_text())['pixel_hashes']) if p.oracle_json else frames(p.b,d,p.game_frames)
    for i,pair in enumerate(itertools.zip_longest(frames(p.a,d,p.game_frames),reference),1):
        assert None not in pair,('frame counts',i);result['frames']=i;result['pixel_hashes'].append(pair)
        if pair[0]!=pair[1]:result['mismatch_frames'].append(i)
        if i%100==0:print(i,'mismatches',len(result['mismatch_frames']),flush=True)
    p.output.write_text(json.dumps(result,indent=2));print({k:v for k,v in result.items() if k!='pixel_hashes'},flush=True)
    return bool(result['mismatch_frames'])
if __name__=='__main__':raise SystemExit(main())
