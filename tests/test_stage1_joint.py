"""Codex: SPG → Z80/FAT32/SD → SPI ядра → настоящее main и файловый слой игры.

Канал и кадры протокола исполняются из C++ прошивки, а не из Python LinkCore.
Папка SD находится только в памяти стенда; пользовательская карта не открывается.
"""
from pathlib import Path
import argparse,ctypes as C,hashlib,importlib.util,json,os,struct,sys,time,types,shutil
from _ctypes import FreeLibrary
ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))
# Codex: результаты длительных прогонов можно держать отдельно от сборочной стадии.
RUNS=Path(os.environ.get('HMM2_JOINT_RUNS',str(BASE/'joint')))
REF=Path('E:/zx/DOOM VDAC4')
sys.path.insert(0,'E:/zx/esp/fat32-venv/Lib/site-packages')
spec=importlib.util.spec_from_file_location('doom_joint_reference',REF/'tests/test_launcher.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
base.ROOT=BASE/'launcher'
from spi_link_model import SlaveHd,EspSide,SpiPorts
picture=types.ModuleType('make_launcher_pic');picture.blocks=lambda:[];sys.modules['make_launcher_pic']=picture

def fixture(blobs,nested=False):
    blobs=dict(blobs);blobs.setdefault('OGG.ELF',Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Sound/modules/out/OGG.ELF').read_bytes())
    # Клод: HMM2_JOINT_SPC — кластер как на настоящей карте (32 КиБ = 64 сектора); без него прежние 8.
    spc=int(os.environ.get('HMM2_JOINT_SPC','8'));size=spc*512
    # Клод: том по размеру файлов: DAT с роликами больше прежних 512 МиБ.
    disk=base.Disk(spc=spc,partition=2048,clusters=max(131072*8//spc,sum(len(b) for b in blobs.values())//size*103//100+4096))
    records=bytearray();first=100
    for name,blob in blobs.items():
        count=(len(blob)+size-1)//size
        clusters=[first+i+(i//4000)*17 for i in range(count)]
        for i,c in enumerate(clusters):
            disk.set_fat(c,clusters[i+1] if i+1<count else 0x0fffffff)
            for j in range(spc):disk.write(disk.data_start+(c-2)*spc+j,blob[i*size+j*512:i*size+(j+1)*512].ljust(512,b'\0'))
        if name=='OGG.ELF':audio_entry=base.entry(name,first,len(blob))
        else:records+=base.entry(name,first,len(blob))
        first=clusters[-1]+100
    disk.set_fat(20,0x0fffffff);disk.set_fat(21,0x0fffffff)
    disk.write(disk.data_start+(20-2)*spc,base.entry('ESP',21,directory=True).ljust(512,b'\0'))
    disk.write(disk.data_start+(21-2)*spc,audio_entry.ljust(512,b'\0'))
    wc=base.entry('WC',20,directory=True)
    assert len(records)+len(wc)<512
    if nested:
        disk.set_fat(3,0x0fffffff);disk.set_fat(7,0x0fffffff)
        disk.write(disk.data_start,(base.entry('GAMES',3,directory=True)+wc).ljust(512,b'\0'))
        disk.write(disk.data_start+spc,base.entry('HMM2',7,directory=True).ljust(512,b'\0'))
    disk.write(disk.data_start+(5*spc if nested else 0),(records+(b'' if nested else wc)).ljust(512,b'\0'))
    return disk

class Native:
    def __init__(self,out,elf,period=0,start_failures=0,start_status=255,reboot_ms=1500):
        # Codex: контрольная DLL прежнего файлового слоя хранится отдельно от конечного стенда.
        build_path=Path(os.environ.get('HMM2_JOINT_BUILD',str(BASE/'joint/build.json')))
        self.library_path=build_path.parent
        self.dll_dir=os.add_dll_directory(str(self.library_path))
        self.build=json.loads(build_path.read_text());self.d=C.CDLL(str(self.library_path/self.build['dll']));self.out=out;self.elf=elf
        self.buffer=C.create_string_buffer(10000);self.controls=C.create_string_buffer(10000)
        self.done=False;self.started=False;self.upload=bytearray();self.paths={};self.bulk_data=b''
        self.app_dll=None;self.cached=False;self.cached_modules=set();self.module_name='HMM2.ELF';self.ogg=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Sound/modules/out/OGG.ELF').read_bytes();self.app_out=out;self.unloads=0
        self.cache_policy=None
        # Клод: HMM2_JOINT_PUT_REFUSE=N — на первые N запросов быстрого куска модуля (MOD_PUT с флагом медиа) ESP не даёт PUT_READY:
        # проверка запасного MOD_PUT лаунчера (04.11 плата после ядра MD: STAGE 08 CODE 00). Лаунчер спрашивает до 100 раз на кусок.
        self.put_refuse=int(os.environ.get('HMM2_JOINT_PUT_REFUSE','0'));self.put_refused=0
        # Codex: отказы стека задаются до загрузки DLL; удачная попытка запускает настоящее main.
        self.start_failures=start_failures;self.start_status=start_status;self.start_attempts=0
        self.resets=0;self.boot_controls=[]
        if os.environ.get('HMM2_JOINT_CACHE_POLICY'):
            from stage1_cache_policy import Policy
            self.cache_policy=Policy(self.ogg,elf,os.environ['HMM2_JOINT_CACHE_POLICY'])
        self.trace=(out/'spi.jsonl').open('w',encoding='utf-8')
        self.d.joint_init(str(out).encode());self.d.joint_result.restype=C.c_int
        self.d.joint_period(max(1,round(period)))
        self.d.joint_core.restype=C.c_void_p;self.d.joint_marker.restype=C.c_void_p
        self.d.joint_bind.argtypes=[C.c_void_p]
        if hasattr(self.d,'joint_reboot_delay'):self.d.joint_reboot_delay(reboot_ms)
    def release_app(self):
        if self.app_dll and not self.d.joint_loaded():
            # Codex: транспорт ESP и состояние Tang сохраняются; CRT выгружает только приложение.
            FreeLibrary(self.app_dll._handle);self.app_dll=None;self.unloads+=1
    def load_app(self):
        self.release_app();assert self.app_dll is None
        self.app_out.mkdir(parents=True,exist_ok=True)
        self.app_dll=C.CDLL(str(self.library_path/self.build['app']))
        init=self.app_dll.joint_app_init;init.restype=C.c_void_p;init.argtypes=[C.c_void_p,C.c_char_p,C.c_void_p]
        api=init(self.d.joint_core(),str(self.app_out).encode(),self.d.joint_marker())
        assert self.d.joint_bind(api)
    def lifecycle(self):
        row=(C.c_uint32*10)();self.d.joint_lifecycle(row)
        return dict(zip(['done','state','ft_cfg','core','ft_closes','core_backs','starts','stops','invalid_ft','loaded'],row))
    def pump(self):
        self.release_app()
        self.d.joint_pump()
        if self.cache_policy and self.d.joint_frames():
            assert self.cache_policy.contains('OGG.ELF'),'OGG evicted before Audio::Init'
            assert self.cache_policy.contains('HMM2.ELF'),'Game ELF evicted during OGG delivery'
        while (n:=self.d.joint_control(self.controls)):
            c=self.controls.raw[0];p=self.controls.raw[1:n]
            r=b''
            if c in (0x0f,0x69,0x6b,0x6c):
                self.boot_controls.append(dict(command=f'{c:02X}',reset=self.resets,
                    name=p[9:].rstrip(b'\0').decode() if c==0x69 else self.module_name))
            if c==0x0f:
                # Codex: ACK и сброс транспорта выполняет C++; кэш и сеансы ESP теряются.
                self.resets+=1;assert self.resets==1,'SYS_RESET loop'
                assert not self.started
                self.cached=False;self.cached_modules.clear();self.upload.clear();self.paths.clear()
                if self.cache_policy:self.cache_policy.d.cache_reset(1703936)
                continue
            elif c==0x6d:
                assert self.d.joint_stop();self.started=False;self.release_app();r=b'\1'
            elif c==0x2a:
                serial=struct.unpack('<I',p[:4])[0];name=p[4:].rstrip(b'\0');cluster=self.paths.get((name,serial),0)
                r=bytes([bool(cluster)])+struct.pack('<I',cluster)
            elif c==0x2b:
                serial,cluster=struct.unpack('<II',p[:8]);self.paths[(p[8:].rstrip(b'\0'),serial)]=cluster;r=b'\1'
            elif c==0x69:
                self.module_name=p[9:].rstrip(b'\0').decode();expected=self.ogg if self.module_name=='OGG.ELF' else self.elf
                size=struct.unpack('<I',p[1:5])[0];assert size==len(expected);self.module_stamp=struct.unpack('<I',p[5:9])[0]
                present=self.cache_policy.has(self.module_name,size,self.module_stamp) if self.cache_policy else self.module_name in self.cached_modules
                self.upload=bytearray(expected if present else b'')
                fit=((size+4095)//4096+1)*4096+8192<=1703936
                if self.cache_policy:fit=self.cache_policy.fits(size,'HMM2.ELF' if self.started else None)
                r=bytes([0 if present else 1 if fit else 2])+struct.pack('<I',1703936)
            elif c==0x6a:
                at=struct.unpack('<I',p[:4])[0];assert at==len(self.upload)
                self.upload+=p[4:];r=b'\1'+struct.pack('<I',len(self.upload))
            elif c==0x6b:
                assert bytes(self.upload)==(self.ogg if self.module_name=='OGG.ELF' else self.elf)
                if self.cache_policy:self.cache_policy.store(self.module_name,self.module_stamp,bytes(self.upload),'HMM2.ELF' if self.started else None)
                self.cached_modules.add(self.module_name);self.cached='HMM2.ELF' in self.cached_modules;r=b'\1'
            elif c==0x6c:
                assert p==b'HMM2.ELF\0';assert bytes(self.upload)==self.elf
                self.start_attempts+=1
                if self.start_attempts<=self.start_failures:r=bytes([self.start_status])
                else:
                    if self.cache_policy:self.cache_policy.touch('HMM2.ELF')
                    self.load_app();assert self.d.joint_start()==0;self.started=True;r=b'\0'
            else:raise AssertionError(('control',hex(c),p.hex()))
            assert self.d.joint_send(c|0x80,r,len(r))
    def state_word(self):
        self.d.joint_state(self.buffer);return self.buffer.raw[:4]
    def handle_request(self,req,state,put_available,put_received):
        if self.put_refuse and len(req)>5 and req[2]&0x10 and req[5]==0x6A:
            self.put_refuse-=1;self.put_refused+=1;put_available=False
        data=put_received or b''
        # Codex: только большие ответы файлов; загрузка ELF и DAT остаются штатными.
        if os.environ.get('HMM2_SAVES_NO_READY') and getattr(self,'file_read',False):put_available=False
        n=self.d.joint_spi(req,self.buffer,state,int(put_available),data,len(data))
        if not n:return None
        reply=self.buffer.raw[:n]
        self.d.joint_bulk(self.buffer);self.bulk_data=self.buffer.raw[:512]
        self.trace.write(json.dumps(dict(ms=time.monotonic(),tid=req[1],flags=req[2],tx=req[3],room=req[4],status=reply[2],rx=reply[3],accepted=reply[4],dma=len(data)))+'\n')
        return reply

class Machine(base.Machine):
    def __init__(self,disk,native,spg,out,latency):
        self.native=native;self.out=out;self.drop_after_start=False
        super().__init__(disk,native,'hmm2')
        # Codex: реальные блоки SPG, включая FAT32. Загруженные базовым стендом
        # .bin здесь заменяются; совпадение с ними проверяется только как контроль упаковки.
        assert spg[32:44]==b'SpectrumProg' and spg[0x35]==2
        at=1024;pages={}
        for desc in range(0x100,1024,3):
            flags,size,page=spg[desc:desc+3]
            assert flags&0x7f==0 and size&0xe0==0,'требуется несжатый блок с начала страницы'
            n=((size&31)+1)*512;pages[page]=spg[at:at+n].ljust(16384,b'\0');at+=n
            if flags&128:break
        assert at==len(spg) and {2,14,15}<=set(pages)
        # Codex: символы и SPG должны относиться к одной успешной сборке, включая отказ ассемблера.
        code=(base.ROOT/'build/hmm2_z80.bin').read_bytes()
        assert pages[2][:len(code)]==code,'SPG/code mismatch: rebuild launcher successfully'
        for page,addr in [(2,0x8000),(14,0x4000),(15,0)]:self.machine.set_memory_block(addr,pages[page])
        self.machine.set_memory_block(0x3900,struct.pack('<4H',base.INIT,base.READ,base.WRITE,base.SYNC))
        self.pc_start,self.sp_start=struct.unpack('<HH',spg[0x30:0x34])
        self.ports.windows[2]=0x8000;self.ports.windows[32]=0xc000
        self.link.latency=latency
        self.events=(out/'z80.jsonl').open('w',encoding='utf-8')
        self.hooks={self.symbols[n]:n for n in ['ServeRead','ServeFile','ServeGraphics','Proto_Send','FastOpen','FastOpen.ready']}
        # Codex: считаем реальные входы FAT32, чтобы проверять отсутствие FIND/FILEX на каждой порции.
        self.hooks.update({self.symbols[n]:n for n in ['FAT32_FIND','FAT32_READ','FAT32_APPEND','FAT32_FILEX','FAT32_SYNC','FAT32_CLOSE']})
        for pc in self.hooks:self.machine.set_breakpoint(pc)
        from fix4_z80 import install
        install(self,base)
        original_end=self.ft.end
        def finish_ft():
            t=bytes(self.ft.tx)
            if self.native.started and len(t)>3 and t[0]&0xc0==0x80:
                address=(t[0]&63)<<16|t[1]<<8|t[2];data=t[3:]
                if address<0x300000:
                    assert self.native.d.joint_ft_spi(address,data,len(data))==0
            original_end()
        self.ft.end=finish_ft
    def port_in(self,port):
        # Клод: HMM2_TEST_MOUSE — мышь всё время едет по диагонали: проверка задачи курсора, пока игра занята.
        if os.environ.get('HMM2_TEST_MOUSE') and port in (0xFADF,0xFBDF,0xFFDF):
            if port==0xFADF:return 0xFF
            return int((time.monotonic()*20))&0xFF
        # Codex: RTC стенда — 29.10.2026 14:00:00, BCD/24ч; FIFO PS/2 остаётся исходным.
        if port==0xBFF7 and self.gluk_reg<0xF0:
            return {0:0,2:0,4:0x14,7:0x29,8:0x10,9:0x26,10:0x20,11:2,13:0x80}.get(self.gluk_reg,0)
        return super().port_in(port)

    def attach_link(self,core=None):
        self.slave=SlaveHd();self.link=EspSide(self.slave,self.native);self.link.app_reads=False
        self.ports=SpiPorts(self.slave);self.ports.memory=self.memory
        self.slave.on_read=self._read_started;self.slave.on_written=self._written
    def _run_app(self):
        self.native.pump();self.link.publish_state()
    def run_joint(self,timeout,play=False,bad_seq=False,exit_mode=None):
        m=self.machine;m.pc=self.pc_start;m.sp=self.sp_start;start=time.monotonic();steps=0
        last_frames=-1;last_report=start
        keys=[(20,0x31),(40,0x1b),(85,0x5a),(160,0x5a),(190,0x5a),(220,0x5a)] if play else []
        if play and exit_mode:keys=keys[:3]
        release=None
        reset_at=None;quit_sent=False;quit_frame=0
        start_count=self.native.lifecycle()['starts']
        damaged=False
        model_cycles=0;metadata_cycles=0
        # Codex: ошибки относятся только к тестовому файлу; системные файлы модели не повреждаем.
        fs_probe=os.environ.get('HMM2_SAVES_PROBE');probe_write_fault=False;probe_sync_fault=False;probe_full=False
        while time.monotonic()-start<timeout:
            if reset_at is not None:
                # Codex: Z80 больше не обслуживает игру; poll ELF продолжает читать LINK сам.
                self.native.pump()
                life=self.native.lifecycle()
                if life['done'] and (not os.environ.get('HMM2_TEST_AUTOUNLOAD') or not life['loaded']):break
                time.sleep(.01);continue
            m.ticks_to_stop=100000;event=m.run();steps+=1
            model_cycles+=100000-int.from_bytes(m._StateBase__ticks_to_stop,'little')
            # Codex: 14 МГц, SPI28 DMA, 150 мкс ожидания сектора; замедляем до модели,
            # время Python никогда не вычитаем из наблюдаемого времени стенда.
            deadline=start+(model_cycles+metadata_cycles+self.dma_model_cycles+self.sd_stream_sectors*2100)/14000000
            remaining=deadline-time.monotonic()
            if remaining>0:time.sleep(min(remaining,0.05))
            if hasattr(self,'error'):raise self.error
            self.native.pump()
            if self.drop_after_start and self.native.started:
                self.link.lose_replies=1;self.drop_after_start=False
            frames=self.native.d.joint_frames()
            if exit_mode and not quit_sent and self.native.lifecycle()['starts']>start_count and frames>=getattr(self,'exit_target',245 if play else 20) and (not play or self.native.d.joint_world() or getattr(self,'reset_during_load',False)):
                quit_sent=True;quit_frame=frames;keys=[]
                if exit_mode=='reset':
                    reset_at=time.monotonic();m.pc=0
                    if getattr(self,'reset_before_poll',False):self.native.d.joint_pause_poll(1000)
                    self.native.d.joint_evo_reset()
                    self.events.write(json.dumps(dict(event='evo_reset_slot0',frame=frames))+'\n');self.events.flush();continue
                keys=[(frames,0x15),(frames+30,0x5a)]
            if release and time.monotonic()>=release[0]:
                self.ps2_queue+=bytes([0xf0,release[1]]);release=None
            if keys and frames>=keys[0][0] and not release:
                _,code=keys.pop(0);self.ps2_queue.append(code);release=(time.monotonic()+0.2,code)
                self.events.write(json.dumps(dict(event='ps2',code=code,frame=frames))+'\n');self.events.flush()
            if frames!=last_frames:
                last_frames=frames;self.events.write(json.dumps(dict(event='frame',n=frames,ms=round((time.monotonic()-start)*1000),pc=m.pc))+'\n');self.events.flush()
            if time.monotonic()-last_report>5:
                last_report=time.monotonic();print(dict(sec=round(last_report-start,1),frames=frames,pc=hex(m.pc),stage=self.memory[self.symbols['BootStage']],reads=self.reads,upload=len(self.native.upload)),flush=True)
            if event&2:
                if m.pc in self.hooks:
                    name=self.hooks[m.pc]
                    if name.startswith('FastOpen'):
                        # Codex: отдельно фиксируем такты построения индекса, без скорости Python.
                        self.events.write(json.dumps(dict(event=name,ms=round((time.monotonic()-start)*1000),
                            cpu_cycles=model_cycles,metadata_cycles=metadata_cycles,dma_cycles=self.dma_model_cycles,
                            data_bytes=int.from_bytes(self.memory[self.symbols['FastSize']:self.symbols['FastSize']+4],'little')))+'\n');self.events.flush()
                    if bad_seq and not damaged and name=='Proto_Send' and m.a==0x74:
                        self.memory[m.hl]^=0x40;damaged=True
                    if name=='ServeFile' and fs_probe:
                        req=bytes(self.memory[self.symbols['ProtoBuf']:self.symbols['ProtoBuf']+35])
                        target=req[3:16].split(b'\0')[0]==b'PROBE.SAV'
                        offset=int.from_bytes(req[29:33],'little')
                        probe_write_fault=target and req[2]==8 and fs_probe in ('write','last') and offset>=(7680 if fs_probe=='write' else 180*1024)
                        probe_sync_fault=target and req[2]==6 and fs_probe=='sync'
                        if target and req[2]==8 and fs_probe=='nospace' and not probe_full:
                            for cluster in range(3,self.disk.clusters+2):
                                if not self.disk.fat(cluster):self.disk.set_fat(cluster,0x0fffffff)
                            probe_full=True
                    if name=='ServeFile':
                        self.native.file_read=self.memory[self.symbols['ProtoBuf']+2]==7
                    elif name=='Proto_Send' and m.a==0x74:self.native.file_read=False
                    if name!='Proto_Send' or m.a in (0x71,0x74,0x76):
                        ptr=m.hl if name=='Proto_Send' else self.symbols['ProtoBuf']
                        self.events.write(json.dumps(dict(event=name,command=m.a,ms=round((time.monotonic()-start)*1000),cpu_cycles=model_cycles,metadata_cycles=metadata_cycles,dma_cycles=self.dma_model_cycles,data=bytes(self.memory[ptr:ptr+35]).hex()))+'\n');self.events.flush()
                    pc=m.pc;m.clear_breakpoint(pc);m.ticks_to_stop=1;m.run();m.set_breakpoint(pc);continue
                if m.pc==self.symbols['Halt']:break
                status=0
                if m.pc in (base.READ,base.WRITE):
                    lba=m.de<<16|m.hl;buf=m.bc
                    if m.pc==base.READ:
                        self.reads+=1;self.read_lbas.add(lba);m.set_memory_block(buf,self.disk.read(lba))
                        metadata_cycles+=55100
                    elif probe_write_fault:status=1
                    else:self.disk.write(lba,bytes(self.memory[buf:buf+512]));self.writes+=1
                elif m.pc not in (base.INIT,base.SYNC):continue
                if m.pc==base.SYNC and probe_sync_fault:status=1
                m.bc,m.de,m.hl=0xa55a,0x1234,0x5678;m.a,m.f=status,64 if not status else 1
                m.pc=int.from_bytes(self.memory[m.sp:m.sp+2],'little');m.sp+=2
            if self.native.d.joint_result()!=-999 and not bad_seq and not exit_mode:break
            if play and frames>=245 and self.native.d.joint_world() and not exit_mode:break
        stats=(C.c_uint32*269)();self.native.d.joint_stats(stats)
        return dict(frames=self.native.d.joint_frames(),world_ready=bool(self.native.d.joint_world()),exit=self.native.d.joint_result(),stage=self.memory[self.symbols['BootStage']],code=self.memory[self.symbols['BootError']],pc=hex(m.pc),sd_reads=self.reads,sd_cmd18=sum(1 for c,_ in self.sd_card.commands if c==18),sd_writes=self.writes,elapsed_s=time.monotonic()-start,steps=steps,z80_cycles=model_cycles,metadata_cycles=metadata_cycles,dma_cycles=self.dma_model_cycles,model='14MHz; SPI28 DMA; 150us/sector; native x64 engine; LINK 1330000 B/s',spi_stats=list(stats[:13]),commands={f'{i:02X}':stats[13+i] for i in range(256) if stats[13+i]},lifecycle=self.native.lifecycle(),exit_mode=exit_mode,exit_frame=quit_frame,reset_stop_s=None if reset_at is None else time.monotonic()-reset_at)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--name',default='baseline');ap.add_argument('--timeout',type=int,default=90);ap.add_argument('--latency',type=int,default=0);ap.add_argument('--period',type=float,default=1);ap.add_argument('--nested',action='store_true');ap.add_argument('--play',action='store_true');ap.add_argument('--spg',type=Path);ap.add_argument('--bad-seq',action='store_true');ap.add_argument('--lose-reply',action='store_true');ap.add_argument('--scene-map')
    # Codex: штатный старт, отказ стека один раз и два отказа с прежним экраном ошибки.
    ap.add_argument('--start-failures',type=int,choices=(0,1,2),default=0)
    ap.add_argument('--start-status',type=lambda s:int(s,0),choices=range(1,256),default=255)
    ap.add_argument('--reboot-ms',type=int,default=1500)
    a=ap.parse_args()
    if a.scene_map:os.environ['HMM2_TEST_MAP']='maps/'+a.scene_map
    if a.spg:base.ROOT=a.spg.parent
    out=RUNS/a.name;out.mkdir(parents=True,exist_ok=True)
    dep=base.ROOT/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    for n in ['fat32.bin','fat32-work.bin']:shutil.copy2(BASE/'launcher/build'/n,dep/n)
    shutil.copy2(base.ROOT/'build/hmm2_z80.bin',base.ROOT/'build/hmm2.bin')
    elf=(BASE/'module/HMM2.ELF').read_bytes();spg=(a.spg or base.ROOT/'_HMM2.SPG').read_bytes()
    cfg=(ROOT/'main/fheroes2.cfg').read_bytes()
    package=Path(os.environ.get('HMM2_JOINT_DAT',str(BASE/'data/HMM2.DAT')))
    # Клод: HMM2_JOINT_EXTRA — каталог с файлами с карты пользователя (сейв, HMM2.IDX, настройки): разбор его игры на стенде.
    extra={p.name.upper():p.read_bytes() for p in Path(os.environ['HMM2_JOINT_EXTRA']).iterdir()} if os.environ.get('HMM2_JOINT_EXTRA') else {}
    disk=fixture({'OGG.ELF':Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Sound/modules/out/OGG.ELF').read_bytes(),'HMM2.ELF':elf,'HMM2.DAT':package.read_bytes(),'FHEROES2.CFG':cfg,**extra},a.nested)
    native=Native(out,elf,a.period,a.start_failures,a.start_status,a.reboot_ms);m=Machine(disk,native,spg,out,a.latency)
    if a.lose_reply:m.drop_after_start=True
    report=m.run_joint(a.timeout,a.play,a.bad_seq)
    # Codex: сохраняем только файлы виртуальной SD для побайтного сравнения до/после.
    if os.environ.get('HMM2_TEST_SAVE') or os.environ.get('HMM2_SAVES_PROBE'):
        saved=out/'sd';saved.mkdir(exist_ok=True)
        for e in disk.entries(7 if a.nested else 2):
            if e['name'].upper().endswith(('.SAV','.IDX','.KEY','.CFG')):
                raw=e['raw'];first=struct.unpack_from('<H',raw,26)[0]|struct.unpack_from('<H',raw,20)[0]<<16
                size=struct.unpack_from('<I',raw,28)[0]
                data=b''.join(disk.cluster_data(c) for c in disk.chain(first))[:size] if size else b''
                (saved/e['name']).write_bytes(data)
    assert native.start_attempts==(2 if a.start_failures else 1),report
    assert native.resets==int(bool(a.start_failures)),report
    reboot=(C.c_uint32*4)()
    if hasattr(native.d,'joint_reboot_stats'):native.d.joint_reboot_stats(reboot)
    report['esp_reset']=dict(failures=a.start_failures,status=a.start_status,attempts=native.start_attempts,
        commands=native.resets,boot_ms=a.reboot_ms,reboots=reboot[0],sessions=reboot[1],
        requests_without_session=reboot[2],offline_polls=reboot[3],controls=native.boot_controls)
    if a.start_failures:
        assert reboot[0]==reboot[1]==1 and reboot[2]==0,report
        for generation in (0,1):
            assert any(e['command']=='69' and e['name']=='HMM2.ELF' and e['reset']==generation for e in native.boot_controls),report
        assert any(e['command']=='6B' and e['name']=='HMM2.ELF' and e['reset']==1 for e in native.boot_controls),report
    if a.start_failures==2:
        from test_stage1_launcher_failures import screen
        report['screen']=screen(m,out/'error.png')
        expected='ESP32: NOT ENOUGH MEMORY' if a.start_status==254 else 'ESP COULD NOT START HMM2'
        assert m.machine.pc==m.symbols['Halt'] and report['stage']==10 and report['code']==a.start_status,report
        assert not report['frames'] and any(expected in s for s in report['screen']),report
    elif os.environ.get('HMM2_SAVES_PROBE'):
        assert report['exit']==0 and 'SAVES PROBE COMPLETE' in (out/'esp.log').read_text(encoding='utf-8'),report
    elif not a.bad_seq:
        assert report['frames']>0 and report['stage']==12 and report['code']==0,report
    if a.bad_seq:
        # Codex: тот же декодер экранного DL, что в регрессиях загрузчика.
        from test_stage1_launcher_failures import screen
        report['screen']=screen(m,out/'error.png')
        # Codex: один потоковый E4 заменил прежние десять запросов по 1016 байт.
        assert m.machine.pc==m.symbols['Halt'] and any('LAST E5 OP 00 SEQ 0002' in s for s in report['screen'])
        assert any('FILE FHEROES2.CFG' in s for s in report['screen'])
    if a.lose_reply:assert report['spi_stats'][1]>0
    report.update(elf_sha256=hashlib.sha256(elf).hexdigest(),spg_sha256=hashlib.sha256(spg).hexdigest())
    report.update(native=native.build,poll_period_ms=a.period,spi_latency_reads=a.latency)
    report.update(data_path=str(package),data_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
        lba_index=dict(last_bank=int(m.memory[m.symbols['FastTablePage']]),music_banks=int(m.memory[m.symbols['MusicPages']]),
        clusters=int.from_bytes(bytes(m.memory[m.symbols['FastCount']:m.symbols['FastCount']+4]),'little')))

    if native.cache_policy:report['cache_policy']=dict(mode=os.environ['HMM2_JOINT_CACHE_POLICY'],events=native.cache_policy.events,ogg_present=native.cache_policy.contains('OGG.ELF'),game_present=native.cache_policy.contains('HMM2.ELF'))
    report['put_refused']=native.put_refused
    (out/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print({k:v for k,v in report.items() if k!='native'},flush=True)
    # Codex: DLL игры выгружается после остановки её задачи, до завершения процесса стенда.
    assert native.d.joint_stop();native.release_app()
    native.trace.close();m.events.close()
    # Codex: постоянные потоки модели ESP завершаются вместе с процессом.
    os._exit(0 if report['frames'] or a.bad_seq or a.start_failures==2 or os.environ.get('HMM2_SAVES_PROBE') else 1)
if __name__=='__main__':main()
