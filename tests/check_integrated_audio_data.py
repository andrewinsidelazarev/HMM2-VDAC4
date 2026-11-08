"""Codex: сверка передаваемых OGG/SFX с исходными ресурсами, включая адреса каталога DAT."""
from pathlib import Path
import hashlib,json,struct
R=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio');B=Path('E:/zx/cache/tmp/hmm2_esp/stage1/data');S=Path('E:/zx/HMM2/Assets/Original')
def digest(f,n):
 h=hashlib.sha256()
 while n:
  b=f.read(min(n,1048576));assert b;n-=len(b);h.update(b)
 return h.hexdigest()
def main():
 m=json.loads((B/'manifest.json').read_text(encoding='utf-8'));songs=aggs=0
 with (B/'HMM2.DAT').open('rb') as f:
  header=f.read(16);magic,version,count,_=struct.unpack('<4I',header);assert magic==0x31465348 and version==1
  raw=f.read(count*72);entries={}
  for i in range(count):
   row=raw[i*72:(i+1)*72];name=row[:64].split(b'\0',1)[0].decode('ascii');assert name not in entries
   entries[name]=struct.unpack_from('<II',row,64)
  assert count==len(m['files'])
  for e in m['files']:assert entries[e['path']]==(e['offset'],e['bytes'])
  for e in m['files']:
   if not e['path'].startswith(('music/','data/')) or 'source' not in e:continue
   source=Path(e['source']);f.seek(e['offset']);actual=digest(f,e['bytes'])
   with source.open('rb') as src:expected=digest(src,e['bytes']);assert not src.read(1)
   assert actual==expected,(e['path'],actual,expected)
   if e['path'].startswith('music/'):songs+=1
   else:aggs+=1
  expected_sounds={}
  for source in sorted((S/'DATA').glob('*.AGG')):
   blob=source.read_bytes();n=struct.unpack_from('<H',blob)[0];parent=entries['data/'+source.name.lower()][0]
   for i in range(n):
    name=blob[len(blob)-15*n+15*i:len(blob)-15*n+15*(i+1)].split(b'\0',1)[0].decode('ascii').lower()
    if name.endswith('.82m'):
     _,at,size=struct.unpack_from('<III',blob,2+12*i);assert at+size<=len(blob)
     expected_sounds['audio/sfx/'+name]=(parent+at,size)
  got={k:v for k,v in entries.items() if k.startswith('audio/sfx/')};assert got==expected_sounds
  assert songs==42 and {f'music/track{i:02}.ogg' for i in range(2,44)}.issubset(entries)
  proof=dict(entries=count,tracks=songs,aggs=aggs,sfx_aliases=len(got),original_audio_bytes_exact=True,catalogue_matches_manifest=True,expansion_sound_override=True)
 (R/'audio_data_check.json').write_text(json.dumps(proof,indent=2),encoding='utf-8');print(proof)
if __name__=='__main__':main()
