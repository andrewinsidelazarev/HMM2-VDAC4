"""Codex: отклик R9 по реально показанному DL/RAM_G и тактам кадров EVR."""
from pathlib import Path
import hashlib,json,sys

HERE=Path('E:/zx/HMM2/Diagnostics/claude/ramg_audit')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/vdac2')
sys.path.insert(0,str(HERE))
from replay import replay,chip_state
from evr import records
from dl import decode

def signature(raw,g,background=False):
    data=decode(raw);assert not data['errors'],data['errors']
    images=[b for b in data['bitmaps'] if b['source'] not in (0xe8000,0xe9100,0xffc00)]
    if background:images=images[:2]
    result=[]
    for b in images:
        geom={k:v for k,v in b.items() if k not in ('word','source','start','end','palette')}
        pal=bytes(g[b['palette']:b['palette']+512]) if b['fmt']==15 else b''
        result.append((geom,hashlib.sha256(g[b['start']:b['end']]).hexdigest(),hashlib.sha256(pal).hexdigest()))
    return json.dumps(result,sort_keys=True)

def main():
    rows=[]
    for scene in sys.argv[1:] or ['transition','load','town']:
        path=OUT/(scene+'.evr');times={};initial=None
        for kind,clk,v in records(path):
            if kind==6:initial=chip_state(v[0]);start=clk;st,g0=v[0],v[1]
            elif kind==3:times[v[0]]=clk
        # Codex: начало EVR совпадает с нажатием; между ними процессор эмулятора остановлен.
        scenario=json.loads((OUT/(scene+'_scenario.json')).read_text('utf-8'))
        click_clock=scenario['click_clock'];assert click_clock==start
        bg=scene in ('startup','load','town')
        after=(OUT/(scene+'_after.state')).read_bytes()
        target=signature((OUT/(scene+'_after.DL'+str(after[112])+'.bin')).read_bytes(),(OUT/(scene+'_after.RAM_G.bin')).read_bytes(),bg)
        before=signature(g0['DL'+str(st[112])],g0['RAM_G'])
        response=[];complete=[];changes=[];last=None
        def visible(frame,raw,g):
            nonlocal last
            sig=signature(raw,g)
            if times[frame]>click_clock and sig!=before:response.append(frame)
            if times[frame]>click_clock and signature(raw,g,bg)==target:complete.append(frame)
            if sig!=last:changes.append(frame);last=sig
        bus=replay(path,visible)
        assert response and complete,(scene,response,complete)
        hz=initial['frequency']
        row=dict(scene=scene,evr_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),click_clock=click_clock,
                 first_response_frame=response[0],complete_frame=complete[0],first_response_ms=(times[response[0]]-click_clock)*1000/hz,
                 complete_ms=(times[complete[0]]-click_clock)*1000/hz,display_changes=changes,spi=bus,
                 completion_scope='Первые два фоновых bitmap и их палитры совпали с конечным снимком' if bg else 'Все bitmap, кроме курсора, совпали с конечным снимком')
        rows.append(row);print({k:v for k,v in row.items() if k not in ('spi','display_changes')},flush=True)
    (OUT/'response_timing.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':main()
