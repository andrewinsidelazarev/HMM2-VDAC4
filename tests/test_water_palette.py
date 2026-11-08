"""Codex: вода сборки 99, фактический CMD_APPEND, полный RGB и вход для RTL Slot1."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,statistics,struct
from walk3x_compare_fast import library
from ft_command_fifo import CommandFifo


def record(frame,dl=(),writes=(),expected=b''):
    data=struct.pack('<5I',frame,0,7000,0,len(dl))+struct.pack('<%dI'%len(dl),*dl)
    data+=struct.pack('<I',len(writes))
    for at,raw in writes:data+=struct.pack('<II',at,len(raw))+raw
    return data+struct.pack('<I',len(expected))+expected


def inspect(path,output=None):
    d=library();ram=bytearray(0x300000);dl=bytearray(8192);active=bytearray(8192)
    rp=(C.c_uint8*len(ram)).from_buffer(ram);ap=(C.c_uint32*2048).from_buffer(active)
    handles=(C.c_uint32*(32*8))();rgb=C.create_string_buffer(1024*768*3);fifo=CommandFifo()
    rows=[];writes=[];steps=[];traffic=atlas=0
    with path.open('rb') as f:
        while header:=f.read(8):
            at,count=struct.unpack('<II',header);raw=f.read(count*4);assert len(raw)==count*4
            if at==0xffffffff:
                frame,=struct.unpack('<I',raw);n=1
                while n<2048 and ap[n-1]:n+=1
                size=d.ft_rgb(ap,n,rp,handles,rgb);assert size==640*480*3,(frame,size)
                pixels=rgb.raw[:size];rows.append(dict(frame=frame,bytes=traffic,atlas=atlas,rgb=hashlib.sha256(pixels).hexdigest()))
                if output and frame==5:
                    output.mkdir(exist_ok=True);(output/'ramg.bin').write_bytes(ram)
                    init=[]
                    # Codex: сборка 99 сохраняет BITMAP_HANDLE между кадрами; первый лист задаёт точный контекст.
                    for h in range(32):
                        source,stride,bh,bw,oh,fmt,wx,wy=handles[h*8:h*8+8]
                        init += [0x05000000|h,0x01000000|source,0x07000000|(fmt<<19)|((stride&1023)<<9)|(bh&511),
                                 0x28000000|((stride>>10)<<2)|(bh>>9),0x08000000|(wx<<19)|(wy<<18)|((bw&511)<<9)|(oh&511),
                                 0x29000000|((bw>>9)<<2)|(oh>>9)]
                    steps=[record(0,init+[0]),record(5,list(ap[:n]))]
                elif output and 6<=frame<=25:
                    # Codex: новые записи палитры исполняет RTL. Готовый DL получен штатным CMD_APPEND без замены его семантики.
                    steps.append(record(frame,list(ap[:n]),writes,pixels))
                traffic=atlas=0;writes=[];continue
            traffic+=len(raw);is_atlas=bool(at&0x80000000);at&=0x7fffffff
            if is_atlas:atlas+=len(raw)
            if at<0x300000:ram[at:at+len(raw)]=raw;writes.append((at,raw))
            elif at<0x302000:dl[at-0x300000:at-0x300000+len(raw)]=raw
            elif at==0x302054:active[:]=dl
            else:fifo.write(at,raw,ram,dl)
    if output:
        assert len(steps)==22
        (output/'water.vtr').write_bytes(b'VTR2'+struct.pack('<I',len(steps))+b''.join(steps))
    return rows


def main():
    ap=argparse.ArgumentParser();ap.add_argument('before',type=Path);ap.add_argument('after',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    before=inspect(a.before);after=inspect(a.after,a.output/'slot1')
    assert len(before)==len(after)==70
    assert [r['rgb'] for r in before]==[r['rgb'] for r in after]
    assert len({r['rgb'] for r in after[3:23]})>=5
    result=dict(frames=70,rgb_exact=True)
    for name,rows in [('before',before),('after',after)]:
        rows=rows[6:63];assert all(r['atlas']==0 for r in rows)
        result[name]=dict(link_median=statistics.median(r['bytes'] for r in rows),link_max=max(r['bytes'] for r in rows))
    assert result['after']['link_max']<result['before']['link_median']
    (a.output/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
