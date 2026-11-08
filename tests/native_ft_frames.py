"""Codex: повторение нативного потока FT с сохранением состояния ручек между кадрами."""
from pathlib import Path
import struct
from check_fix5_ft import render
from ft_command_fifo import CommandFifo

def frames(folder):
    raw=(Path(folder)/'frames.ft').read_bytes();ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo();at=0;rgb=shadow=None
    handles=[dict(source=0,stride=0,bh=0,bw=0,oh=0) for _ in range(32)]
    while at<len(raw):
        address,count=struct.unpack_from('<II',raw,at);at+=8;n=count*4;data=raw[at:at+n];at+=n
        if address==0xffffffff:
            yield struct.unpack('<I',data)[0],rgb,shadow;continue
        address&=0x7fffffff
        if address<0x300000:ram[address:address+n]=data
        elif address<0x302000:dl[address-0x300000:address-0x300000+n]=data
        elif address==0x302054:
            words=[]
            for word, in struct.iter_unpack('<I',dl):
                words.append(word)
                if not word:break
            rgb,shadow=render(words,ram,handle_state=handles)
        elif fifo.write(address,data,ram,dl):pass
        else:assert address in (0x302034,0x302048,0x302070),hex(address)
