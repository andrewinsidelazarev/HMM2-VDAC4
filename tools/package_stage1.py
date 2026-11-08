"""Codex: выдаём только тот ELF, который прошёл стенд; пакет остаётся в рабочем каталоге."""
from pathlib import Path
import hashlib,json,shutil,struct,re
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1');OUT=ROOT/'dist/HMM2';EV=ROOT/'Docs/evidence'
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    elf=BASE/'module/HMM2.ELF';digest=sha(elf);datahash=sha(BASE/'data/HMM2.DAT')
    # Codex: исправление 4 нельзя выдать по старым доказательствам исправления 3.
    fix4=json.loads((EV/'stage1_fix4_gate.json').read_text(encoding='utf-8'))
    assert fix4['elf_sha256']==digest and fix4['spg_sha256']==sha(BASE/'launcher/_HMM2.SPG') and fix4['data_sha256']==datahash
    assert fix4['exact_ft'] and fix4['cursor'] and fix4['resolution']==[1024,768]
    assert fix4['complete_draw_interception'],'Codex: CPU-покрытие поддерживаемых FT812 операций ещё не устранено'
    assert fix4['performance_pass'],'Codex: требуемая скорость во всех сценах ещё не подтверждена'
    assert fix4['menu_link_bytes_max']<=10240 and fix4['sd_kib_s']>=300
    assert all(fix4['vdac2_comparison'][s]['pass'] for s in ['menu','transitions','scroll','hero','battle','town','load'])
    joint=json.loads((EV/'stage1_fix4_joint.json').read_text(encoding='utf-8'))
    assert joint['elf_sha256']==digest and joint['spg_sha256']==sha(BASE/'launcher/_HMM2.SPG') and joint['data_sha256']==datahash
    assert joint['menu_and_map'] and len(joint['runs'])==2
    for r in joint['runs']:
        assert r['world_ready'] and r['exact_ft_frames']>=245 and r['menu_rgb_exact']==20 and r['no_visible_overwrites']
        for name,d in r['native']['sources'].items():assert sha(Path(name))==d,name
    assert elf.stat().st_size<=1572864
    stack=json.loads((EV/'stage1_fix2_stack.json').read_text(encoding='utf-8'))
    assert stack['elf_sha256']==digest and stack['task_stack_bytes']<=16384 and stack['minimum_stack_free']>=2048
    assert len(stack['scenes'])==7 and stack['baseline']['rejected_with_board_limits']
    for name in ['packing','puff']:
        proof=json.loads((EV/f'stage1_fix1_{name}.json').read_text(encoding='utf-8'))
        assert proof['elf_sha256']==digest,name
    delivery=json.loads((EV/'stage1_fix1_delivery.json').read_text(encoding='utf-8'))
    assert delivery['elf_sha256']==digest and delivery['free_after']>=0 and delivery['firmware_elf_validation']
    assert ((elf.stat().st_size+4095)//4096+1)*4096+8192<=1703936
    launcher=json.loads((EV/'stage1_fix1_launcher.json').read_text(encoding='utf-8'))
    assert launcher['elf_sha256']==digest and launcher['spg_sha256']==sha(BASE/'launcher/_HMM2.SPG') and len(launcher['cases'])>=12
    for scene in ['menu','kingdth','gatehell','battle']:
        r=json.loads((EV/f'stage1_qemu_{scene}.json').read_text(encoding='utf-8'))
        assert r['elf_sha256']==digest and r['data_sha256']==datahash and r['minimum_free']>0 and not r['oom_retries'],scene
    menu=json.loads((EV/'stage1_qemu_menu_compare.json').read_text());assert menu['exact']==menu['total']==30
    assert menu['elf_sha256']==digest and menu['resolution']==[1024,768]
    frames=json.loads((EV/'stage1_comparison.json').read_text())['packed32_vs_original64'];assert frames['same_index_frames']==frames['base_frames']==370
    maps=json.loads((EV/'stage1_maps.json').read_text());assert maps['exact_maps']==maps['exact_region_graphs']==97
    saves=json.loads((EV/'stage1_saves.json').read_text());assert saves['streamed_to_original'] and saves['original_to_streamed']
    target_save=json.loads((EV/'stage1_target_save.json').read_text());assert target_save['loaded_by_original_x64'] and target_save['world_bytes_exact']
    check=(BASE/'module/check.log').read_text();assert '34 external symbols, all given by the core' in check
    OUT.mkdir(parents=True,exist_ok=True)
    for source,name in [(elf,'HMM2.ELF'),(BASE/'launcher/_HMM2.SPG','_HMM2.SPG'),(BASE/'data/HMM2.DAT','HMM2.DAT'),(ROOT/'main/fheroes2.cfg','fheroes2.cfg'),(ROOT/'Docs/STAGE1_README.md','README.md'),(ROOT/'LICENSE','LICENSE'),(ROOT/'THIRD_PARTY.md','THIRD_PARTY.md')]:shutil.copy2(source,OUT/name)
    (OUT/'licenses').mkdir(exist_ok=True)
    for source in (ROOT/'licenses').glob('*'):shutil.copy2(source,OUT/'licenses'/source.name)
    files={str(p.relative_to(OUT)).replace('\\','/'):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(OUT.rglob('*')) if p.is_file() and p.name not in ['manifest.json','SHA256SUMS.txt']}
    (OUT/'SHA256SUMS.txt').write_text(''.join(v['sha256']+'  '+n+'\n' for n,v in files.items()),encoding='ascii')
    raw=elf.read_bytes();header=struct.unpack_from('<16sHHIIIIIHHHHHH',raw);assert header[2]==94
    offset,size,count,names=header[6],header[11],header[12],header[13]
    sections=[struct.unpack_from('<10I',raw,offset+i*size) for i in range(count)];string=sections[names];strings=raw[string[4]:string[4]+string[5]]
    sizes={strings[s[0]:].split(b'\0',1)[0].decode():s[5] for s in sections if s[0]}
    result=dict(files=files,elf_sections=sizes,resident_sections_bytes=sum(sizes.get(n,0) for n in ['.text','.rodata','.data','.data.rel.ro','.bss']),external_symbols=34,missing_symbols=0,psram_budget=7864320)
    result['cache']=dict(capacity=delivery['capacity'],elf_footprint=delivery['elf_footprint'],index_footprint=delivery['index_footprint'],free_after=delivery['free_after'])
    result['stack']=dict(task_bytes=stack['task_stack_bytes'],maximum_used=stack['maximum_stack_used'],minimum_free=stack['minimum_stack_free'],tasks=1)
    (OUT/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    shutil.copy2(OUT/'manifest.json',EV/'stage1_package.json');shutil.copy2(BASE/'module/check.log',EV/'stage1_symbols.log');shutil.copy2(BASE/'module/size.txt',EV/'stage1_sections.txt')
    sources={}
    for folder in ['main','tools','tests','launcher','prototype','bench']:
        for p in (ROOT/folder).rglob('*'):
            if p.is_file() and p.suffix in ['.c','.cpp','.h','.inc','.asm','.ld','.py','.ps1','.cfg']:sources[str(p.relative_to(ROOT)).replace('\\','/')]=sha(p)
    (EV/'stage1_sources.json').write_text(json.dumps(sources,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Пакет:',OUT,'ELF:',elf.stat().st_size,'резидентные секции:',result['resident_sections_bytes'],'SHA256:',digest)
if __name__=='__main__':main()
