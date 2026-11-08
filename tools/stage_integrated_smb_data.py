"""Codex: резервная копия установленного комплекта и проверенная временная выдача DAT; рабочие файлы не заменяются."""
from pathlib import Path
import hashlib,json,os,time
ROOT=Path(r'\\ZX-Evo\1\GAMES\VDAC4\HMM2 VDAC4')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/smb_before_v12_20261029')
SOURCE=Path('E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.DAT')
R=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio')
def opened(path,write=False,existing=False):
 flags=os.O_BINARY|os.O_RANDOM|(os.O_RDWR if write else os.O_RDONLY)
 if write and not existing:flags|=os.O_CREAT|os.O_EXCL
 return os.fdopen(os.open(str(path),flags),'r+b' if write else 'rb',buffering=0)
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def pull(remote,local,size):
 at=local.stat().st_size if local.exists() else 0;h=hashlib.sha256();t=time.monotonic();reported=at
 if at:
  with local.open("rb") as prior:
   while data:=prior.read(1048576):h.update(data)
 assert at<=size
 with local.open('ab') as target:
  while at<size:
   start=at
   for retry in range(8):
    try:
     with opened(remote) as f:
      f.seek(at)
      while at<size:
       data=f.read(min(65536,size-at));assert data,'short remote file';target.write(data);h.update(data);at+=len(data)
       if at-reported>=1024*1024:reported=at;print('backup',remote.name,at,'/',size,'seconds',round(time.monotonic()-t),flush=True)
     break
    except OSError as e:
     print('read retry',remote.name,at,type(e).__name__,flush=True)
     if retry==7:raise
   assert at>start
 return h.hexdigest()
def main():
 OUT.mkdir(exist_ok=True);rows=json.loads((OUT/'manifest.json').read_text()) if (OUT/'manifest.json').exists() else {}
 for name,size in [('HMM2.ELF',1543200),('_HMM2.SPG',50176),('FHEROES2.CFG',3404),('HMM2.IDX',8192),('H2S00000.SAV',105225),('HMM2.DAT',233303040)]:
  local=OUT/name
  if name in rows:
   assert local.stat().st_size==size and sha(local)==rows[name]['sha256'];continue
  if local.exists():assert local.stat().st_size<size
  digest=pull(ROOT/name,local,size);rows[name]=dict(bytes=size,sha256=digest)
  (OUT/'manifest.json').write_text(json.dumps(rows,indent=2),encoding='utf-8');print('backed up',name,size,digest,flush=True)
 assert rows['HMM2.DAT']['sha256']=='3bd678239c176159f36436ebd69ba174755de043deb65dde447b6adbed8c2835'
 remote=ROOT/'H2AV12.DAT';size=SOURCE.stat().st_size;at=0;t=time.monotonic();reported=0
 with opened(remote,write=True) as f:f.flush();os.fsync(f.fileno())
 with SOURCE.open('rb') as source:
  for retry in range(8):
   try:
    with opened(remote,write=True,existing=True) as target:
     source.seek(at);target.seek(at)
     while at<size:
      data=source.read(min(65536,size-at));sent=target.write(data);assert sent and sent<=len(data);at+=sent;source.seek(at)
      if at-reported>=1024*1024:reported=at;print('staged DAT',at,'/',size,'seconds',round(time.monotonic()-t),flush=True)
     target.flush();os.fsync(target.fileno())
    break
   except OSError as e:
    print('write retry',at,type(e).__name__,flush=True)
    if retry==7:raise
 assert at==size
 verify=R/'smb_staged_DAT_readback.bin';digest=pull(remote,verify,size);assert digest==sha(SOURCE)
 result=dict(staged=str(remote),bytes=size,sha256=digest,readback_verified=True,working_files_replaced=False,backup=str(OUT))
 (R/'smb_staged_data.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(result,flush=True)
if __name__=='__main__':main()
