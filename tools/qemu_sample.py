"""Клод: выборочный профиль сцены QEMU по монитору: PC и адрес возврата (A0) ядра игры.

Трасса exec (tools/qemu_trace.py) покрывает около секунды работы платы: 2 ГБ журнала. Ход ИИ длится десятки секунд, поэтому
здесь монитор опрашивает регистры всю сцену. Начало и конец — строки журнала ESP. Итог: плоский профиль и пары «функция ← вызвавшая»,
число команд между метками (время QEMU -icount) — одно число для сравнения сборок.

Пример: python tools/qemu_sample.py load --start "AUDIO track=28 " --stop "AUDIO track=16 " --input-dir ... --out ...
"""
from pathlib import Path
import argparse,bisect,collections,json,os,re,socket,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
BASE=Path(os.environ.get('HMM2_STAGE1_BASE','E:/zx/cache/tmp/hmm2_esp/stage1'))/'qemu_stage1'
NM=Path('E:/zx/esp/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')
SCENES={'alteris':['Alteris.mp2'],'gatehell':['GateHell.mp2','--extended'],'town':['KINGDTH.MX2','--town'],'battle':['battle'],'load':['load'],'week':['sevenl.mp2','--week']}

class Monitor:
    def __init__(self,port):
        self.s=socket.create_connection(('127.0.0.1',port),timeout=20);self.buf=b'';self.read()
    def read(self):
        while b'(qemu)' not in self.buf:
            d=self.s.recv(65536)
            if not d:raise EOFError
            self.buf+=d
        i=self.buf.index(b'(qemu)');t=self.buf[:i];self.buf=self.buf[i+6:];return t
    def command(self,text):
        self.s.sendall(text.encode()+b'\n');return self.read().decode(errors='replace')

def symbols(elf,delta):
    out=subprocess.run([str(NM),'-C','-S','--defined-only',str(elf)],capture_output=True,text=True,errors='replace').stdout
    rows=[]
    for line in out.splitlines():
        p=line.split(' ',3)
        if len(p)==4 and p[2] in 'tTwW':
            try:rows.append((int(p[0],16)+delta,int(p[1],16),p[3]))
            except ValueError:pass
    rows.sort();return rows

def main():
    ap=argparse.ArgumentParser();ap.add_argument('scene',choices=list(SCENES));ap.add_argument('--start',required=True);ap.add_argument('--stop',default='')
    ap.add_argument('--out',required=True);ap.add_argument('--input-dir',default='');ap.add_argument('--frames',type=int);ap.add_argument('--port',type=int,default=45466)
    ap.add_argument('--interval',type=float,default=.002);ap.add_argument('--limit',type=float,default=1500);ap.add_argument('--cpu',type=int,default=1)
    ap.add_argument('--finish-after',type=int,default=0)
    a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,str(ROOT/'tests/prepare_qemu_scene.py'),*SCENES[a.scene]],cwd=ROOT,check=True)
    if a.frames:(BASE/'ui_test.bin').write_bytes(struct.pack('<I',a.frames))
    if a.finish_after:(BASE/'ui_test.bin').write_bytes(struct.pack('<2I',a.frames or 0,a.finish_after))
    if a.input_dir:
        for name in ['ui_keys.bin','ui_times.bin','ui_mouse.bin','ui_clicks.bin']:
            p=Path(a.input_dir)/name
            if p.exists():(BASE/name).write_bytes(p.read_bytes())
    (BASE/'qemu.log').unlink(missing_ok=True)
    env=dict(os.environ,HMM2_QEMU_MONITOR=str(a.port))
    log=(out/'run.log').open('wb')
    p=subprocess.Popen([sys.executable,str(ROOT/'tools/run_qemu_stage1.py'),'--icount','--timeout','3400'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    qlog=BASE/'qemu.log';start=a.start.encode();stop=a.stop.encode();mon=None;t0=None;samples=[];ended=False;seek=0;began=time.monotonic()
    rx=re.compile(r'\bPC=([0-9a-fA-F]{8})');ra=re.compile(r'\bA00=([0-9a-fA-F]{8})')
    try:
        while p.poll() is None and time.monotonic()-began<a.limit:
            if mon is None:
                try:mon=Monitor(a.port);mon.command(f'cpu {a.cpu}')
                except OSError:mon=None;time.sleep(.2);continue
            if not qlog.exists():time.sleep(.2);continue
            data=qlog.read_bytes()
            if t0 is None:
                at=data.find(start,seek)
                if at<0:time.sleep(.1);continue
                t0=time.monotonic();seek=at+len(start);print('start',flush=True)
            elif stop and data.find(stop,seek)>=0:
                ended=True;break
            for _ in range(40):
                text=mon.command('info registers')
                m=rx.search(text);n=ra.search(text)
                if m:samples.append((int(m.group(1),16),int(n.group(1),16) if n else 0))
                time.sleep(a.interval)
    finally:
        if mon:
            try:mon.command('quit')
            except (EOFError,OSError):pass
        try:p.wait(timeout=30)
        except subprocess.TimeoutExpired:p.kill()
        log.close()
    text=qlog.read_text(encoding='utf-8',errors='replace') if qlog.exists() else ''
    (out/'qemu.log').write_text(text,encoding='utf-8')
    loaded=int(re.findall(r'elf->entry=0x([0-9a-f]+)',text)[0],16);elf=(BASE/'main/HMM2.ELF').read_bytes();entry=struct.unpack_from('<I',elf,24)[0]
    rows=symbols(Path(os.environ.get('HMM2_QEMU_SYMBOLS',str(BASE.parent/'module/HMM2.raw.ELF'))),loaded-entry)+symbols(BASE/'build/hmm2_runtime_qemu.elf',0);rows.sort();starts=[r[0] for r in rows]
    def name(pc):
        pc=(pc&0x3fffffff)|0x40000000 if pc>>30==1 else pc
        i=bisect.bisect_right(starts,pc)-1
        if i>=0 and pc<rows[i][0]+max(rows[i][1],1):return rows[i][2]
        return '<0x%08x>'%(pc&~0xfff)
    flat=collections.Counter();pairs=collections.Counter()
    for pc,ra_ in samples:
        n=name(pc);flat[n]+=1;pairs[(n,name((ra_&0x3fffffff)|0x40000000))]+=1
    total=max(len(samples),1)
    (out/'profile.json').write_text(json.dumps(dict(samples=len(samples),ended=ended,flat=flat.most_common(),pairs=[dict(function=k[0],caller=k[1],samples=v) for k,v in pairs.most_common()]),indent=2),encoding='utf-8')
    print('выборок',len(samples),'конец по метке' if ended else 'конец по времени/сцене')
    print('время сцены QEMU:',' '.join(re.findall(r'HMM2 PERF frame=\d+ N=100 wall=\d+',text)[-3:]))
    print('\nПлоско:')
    for n,c in flat.most_common(45):print('%6d %5.1f%%  %s'%(c,100*c/total,n[:100]))
    print('\nФункция <- вызвавшая:')
    for (n,c2),c in pairs.most_common(40):print('%6d %5.1f%%  %s  <-  %s'%(c,100*c/total,n[:50],c2[:50]))

if __name__=='__main__':raise SystemExit(main())
