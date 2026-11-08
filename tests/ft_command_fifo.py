"""Codex: минимальный строгий исполнитель CMD_DLSTART/CMD_APPEND для журналов FT812."""
import struct
class CommandFifo:
    def __init__(self):
        self.ram=bytearray(4096);self.read=0;self.dl_at=0;self.appends=0
        self.inflater=None;self.inflate_at=0
        self.writeback=None
        self.media_base=0;self.media_size=0;self.media_written=0;self.video_frames=0
    def write(self,address,data,ram,dl):
        if address==0x309018:
            self.media_written=struct.unpack('<I',data)[0];return True
        if 0x308000<=address<0x309000:
            at=address-0x308000
            assert not at%4 and not len(data)%4 and at+len(data)<=4096
            self.ram[at:at+len(data)]=data;return True
        if address!=0x3020fc:return False
        assert len(data)==4
        end=struct.unpack('<I',data)[0];assert 0<=end<4096 and not end%4
        def take():
            assert self.read!=end,'Незавершённая команда FIFO'
            v=struct.unpack_from('<I',self.ram,self.read)[0];self.read=(self.read+4)&4095;return v
        while self.read!=end:
            if self.inflater is not None:
                raw=struct.pack('<I',take());data=self.inflater.decompress(raw)
                assert self.inflate_at+len(data)<=len(ram)
                if data and self.writeback:self.writeback(self.inflate_at,data)
                ram[self.inflate_at:self.inflate_at+len(data)]=data;self.inflate_at+=len(data)
                if self.inflater.eof:self.inflater=None
                continue
            command=take()
            if command==0xffffff00:self.dl_at=0
            elif command==0xffffff22:
                import zlib
                self.inflate_at=take();self.inflater=zlib.decompressobj()
            elif command==0xffffff1d:
                target,source,size=take(),take(),take()
                assert source+size<=len(ram) and target+size<=len(ram)
                ram[target:target+size]=ram[source:source+size]
                if self.writeback:self.writeback(target,bytes(ram[target:target+size]))
            elif command==0xffffff1b:
                target,value,size=take(),take(),take()
                assert target+size<=len(ram)
                ram[target:target+size]=bytes([value&255])*size
                if self.writeback:self.writeback(target,bytes(ram[target:target+size]))
            elif command==0xffffff1e:
                source,size=take(),take()
                assert not source%4 and not size%4 and source+size<=len(ram)
                assert self.dl_at+size<=len(dl),'Переполнение DL через CMD_APPEND'
                dl[self.dl_at:self.dl_at+size]=ram[source:source+size];self.dl_at+=size;self.appends+=1
            elif command==0xffffff39:
                self.media_base,self.media_size=take(),take();self.media_written=0
                assert self.media_base+self.media_size<=len(ram)
            elif command==0xffffff40:pass
            elif command==0xffffff41:
                # Codex: снимок трассы восстанавливает штатный JPEG в RGB565; аппаратную проверку выполняет test_clip_copro.
                import io
                import numpy as np
                from PIL import Image
                target,result=take(),take()
                assert 0<self.media_written<=self.media_size
                avi=bytes(ram[self.media_base:self.media_base+self.media_written])
                start=avi.find(b'\xff\xd8');jpeg_end=avi.find(b'\xff\xd9',start)
                assert start>=0 and jpeg_end>start,'Нет JPEG в mini-AVI'
                rgb=np.asarray(Image.open(io.BytesIO(avi[start:jpeg_end+2])).convert('RGB'),dtype=np.uint16)
                raw=(((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)).astype('<u2').tobytes()
                assert target+len(raw)<=len(ram) and result+4<=len(ram)
                ram[target:target+len(raw)]=raw;ram[result:result+4]=bytes(4);self.video_frames+=1
            else:raise AssertionError('Неожиданная команда FIFO '+hex(command))
        return True
