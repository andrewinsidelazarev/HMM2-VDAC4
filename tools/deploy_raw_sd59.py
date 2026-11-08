"""Codex: запись только трёх файлов тестовой сборки на заданную пользователем карту."""
from pathlib import Path
import ctypes,datetime,hashlib,json,os,shutil
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio')
SOURCE=ROOT/'dist/HMM2_build59_raw_sd_test';CARD=Path('G:/games/VDAC4/HMM2 VDAC4')
NAMES=['HMM2.DAT','HMM2.ELF','_HMM2.SPG']
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def volume():
 serial=ctypes.c_ulong();assert ctypes.windll.kernel32.GetVolumeInformationW('G:\\',None,0,ctypes.byref(serial),None,None,None,0),ctypes.get_last_error()
 return serial.value
def copy_checked(src,dst,label):
 digest=hashlib.sha256();total=0;mark=0
 with src.open('rb') as a,dst.open('wb') as b:
  while data:=a.read(8*1024*1024):
   b.write(data);digest.update(data);total+=len(data)
   if total-mark>=64*1024*1024:print(label,total,'bytes',flush=True);mark=total
  b.flush();os.fsync(b.fileno())
 shutil.copystat(src,dst);return dict(bytes=total,sha256=digest.hexdigest())
def main():
 assert CARD.is_dir() and str(CARD.resolve()).lower()=='g:\\games\\vdac4\\hmm2 vdac4'
 identity=volume();manifest=read(SOURCE/'manifest.json')
 assert manifest['build']==59 and all((CARD/n).is_file() for n in NAMES)
 assert shutil.disk_usage(CARD).free>sum((SOURCE/n).stat().st_size for n in NAMES)+1048576
 stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');backup=BASE/('card_before59_'+stamp);backup.mkdir()
 previous={};untouched={};activated=[];staged={}
 receipt=dict(build=59,destination=str(CARD),volume_serial=identity,backup=str(backup),started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),delivered=False)
 try:
  print('BACKUP',str(backup),flush=True)
  for n in NAMES:
   previous[n]=copy_checked(CARD/n,backup/n,'backup '+n);assert sha(backup/n)==previous[n]['sha256']
  for p in CARD.iterdir():
   if p.is_file() and p.name not in NAMES:untouched[p.name]=sha(p)
  (backup/'before.json').write_text(json.dumps(dict(files=previous,untouched=untouched,volume_serial=identity),ensure_ascii=False,indent=2),encoding='utf-8')
  for n,temp in [('HMM2.DAT','H2R59.DAT'),('HMM2.ELF','H2R59.ELF'),('_HMM2.SPG','H2R59.SPG')]:
   assert volume()==identity
   if previous[n]['sha256']==manifest['files'][n]['sha256']:continue
   stage=CARD/temp;assert not stage.exists(),str(stage)
   got=copy_checked(SOURCE/n,stage,'stage '+n);assert got==manifest['files'][n]
   print('VERIFY STAGED',n,flush=True);assert sha(stage)==got['sha256'];staged[n]=stage
  for n in ['_HMM2.SPG','HMM2.ELF','HMM2.DAT']:
   assert volume()==identity
   if n in staged:os.replace(staged[n],CARD/n);activated.append(n)
  active={}
  for n in NAMES:
   print('VERIFY ACTIVE',n,flush=True);active[n]=dict(bytes=(CARD/n).stat().st_size,sha256=sha(CARD/n));assert active[n]==manifest['files'][n]
  for n,digest in untouched.items():assert sha(CARD/n)==digest,('user file changed',n)
  assert volume()==identity
  receipt.update(delivered=True,completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=active,previous_files=previous,preserved_files=untouched)
  (SOURCE/'card_delivery.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
  manifest.update(delivered=True,delivery_receipt='card_delivery.json');(SOURCE/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
  (backup/'delivery.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8');print('CARD DELIVERY VERIFIED',str(SOURCE/'card_delivery.json'),flush=True)
 except Exception as error:
  receipt['error']=repr(error);receipt['activated']=activated
  (backup/'delivery_failed.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
  print('DELIVERY FAILED; originals saved in',str(backup),flush=True);raise
if __name__=='__main__':main()
