"""Codex: считаем обращения потокового MP2 по настоящим цепочкам addons; скорость платы не моделируем."""
from pathlib import Path
import collections,json,struct
ROOT=Path(__file__).resolve().parents[1]
def u16(data,at):return struct.unpack_from('<H',data,at)[0]
def u32(data,at):return struct.unpack_from('<I',data,at)[0]
def main():
    result=[]
    for path in sorted(Path('E:/zx/HMM2/Assets/Original/MAPS').iterdir()):
        if path.suffix.lower() not in ['.mp2','.mx2']:continue
        data=path.read_bytes();w,h=struct.unpack_from('<II',data,420);count=w*h
        addons=428+count*20;addon_count=u32(data,addons);start=addons+4
        accesses=[];visits=0
        for i in range(count):
            at=428+i*20;accesses.append((at,20));index=u16(data,at+10);seen=set()
            while index:
                assert index<addon_count and index not in seen;seen.add(index)
                address=start+index*15;accesses.append((address,15));visits+=1;index=u16(data,address)
        caches={}
        for capacity in [4096,8192,16384,32768,65536]:
            lru=collections.OrderedDict();misses=wire=queries=0
            for at,size in accesses:
                for page in range(at//4096,(at+size-1)//4096+1):
                    if page in lru:lru.move_to_end(page);continue
                    if len(lru)==capacity//4096:lru.popitem(last=False)
                    lru[page]=None;misses+=1;n=min(4096,len(data)-page*4096);wire+=n;queries+=(n+1015)//1016
            caches[str(capacity)]=dict(misses=misses,read_bytes=wire,packets1016=queries)
        result.append(dict(map=path.name,tiles=count,addons_declared=addon_count,addon_visits=visits,logical_read_bytes=count*20+visits*15,additional_seeks=visits*2,caches=caches))
    assert len(result)==97
    (ROOT/'Docs/evidence/map_io_model.json').write_text(json.dumps(dict(scope='Модель файлового LRU для чередования tiles/addons. Не замер SD и не полная трасса загрузки: заголовки/объекты/метаданные вне этой фазы исключены.',maps=result),ensure_ascii=False,indent=2),encoding='utf-8')
    print(next(r for r in result if r['map']=='GateHell.mp2'))
if __name__=='__main__':main()
