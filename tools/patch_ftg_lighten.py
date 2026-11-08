"""Клод: кадры с преобразованиями 6–15 (осветление, «без цикла») — в атлас FT812, а не на CPU.

Упаковщик (pack_stage1_ft.py) писал такие точки в атлас прозрачными и ставил кадру флаг 2 «FT812 не рисует». ELF тогда рисовал
весь кадр процессором полосами по 16 строк, а для каждой полосы распаковывал с SD фон под ней (в бою — CBKG, zlib с начала
потока): кадры покоя чёрного дракона (44–53, IDLE1–IDLE4) стоили 120–150 КБ чтения SD и 240–290 мс на кадр (журнал платы,
сборка 87). Теперь точка с преобразованием t (6…15) получает свободный индекс палитры атласа FREE[t-6]; ELF задаёт этим
индексам цвет с альфой, подобранный по таблице transformTable оригинала (hmm_gpu.cpp, palette_address), флаг 2 снимается.

Правка на месте: меняются только ячейки атласа затронутых кадров и поле флагов их записей, размер и смещения прежние.
  --ftg FTG        — исходный HMM2.FTG (версия 3 или 4), из которого собран DAT;
  --gfx GFX        — HMM2.GFX той же сборки (версия 2 — несжатые кадры, версия 1 — zlib);
  --apply DAT --base N — применить к DAT, где hmm2.ftg начинается со смещения N: сначала сверяются ВСЕ прежние байты кусков,
                     при любом расхождении ничего не пишется; после записи байты перечитываются;
  --current packer|buggy — что сейчас лежит в DAT: атлас упаковщика или первая правка 04.11 (см. RULES).
"""
from pathlib import Path
import argparse,struct,zlib
REC=struct.Struct('<IIiiiiI');FT=struct.Struct('<IIHHHH');RECORD=100

def free_indices(mapping):
    # Те же свободные индексы считает ELF из таблицы перевода палитры (hmm_resource_palette): не цели перевода, кроме 0 и 2–5.
    free=sorted(set(range(256))-set(mapping)-{0,2,3,4,5});assert len(free)>=10,free;return free[:10]

# Клод: индекс атласа для точки цвета c с преобразованием t. buggy — первая правка 04.11: прозрачная точка (t=1) получала free[1-6],
# то есть free[-5] (отрицательный индекс Python) — индекс 246, голубоватое осветление: светлый прямоугольник вокруг кадра на плате.
RULES={
    'packer':lambda c,t,free:c if not t else t if 2<=t<=5 else 0,
    'buggy':lambda c,t,free:c if not t else t if 2<=t<=5 else free[t-6] if t<=15 else 0,
    'fixed':lambda c,t,free:c if not t else t if 2<=t<=5 else free[t-6] if 6<=t<=15 else 0,
}

def frames(gfx):
    magic,version,icn,til=struct.unpack_from('<4I',gfx);assert magic==0x4b503248 and version in (1,2)
    for g in range(icn+til*4):
        at,n=struct.unpack_from('<II',gfx,16+g*8)
        for i in range(n):
            o,size,w,h,x,y,single=REC.unpack_from(gfx,at+i*REC.size)
            if not w or not h or single:continue
            raw=gfx[o:o+size] if version==2 else zlib.decompress(gfx[o:o+size])
            yield g,i,w,h,raw

def build(ftg,gfx):
    """Затронутые места атласа: {смещение: {правило: байты}} — ячейки кадров и поля флагов."""
    magic,version,icn,til=struct.unpack_from('<4I',ftg);assert magic==0x35475448 and version in (3,4)
    mapping=ftg[16:272];free=free_indices(mapping);places={}
    for g,i,w,h,raw in frames(gfx):
        area=w*h;colors=raw[:area].translate(mapping);mask=raw[area:]
        if max(mask)<=5:continue
        at,n=struct.unpack_from('<II',ftg,272+g*8);record=at+i*RECORD
        fa,fsize,stride,cell_h,cell,flags=FT.unpack_from(ftg,record+REC.size)
        assert flags&2,(g,i,flags)
        def padded(p):return b''.join(p[y*w:(y+1)*w]+bytes(stride-w) for y in range(h))+bytes((cell_h-h)*stride)
        cells={name:padded(bytes(rule(c,t,free) for c,t in zip(colors,mask))) for name,rule in RULES.items()}
        base=fa+cell*stride*cell_h
        # Проверка источника: правило упаковщика даёт ровно то, что лежит в атласе.
        assert ftg[base:base+len(cells['packer'])]==cells['packer'],('атлас не совпал',g,i)
        places[base]=cells
        flag=struct.pack('<H',flags);clear=struct.pack('<H',flags&~2)
        places[record+REC.size+14]={'packer':flag,'buggy':clear,'fixed':clear}
    return free,places

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--ftg',type=Path,required=True);ap.add_argument('--gfx',type=Path,required=True)
    ap.add_argument('--apply',type=Path);ap.add_argument('--base',type=int,default=0)
    ap.add_argument('--current',choices=('packer','buggy'),default='packer');a=ap.parse_args()
    ftg=a.ftg.read_bytes();gfx=a.gfx.read_bytes();free,places=build(ftg,gfx)
    changed=sum(sum(x!=y for x,y in zip(v['packer'],v['fixed'])) for v in places.values())
    print('свободные индексы',free,'мест',len(places),'меняется байт против упаковщика',changed)
    if a.apply:
        # Клод: соседние места (просвет до 4 КиБ) — один кусок: по SMB каждая операция — круг по сети.
        spans=[]
        for at in sorted(places):
            end=at+len(places[at]['fixed'])
            if spans and at-spans[-1][1]<=4096:spans[-1][1]=max(spans[-1][1],end);spans[-1][2].append(at)
            else:spans.append([at,end,[at]])
        def image(start,end,ats,rule):
            b=bytearray(ftg[start:end])
            for at in ats:v=places[at][rule];b[at-start:at-start+len(v)]=v
            return bytes(b)
        with a.apply.open('r+b') as f:
            blocks=[]
            for start,end,ats in spans:
                f.seek(a.base+start);assert f.read(end-start)==image(start,end,ats,a.current),('на месте не то',start)
                blocks.append((start,image(start,end,ats,'fixed')))
            for start,data in blocks:f.seek(a.base+start);f.write(data)
            f.flush()
            for start,data in blocks:f.seek(a.base+start);assert f.read(len(data))==data,('не записалось',start)
        print('применено к',a.apply,'кусков',len(spans),'байт',sum(len(b[1]) for b in blocks))

if __name__=='__main__':main()
