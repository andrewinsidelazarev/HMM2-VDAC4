"""Codex: полнота оригинальных ресурсов CampaignData и неизменность файлов пакета."""
from pathlib import Path
import hashlib, json, os, re, struct, sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from update_campaign_dat import catalog,region_digest
stage=Path(os.environ['HMM2_STAGE1_BASE']);base=stage.parent
dat=stage/'data/HMM2_campaign.DAT'
_,_,records=catalog(dat);entries={e['path']:e for e in records}
source=root/'third_party/openhmm2/src/fheroes2'
maps=sorted(set(re.findall(r'"([^"\n]+\.(?:H2C|HXC))"',(source/'campaign/campaign_data.cpp').read_text(encoding='utf-8'))))
assert len(maps)==47
original=Path('E:/zx/HMM2/Assets/Original');checks=[]
with dat.open('rb') as stream:
    for name in maps:
        entry=entries['maps/'+name.lower()]
        with (original/'MAPS'/name).open('rb') as f:want=hashlib.file_digest(f,'sha256').hexdigest()
        assert region_digest(stream,entry['offset'],entry['bytes'])==want
        checks.append(dict(file=name,bytes=entry['bytes'],sha256=want))
    agg=entries['data/heroes2x.agg'];stream.seek(agg['offset']);raw=stream.read(agg['bytes'])
    count=struct.unpack_from('<H',raw)[0]
    names=[raw[-count*15+i*15:][:15].split(b'\0')[0].decode('ascii') for i in range(count)]
    assert 'X_LOADCM.ICN' in names and 'X_IVY.ICN' in names
videos=json.loads((base/'campaign_videos.json').read_text())
for name in videos['missing']:assert not (original/'ANIM'/name.upper()).exists()
result=dict(required_campaign_maps=len(maps),verified=checks,expansion_graphics=['X_LOADCM.ICN','X_IVY.ICN'],
    clips_smk=sum(n.endswith('.smk') for n in videos['included']),clips_ogg=sum(n.endswith('.ogg') for n in videos['included']),
    absent_in_original=videos['missing'])
(base/'resource_audit.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('47 карт, HEROES2X и список роликов проверены')
