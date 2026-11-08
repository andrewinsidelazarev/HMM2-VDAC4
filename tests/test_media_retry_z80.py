"""Клод: машинный код Z80 MediaSend (launcher/media.asm): отказ ESP блоку DMA (PUT_BAD) не роняет чтение.

Сборка 70 на плате 2026-11-02: ESP один раз ответила на блок музыки «XOR differs or no room», MediaSend сразу
вернул CF, SPG показал E1 «SD RESPONSE TRANSFER FAILED» и остановил игру. Обычные пакеты (SpiLink_PutBlock)
при том же ответе блок повторяют. Здесь обмен со стороной ESP заменён ответами модели: управляющий запрос,
блок DMA, PUT_DONE. Проверяются итог MediaSend (CF), число посланных блоков и то, что повторы ограничены.
"""
from pathlib import Path
import json,os,struct
from test_stage1_joint import BASE,base
OUT=Path(os.environ.get('HMM2_MEDIA_RETRY_OUT','E:/zx/cache/tmp/hmm2_esp/media_retry_z80'))
sym=base.symbols(BASE/'launcher/build/hmm2.sym')
# Клод: флаги запроса и биты ответа — как в launcher/zifi_spi.asm (LINK_F_*, LINK_S_*), #10 — управляющий запрос MediaSend.
F_MEDIA,F_PUT_DONE=0x10,0x08
S_PUT_READY,S_PUT_OK,S_PUT_BAD=0x10,0x20,0x40

class Bench:
    def __init__(self,ready,done):
        # ready(i) — ответ на i-й управляющий запрос, done(i) — на i-й PUT_DONE.
        self.ready,self.done=ready,done;self.asks=self.blocks=self.dones=0
        self.m=m=base.z80.Z80Machine();m.set_memory_block(0x8000,(BASE/'launcher/build/hmm2_z80.bin').read_bytes())
        for name in ('SpiLink_Turn','SpiLink_BulkOpen','SpiLink_BulkRun','SpiLink_BulkDone','ZiFi_Flush'):m.set_breakpoint(sym[name])
        m.set_breakpoint(0x7fff);m.set_memory_block(sym['Chunk'],struct.pack('<H',469))
    def back(self,carry=False):
        m=self.m;m.pc=int.from_bytes(bytes(m.memory[m.sp:m.sp+2]),'little');m.sp+=2;m.f=(m.f&~1)|(1 if carry else 0)
    def answer(self,status):
        self.m.set_memory_block(sym['SpiRsp'],bytes([status]))
    def run(self):
        m=self.m;m.pc=sym['MediaSend'];m.sp=0xbff0;m.set_memory_block(m.sp,struct.pack('<H',0x7fff))
        for _ in range(100000):
            m.ticks_to_stop=2000000;m.run()
            if m.pc==0x7fff:return bool(m.f&1)
            if m.pc==sym['SpiLink_Turn']:
                if m.a==F_MEDIA:self.answer(self.ready(self.asks));self.asks+=1
                elif m.a==F_PUT_DONE:self.answer(self.done(self.dones));self.dones+=1
                else:raise AssertionError(('флаг запроса',hex(m.a)))
                self.back()
            elif m.pc==sym['SpiLink_BulkRun']:self.blocks+=1;self.back()
            elif m.pc in (sym['SpiLink_BulkOpen'],sym['SpiLink_BulkDone'],sym['ZiFi_Flush']):self.back()
            # Иначе кончился запас тактов (пауза ~1 мс между запросами): выполнение просто продолжается.
        raise AssertionError(('MediaSend не вернулся',hex(m.pc)))

def case(name,ready,done):
    b=Bench(ready,done);carry=b.run()
    return dict(case=name,carry=carry,asks=b.asks,blocks=b.blocks,dones=b.dones)

def main():
    results=[
        case('блок принят сразу',lambda i:S_PUT_READY,lambda i:S_PUT_OK),
        case('один отказ блоку',lambda i:S_PUT_READY,lambda i:S_PUT_BAD if i==0 else S_PUT_OK),
        case('ESP отказывает всегда',lambda i:S_PUT_READY,lambda i:S_PUT_BAD),
        case('места нет никогда',lambda i:0,lambda i:S_PUT_OK)]
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'result.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    for r in results:print(r)
    ok,once,never,full=results
    # Обычный путь не изменился: один запрос, один блок.
    assert not ok['carry'] and ok['blocks']==1 and ok['asks']==1,ok
    # Один отказ: блок уходит снова, MediaSend успешен.
    assert not once['carry'] and once['blocks']==2 and once['asks']==2,once
    # Отказ всегда: повторы ограничены тем же счётом, что и ожидание места, итог — CF.
    assert never['carry'] and 2<=never['blocks']<=100,never
    # Места нет: прежнее поведение — CF без единого блока.
    assert full['carry'] and full['blocks']==0 and full['asks']==100,full
    print('MEDIA RETRY OK')
if __name__=='__main__':main()
