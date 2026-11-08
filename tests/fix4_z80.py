"""Codex: SD CMD18 и физические банки DMA для прежнего стенда Z80 (WC не меняется)."""
import types

def install(m, base):
    # Codex: размер очереди штатной прошивки; старый стенд DOOM задавал только 4 КиБ.
    m.link.core.from_capacity=16384
    class StreamCard(base.Card):
        stream=None
        def command(self,cmd,arg,crc):
            if cmd==18:
                self.commands.append((cmd,arg));assert not self.idle
                self.stream=arg if self.kind=='sdhc' else arg//512
                self.reply.append(0)
            elif cmd==12:
                self.commands.append((cmd,arg));self.stream=None
                self.reply.clear();self.reply.extend([255,0,0,255])
            else:super().command(cmd,arg,crc)
        def input(self,port):
            if self.selected and not self.reply and self.stream is not None and not self.packet:
                data=self.disk.read(self.stream);self.stream+=1
                self.reply.extend([255,254]);self.reply.extend(data);self.reply.extend([255,255])
                m.reads+=1;m.read_lbas.add(self.stream-1);m.sd_stream_sectors+=1
            return super().input(port)
    card=StreamCard(m.disk);card.idle=False
    controller=base.ZController(card)
    m.sd_card=card;m.sd_stream_sectors=0;m.sd_dma_bytes=0;m.sd_selected=False;m.clock_writes=[];m.dma_model_cycles=0
    # Codex: INIT старого стенда заменён; для нового потока SDHC состояние инициализации явно задано.
    from pathlib import Path
    symbols=base.symbols(Path('E:/zx/cache/tmp/hmm2_esp/stage1/launcher/build/port_sdzc.sym'))
    m.memory[symbols['SD_BLOCK_ADDRESS']]=1
    old_map=m.map3
    def map3(page):
        old_map(page)
        m.ports._ts_write(0x13,page)
    m.map3=map3
    def read_page(page,at,n):
        if page==m.page3:return bytes(m.memory[0xc000+at:0xc000+at+n])
        if page==2:return bytes(m.memory[0x8000+at:0x8000+at+n])
        return m.pages.get(page,bytes(16384))[at:at+n]
    def write_page(page,at,data):
        if page==m.page3:m.machine.set_memory_block(0xc000+at,data)
        elif page==2:m.machine.set_memory_block(0x8000+at,data)
        else:
            buf=bytearray(m.pages.get(page,bytes(16384)));buf[at:at+len(data)]=data;m.pages[page]=bytes(buf)
    original_dma=m.ports._dma_launch
    def dma(mode):
        d=m.ports.dma;n=2*(d[0x26]+1)*(d[0x28]+1)
        m.dma_model_cycles+=n*(1 if mode==1 else 4)
        def addr(reg):
            at=(d[reg+1]&63)*256+d[reg]
            assert at%2==0 and at+n<=16384
            return d[reg+2],at
        if mode==1:
            sp,sa=addr(0x1a);dp,da=addr(0x1d)
            write_page(dp,da,read_page(sp,sa,n));m.ports.dma_runs.append((mode,n));return
        if mode==2 and m.sd_selected:
            page,at=addr(0x1d)
            write_page(page,at,bytes(controller.input(0x57) for _ in range(n)))
            m.ports.dma_runs.append((mode,n));m.sd_dma_bytes+=n;return
        if mode==0x82 and m.spi_select==7:
            # Codex: DMA выбранного FT812 идёт в RAM_G, а не в буфер SPI ESP.
            page,at=addr(0x1a)
            for value in read_page(page,at,n):m.ft.exchange(value)
            m.ports.dma_runs.append((mode,n));return
        original_dma(mode)
    m.ports._dma_launch=dma
    old_out,old_in=m.port_out,m.port_in
    def out(port,value):
        if port in (0x20af,0x2baf):m.clock_writes.append((port,value))
        if port&255==0x77:
            m.sd_selected=value in (1,11)
            controller.output(port,value)
            if m.sd_selected:return
        if port&255==0x57 and m.sd_selected:controller.output(port,value);return
        old_out(port,value)
    def inp(port):
        if port&255==0x57 and m.sd_selected:return controller.input(port)
        return old_in(port)
    m.port_out,m.port_in=out,inp
    m.machine.set_output_callback(out);m.machine.set_input_callback(inp)

def patched_machine(base):
    class Machine(base.Machine):
        def __init__(self,*args,**kwargs):
            super().__init__(*args,**kwargs);install(self,base)
    return Machine
