"""Codex: проверка отказа на чужом DAT, прерывания, повторного применения и точного отката."""
from pathlib import Path
import contextlib, importlib.util, io, json, os, struct
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('update_campaign_dat',root/'tools/update_campaign_dat.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
out=Path(os.environ['HMM2_STAGE1_BASE']).parent/'updater_tests';out.mkdir(exist_ok=True)
maps=out/'maps';maps.mkdir(exist_ok=True)
for i in range(47):(maps/f'camp{i:02}.h2c').write_bytes(bytes([i])*37)
checks=[]
for count in (1,453):
    first=4096 if count==1 else 32768
    old=struct.pack('<4I',tool.MAGIC,1,count,0)+b''.join(f'data/a{i}'.encode().ljust(64,b'\0')+struct.pack('<II',first,1024) for i in range(count))
    old=old.ljust(first,b'\0')+b'0123456789abcdef'*64
    dat=out/f'fixture{count}.dat';dat.write_bytes(old)
    bundle=out/f'bundle{count}'
    with contextlib.redirect_stdout(io.StringIO()):tool.prepare(dat,maps,None,bundle)
    report,before,after=tool.load_bundle(bundle)
    tail=(bundle/'campaign.append').read_bytes()
    assert report['version']==(1 if count==1 else 3)
    for cut in (0,1,len(tail)//2,len(tail)):
        dat.write_bytes(old+tail[:cut])
        with contextlib.redirect_stdout(io.StringIO()):tool.apply(dat,bundle)
        final=dat.read_bytes();assert tool.digest(dat)==report['target_sha256']
        stamp=dat.stat().st_mtime_ns
        with contextlib.redirect_stdout(io.StringIO()):tool.apply(dat,bundle)
        assert dat.stat().st_mtime_ns==stamp
        with contextlib.redirect_stdout(io.StringIO()):tool.apply(dat,bundle,rollback=True)
        assert dat.read_bytes()==old
        checks.append(dict(count=count,cut=cut,resume=True,idempotent=True,rollback=True))
    dat.write_bytes(after[:8]+before[8:]+old[len(before):]+tail)
    with contextlib.redirect_stdout(io.StringIO()):tool.apply(dat,bundle)
    assert tool.digest(dat)==report['target_sha256']
    for corrupt in (b'wrong',old[:-1]+b'X',old+tail[:20]+b'X'):
        dat.write_bytes(corrupt)
        try:
            with contextlib.redirect_stdout(io.StringIO()):tool.apply(dat,bundle)
        except ValueError:pass
        else:raise AssertionError('Изменённый пакет принят')
        assert dat.read_bytes()==corrupt
    dat.write_bytes(old)
    saved=(bundle/'campaign.append').read_bytes()
    (bundle/'campaign.append').write_bytes(saved+b'X')
    try:
        tool.apply(dat,bundle)
    except ValueError:pass
    else:raise AssertionError('Повреждённое обновление принято')
    assert dat.read_bytes()==old
    (bundle/'campaign.append').write_bytes(saved)
(out/'result.json').write_text(json.dumps(dict(cases=checks,partial_header=True,invalid_base_refused=True,invalid_bundle_refused=True),indent=2),encoding='utf-8')
print('Проверено:',len(checks),'сценариев прерывания/отката, FS1 и FS3, отказ без записи на чужих данных')
