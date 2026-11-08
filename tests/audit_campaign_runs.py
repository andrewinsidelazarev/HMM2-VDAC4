"""Codex: фактические результаты QEMU, диапазоны прочитанных карт и структурная проверка трасс FT."""
from pathlib import Path
import hashlib,json,re,struct,sys,os
root=Path(__file__).resolve().parents[1];base=Path(os.environ['HMM2_STAGE1_BASE']).resolve().parent
sys.path.insert(0,str(root/'tools'));sys.path.insert(0,str(root/'tests'))
from update_campaign_dat import catalog, digest
from ft_command_fifo import CommandFifo
from check_speed_save import decode_save
expected_elf=digest(base/'update/HMM2.ELF')
names={'roland_final':'maps/campg01.h2c','pol_complete':'maps/camp1_01.hxc','alteris':'maps/alteris.mp2',
       'gatehell_7m':'maps/gatehell.mp2','battle':None,'load_previous':None,'load_campaign':None,'roland_rebuilt':'maps/campg01.h2c','roland_no_video':'maps/campg01.h2c'}
results=[]
for name,map_name in names.items():
    folder=base/'runs'/name
    if not (folder/'scene.json').exists():continue
    scene=json.loads((folder/'scene.json').read_text());log=(folder/'qemu.log').read_text(encoding='utf-8',errors='replace')
    if scene['returncode']!=0:continue
    assert scene['elf_sha256']==expected_elf
    bad=re.findall(r'^.*(?:Exception |failure|abort\(\)|аварийное завершение|Guru Meditation|invalid catalogue|short package read|layer skipped).*$',log,re.M)
    assert not bad,(name,bad)
    dat=Path(scene['arguments']['dat']);dat=dat if dat.is_absolute() else base/dat
    assert digest(dat)==scene['dat_sha256']
    with dat.open('rb') as stream:entry_count=struct.unpack_from('<I',stream.read(16),8)[0]
    raw=(folder/'menu.ft').read_bytes();at=swaps=max_dl=0;ram=bytearray(0x300000);dl=bytearray(8192);fifo=CommandFifo()
    while at<len(raw):
        address,count=struct.unpack_from('<II',raw,at);at+=8;data=raw[at:at+4*count];at+=len(data)
        assert len(data)==4*count
        if address==0xffffffff:continue
        address&=0x7fffffff
        if address<0x300000:
            assert address+len(data)<=len(ram);ram[address:address+len(data)]=data
        elif address<0x302000:
            pos=address-0x300000;assert pos+len(data)<=len(dl);dl[pos:pos+len(data)]=data
        elif address==0x302054:
            words=list(struct.unpack('<2048I',dl));assert 0 in words
            max_dl=max(max_dl,words.index(0)+1);swaps+=1
        else:fifo.write(address,data,ram,dl)
    recorded=int(re.search(r'захват кадров завершён, (\d+) кадров',log)[1]);assert swaps==recorded
    row=dict(name=name,frames=swaps,elf_sha256=expected_elf,dat_sha256=scene['dat_sha256'],catalog_entries=entry_count,
             max_dl_words=max_dl,decoded_video_frames=fifo.video_frames,
             psram_budget=scene['psram'],psram_min=min(map(int,re.findall(r'минимум=(\d+)',log))),
             stack_free_min=min(map(int,re.findall(r'STACK=(\d+)/',log))),runtime_errors=bad,
             allocator_probes=len(re.findall(r'QEMU: OOM',log)))
    pcm=re.findall(r'gaps=(\d+) max_gap_samples=(\d+)',log)
    if pcm:row['pcm']={'gaps':int(pcm[-1][0]),'max_gap_samples':int(pcm[-1][1])}
    filex=re.search(r'FILEX read=(\d+) write=(\d+)',log)
    row['filex']={'read':int(filex[1]),'write':int(filex[2])}
    if map_name:
        _,_,entries=catalog(dat);entry=next(e for e in entries if e['path']==map_name)
        spans=[]
        for line in (folder/'reads.log').read_text().splitlines():
            time,kind,start,size=line.split();start=int(start);end=start+int(size)
            a=max(start,entry['offset']);b=min(end,entry['offset']+entry['bytes'])
            if a<b:spans.append((a,b))
        end=entry['offset'];covered=0
        for a,b in sorted(spans):
            if b>end:covered+=b-max(a,end);end=b
        row['map']={'path':map_name,'bytes':entry['bytes'],'bytes_read':covered}
    saves=[]
    for path in (folder/'save').glob('H2S*.SAV'):
        _,meta=decode_save(path);saves.append(dict(file=path.name,**meta))
    row['saves']=saves;results.append(row)
    print(name,'frames',swaps,'max_DL',max_dl,'video',fifo.video_frames,'save_count',len(saves),flush=True)
(base/'run_verification.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
