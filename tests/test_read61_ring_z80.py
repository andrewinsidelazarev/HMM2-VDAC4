"""Клод: машинный код Z80: кольцо строками 4 КиБ, E6 мимо кольца и музыка; SD заменён моделью сектора.

Кластеры 1, 8 и 64 сектора, фрагментированная таблица LBA. Проверяются байты ответа, число секторов SD,
страница после вызова, метка слота при отказе SD и то, что кэш музыки и чтение вперёд берут слот целиком.
"""
from pathlib import Path
import json,os,random,struct
from test_stage1_joint import BASE,base
OUT=Path(os.environ.get('HMM2_READ61_OUT','E:/zx/cache/tmp/hmm2_esp/integration_audio/read61_z80'))
SIZE=5*1024*1024
data=bytes((i*37+(i>>9)*13+(i>>17))&255 for i in range(SIZE))
sym=base.symbols(BASE/'launcher/build/hmm2.sym')

class Bench:
    def __init__(self,spc):
        self.spc=spc;self.pages={};self.cur=32;self.calls=[];self.fail=0
        self.m=m=base.z80.Z80Machine();m.set_memory_block(0x8000,(BASE/'launcher/build/hmm2_z80.bin').read_bytes())
        sectors=(SIZE+511)//512;count=(sectors+spc-1)//spc
        # Фрагментированная таблица: кластеры идут через один и с разрывами страниц.
        self.lba={}
        for c in range(count):
            at=0x10000+c*spc*2+(c//97)*spc*5
            page=64+c//4096;self.pages.setdefault(page,bytearray(16384))
            struct.pack_into('<I',self.pages[page],(c%4096)*4,at)
            for s in range(spc):self.lba[at+s]=c*spc+s
        self.put('ScanSectorsPerCluster',spc,1);self.put('FastTablePage',64+(count-1)//4096,1);self.put('FastEnabled',1,1)
        self.put('FastSize',SIZE);self.put('FastSectors',sectors);self.put('WC_PAGE3',32,1)
        m.set_memory_block(sym['FastTags'],b'\xff'*512)
        m.set_output_callback(self.out);m.set_breakpoint(0x7fff);m.set_breakpoint(sym['FastSDReadMany'])
        self.call('FastMusicOpen')
    def put(self,k,v,n=4):self.m.set_memory_block(sym[k],int(v).to_bytes(n,'little'))
    def get(self,k,n=4):return int.from_bytes(bytes(self.m.memory[sym[k]:sym[k]+n]),'little')
    def out(self,port,value):
        assert port==0x13af,hex(port)
        if value==self.cur:return
        self.pages[self.cur]=bytearray(self.m.memory[0xc000:0x10000])
        self.cur=value;self.m.set_memory_block(0xc000,bytes(self.pages.get(value,bytes(16384))))
    def call(self,name):
        m=self.m;m.pc=sym[name];m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0x7fff))
        for _ in range(4000):
            m.ticks_to_stop=200000;m.run()
            if m.pc==0x7fff:return
            if m.pc==sym['FastSDReadMany']:
                count,at,dest=m.a,m.de<<16|m.hl,m.bc
                assert 1<=count<=32 and 0xc000<=dest and dest+count*512<=0x10000,(count,dest)
                self.calls.append((self.cur,count))
                status=0
                if self.fail:self.fail-=1;status=0xe1
                else:
                    blob=bytearray()
                    for s in range(count):
                        logical=self.lba[at+s];blob+=data[logical*512:(logical+1)*512].ljust(512,b'\0')
                    m.set_memory_block(dest,bytes(blob))
                m.a=status;m.f=0x40 if not status else 0
                m.pc=int.from_bytes(bytes(m.memory[m.sp:m.sp+2]),'little');m.sp+=2
        raise AssertionError(('unbounded',name,hex(m.pc)))
    def read(self,at,n):
        self.calls.clear()
        fx=sym['Fx'];m=self.m
        m.set_memory_block(fx+4,struct.pack('<I',at));m.set_memory_block(fx+8,struct.pack('<HH',0x1000,n))
        self.call('FastReadAt')
        assert m.a==0,(at,n,m.a)
        got=bytes(m.memory[0x1000:0x1000+n]);assert got==data[at:at+n],(self.spc,at,n)
        assert self.cur==32,self.cur
        return sum(c for _,c in self.calls)

def run(spc):
    b=Bench(spc)
    # Вразброс: только строка; повтор и другая строка слота без лишнего SD.
    assert [b.read(0x40000,4096),b.read(0x40000,4096),b.read(0x42000,4096),b.read(0x42000,100),b.read(0x40010,100)]==[8,0,8,0,0]
    assert b.get('FastAheadLeft',1)==8,'вразброс подгрузка ближняя'
    # Вытеснение: другой адрес того же слота через полное число слотов данных.
    slots=b.get('FastDataSlots',1);assert slots>128
    far=0x40000+slots*16384
    assert [b.read(far,4096),b.read(0x42000,4096),b.read(0x40000,4096)]==[8,8,8]
    # Подряд: дочитывает слот до конца, подгрузка длинная и идёт по одной строке за проход.
    assert b.read(0x100000,4096)==8 and b.read(0x101000,4096)==24 and b.get('FastAheadLeft',1)==16
    b.calls.clear();b.call('FastIdle');assert sum(c for _,c in b.calls)==0 and b.cur==32,'строка уже в слоте'
    b.calls.clear()
    for _ in range(3):b.call('FastIdle')
    # 0x103000 уже в слоте, дальше — две новые строки следующего слота.
    assert sum(c for _,c in b.calls)==16 and b.get('FastAheadLeft',1)==12
    assert b.read(0x104000,4096)==0,'подгрузка уже принесла строку'
    # Данные вне кольца 128..255: слоты сверх 128 живут в свободных банках.
    extra=next(n for n in range(slots) if n>=128)*16384
    assert b.read(extra,4096)==8 and b.get('FastDataPage',1)<128 and b.read(extra,100)==0
    # Большой кусок: до конца слота одним заходом.
    assert b.read(0x180000,8192)==32
    # Подгрузка вразброс: следующая строка уже в кольце к следующему запросу.
    assert b.read(0x1c0000,4096)==8
    b.calls.clear();b.call('FastIdle');assert sum(c for _,c in b.calls)==8 and b.read(0x1c1000,4096)==0
    # Отказ SD: метка снята, данные не выдаются за старые; следующее чтение снова идёт на SD.
    b.fail=1;fx=sym['Fx'];b.m.set_memory_block(fx+4,struct.pack('<I',0x2a0000));b.m.set_memory_block(fx+8,struct.pack('<HH',0x1000,100));b.call('FastReadAt')
    assert b.m.a==0xe1 and b.cur==32,(b.m.a,b.cur)
    # Повтор той же строки считается чтением подряд, поэтому слот дочитывается целиком.
    assert b.read(0x2a0000,100)==32
    # Случайные чтения любого размера и начала.
    rng=random.Random(spc)
    for _ in range(300):
        at=rng.randrange(0,SIZE-16384);b.read(at,rng.choice([1,100,1016,1017,4096,4097,7680,8192]))
    assert b.read(SIZE-100,100)>=0
    # Музыка: своя область страниц и меток; строка вразброс, кэш простоя — слот целиком.
    origin=0x3a0000;b.m.set_memory_block(sym['ProtoBuf'],struct.pack('<II',origin,0x40000));b.put('ProtoRxLen',8,2);b.call('ServeMusic')
    assert b.get('MusicActive',1)==1
    n=b.read(origin+0x8000,4096);assert n==8 and b.get('FastDataPage',1) not in range(128,256),n
    assert b.read(origin+0x8000,100)==0
    b.put('MusicCursor',origin//512);b.put('MusicDemand',origin//512)
    b.calls.clear();b.call('FastIdle');assert sum(c for _,c in b.calls)==32 and b.get('MusicCursor')==origin//512+32
    b.put('MusicCursor',(origin+0x8000)//512);b.calls.clear();b.call('FastIdle')
    assert sum(c for _,c in b.calls)==32,'частичный слот музыки дочитан'
    assert b.read(origin+0x8000+8192,4096)==0 and b.read(origin,4096)==0
    return dict(sectors_per_cluster=spc,ok=True)

def main():
    result=[run(spc) for spc in (1,8,64)]
    OUT.mkdir(exist_ok=True);(OUT/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result)
if __name__=='__main__':main()
