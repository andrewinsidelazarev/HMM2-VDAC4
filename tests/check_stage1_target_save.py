"""Codex: сохранение, записанное настоящим Xtensa ELF, загружается исходным x64 кодом."""
from pathlib import Path
import struct,zlib,json,hashlib,subprocess,sys,os,shutil,zipfile,re
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/stage1')
def main():
    label=sys.argv[1] if len(sys.argv)>1 else 'gatehell'
    assert label in ('gatehell','week','loaded')
    folder=BASE/('runs/target_save64' if label=='gatehell' else 'runs/target_'+label+'64');(folder/'states').mkdir(parents=True,exist_ok=True)
    index=(BASE/'qemu_stage1/run/HMM2.IDX').read_bytes()
    wanted=b'quicksave.sav' if label=='week' else b'quicksave.savh'
    slot=next(i for i in range(64) if index[i*128:(i+1)*128].split(b'\0',1)[0].lower()==wanted)
    source=BASE/f'qemu_stage1/run/H2S{slot:05}.SAV';save=folder/'GateHell.savh';shutil.copy2(source,save);data=save.read_bytes()
    candidates=[]
    for at in range(min(2048,len(data)-12)):
        rawsize,zipsize,version,unused=struct.unpack_from('>IIHH',data,at)
        if at+12+zipsize==len(data) and not version and not unused:candidates.append((at,rawsize,zipsize))
    assert len(candidates)==1;c,rawsize,zipsize=candidates[0];raw=zlib.decompress(data[c+12:]);assert len(raw)==rawsize and raw[-2:]==b'\xff\x03'
    env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',HMM2_TEST_MAP='maps/GateHell.mp2',HMM2_STATE_OUT=str(folder/'states'),HMM2_TEST_LOAD_EXTERNAL=str(save))
    with (folder/'run.log').open('wb') as f:r=subprocess.run([str(BASE/'host64/hmm2_host.exe'),str(folder),'1000'],env=env,cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=180)
    assert not r.returncode and 'TARGET SAVE LOAD COMPLETE' in (folder/'run.log').read_text(encoding='utf-8')
    world=(folder/'states/target_world.be').read_bytes();assert raw.startswith(world)
    result=dict(save_bytes=len(data),save_sha256=hashlib.sha256(data).hexdigest(),raw_bytes=rawsize,world_bytes=len(world),world_sha256=hashlib.sha256(world).hexdigest(),loaded_by_original_x64=True,world_bytes_exact=True)
    result['day']=int(re.search(r'TARGET SAVE DAY=(\d+)',(folder/'run.log').read_text(encoding='utf-8'))[1])
    result['elf_sha256']=hashlib.sha256((BASE/'qemu_stage1/main/HMM2.ELF').read_bytes()).hexdigest()
    assert result['elf_sha256']==hashlib.sha256((BASE/'module/HMM2.ELF').read_bytes()).hexdigest()
    if label in ('week','loaded'):
        log=(BASE/'qemu_stage1/qemu.log').read_text(encoding='utf-8')
        counts=re.search(r'QEMU: FILEX read=(\d+) write=(\d+)',log)
        assert counts and int(counts[2])>=len(data)
        if label=='loaded':assert int(counts[1])>=len(data)
        result['filex']=dict(read_bytes=int(counts[1]),write_bytes=int(counts[2]))
    if label=='week':assert result['day']>=8,result
    if label=='loaded':
        original=json.loads((ROOT/'Docs/evidence/stage1_target_save.json').read_text())
        assert result['world_sha256']==original['world_sha256'] and result['day']==original['day']
        result['world_unchanged_after_target_reload']=True
    prefix='stage1_target_save' if label=='gatehell' else 'stage1_fix2_'+label+'_save'
    (ROOT/('Docs/evidence/'+prefix+'.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
    with zipfile.ZipFile(ROOT/('Docs/evidence/'+prefix+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for p in [save,folder/'states/target_world.be',folder/'run.log']:z.write(p,p.name)
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
