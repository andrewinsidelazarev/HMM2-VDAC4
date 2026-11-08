"""Codex: одинаковый ввод F→L, задержка до второго кадра и полное окно PERF со снами отдельно."""
from pathlib import Path
import argparse,hashlib,json,re

def measure(folder):
    scenario=json.loads((folder/'scenario.json').read_text(encoding='utf-8'))
    text=(folder/'qemu.log').read_text(encoding='utf-8')
    origin=int(re.search(r'input ready after first frame at=(\d+)',text)[1])
    rows=[];last=None;pending=[];key=None
    for line in text.splitlines():
        m=re.search(r'PS2=([0-9a-f]+)',line)
        if m:
            if key:rows.append(dict(key=key,before=before,after=pending))
            key=m[1];before=last;pending=[]
        m=re.search(r'кадр=(\d+) T=(\d+) SD=(\d+)/(\d+)',line)
        if m:
            last=list(map(int,m.groups()))
            if key and len(pending)<2:pending.append(last)
    if key:rows.append(dict(key=key,before=before,after=pending))
    assert [r['key'] for r in rows[-2:]]==['2b','4b']
    opening=[]
    for r,requested in zip(rows[-2:],scenario['key_times'][-2:]):
        after=r['after'][1]
        opening.append(dict(key=r['key'],second_frame=after[0],delay_ms=after[1]-origin-requested,
                            sd_bytes=after[3]-r['before'][3]))
    metrics=json.loads((folder/'metrics.json').read_text(encoding='utf-8'))
    first,last=[origin+t for t in scenario['key_times'][-2:]]
    # Codex: это окно содержит также фоновые кадры. Не называем его чистой ценой открытия.
    windows=[p for p in metrics['PERF'] if p['time']-p['wall']<=first and p['time']>=last]
    assert len(windows)==1,windows
    p=windows[0]
    return dict(elf_sha256=hashlib.sha256((folder/'HMM2.ELF').read_bytes()).hexdigest(),keys=opening,
                opening_delay_ms=sum(r['delay_ms'] for r in opening),opening_sd_bytes=sum(r['sd_bytes'] for r in opening),
                perf_window=dict(frame=p['frame'],frames=p['N'],work_ms=p['wall']-p['sleep'],sd_bytes=p['SD_B']))

def main():
    ap=argparse.ArgumentParser();ap.add_argument('before',type=Path);ap.add_argument('after',type=Path);a=ap.parse_args()
    controls=['initial.cfg','ui_keys.bin','ui_times.bin','ui_clicks.bin','ui_click_hold.bin','ui_test.bin',
              'psram_budget.bin','sd_latency.bin','internal_limit.bin','data_path.bin','main.c']
    for name in controls:assert (a.before/name).read_bytes()==(a.after/name).read_bytes(),name
    before,after=measure(a.before),measure(a.after)
    result=dict(before=before,after=after,identical_input=True,
                delay_delta_ms=after['opening_delay_ms']-before['opening_delay_ms'],
                perf_work_delta_ms=after['perf_window']['work_ms']-before['perf_window']['work_ms'])
    result['passed']=bool(after['opening_delay_ms']<=2800 and after['perf_window']['sd_bytes']<=1600000
                          and result['perf_work_delta_ms']<=0)
    (a.after/'opening_comparison.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2));assert result['passed'],result

if __name__=='__main__':main()
