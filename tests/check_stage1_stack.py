"""Codex: выпуск требует измерений стека того же ELF и воспроизведения отказа 32 КиБ."""
from pathlib import Path
import hashlib,json,re
ROOT=Path(__file__).resolve().parents[1];EV=ROOT/'Docs/evidence'
BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')

def read_log(path):
    data=path.read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8')

def main():
    digest=hashlib.sha256((BASE/'module/HMM2.ELF').read_bytes()).hexdigest()
    scenes={}
    for label in ['menu','kingdth','gatehell','battle','week','loaded','deep_town']:
        r=json.loads((EV/f'stage1_qemu_{label}.json').read_text(encoding='utf-8'))
        assert r['elf_sha256']==digest and not r['oom_retries']
        s=r['stack'];assert s['tasks']==1 and s['allocated_bytes']<=16384 and s['minimum_free_bytes']>=2048
        assert s['canary'] and s['watchpoint'] and 49000<=s['internal_before']<=49843 and s['largest_before']<=24576
        scenes[label]=dict(frames=r['frames'],minimum_psram=r['minimum_free'],**s)
    for label in ['week','loaded']:
        r=json.loads((EV/f'stage1_fix2_{label}_save.json').read_text())
        assert r['elf_sha256']==digest and r['world_bytes_exact'] and r['loaded_by_original_x64']
        if label=='week':assert r['day']>=8
        if label=='loaded':
            original=json.loads((EV/'stage1_target_save.json').read_text())
            assert r['world_sha256']==original['world_sha256'] and r['day']==original['day']
    static=json.loads((EV/'stage1_fix2_stack_static.json').read_text(encoding='utf-8'))
    assert static['elf_sha256']==digest and static['game_lto'] and static['runtime_translation_units']>=60
    tables=json.loads((EV/'stage1_fix2_tables.json').read_text());assert tables['all_fields_exact']
    rejected=read_log(EV/'stage1_fix2_reject32.log')
    assert 'free=49199 largest=24576' in rejected and 'task_start stack=32768 ok=0' in rejected and 'QEMU: start=-1' in rejected
    assert not re.search(r'кадр=\d',rejected)
    baseline=read_log(EV/'stage1_fix2_baseline_gatehell.log')
    free=min(int(m[0]) for m in re.findall(r'STACK=(\d+)/(32768)',baseline))
    result=dict(elf_sha256=digest,task_stack_bytes=16384,loaded_world_unchanged=True,minimum_stack_free=min(s['minimum_free_bytes'] for s in scenes.values()),
        maximum_stack_used=max(s['maximum_used_bytes'] for s in scenes.values()),scenes=scenes,
        baseline=dict(elf_sha256='d14dfa3963ac06f52b695986bb5a3561781859cc0c638ff4a19df7d949413373',
                      stack_bytes=32768,gatehell_used=32768-free,rejected_with_board_limits=True),
        dynamic_stack_records=[r for r in static['game']+static['runtime'] if r['kind']!='static'])
    (EV/'stage1_fix2_stack.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('scenes','dynamic_stack_records')},indent=2))
if __name__=='__main__':main()
