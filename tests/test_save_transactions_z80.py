"""Replay successful transactions captured from the real C layer through Z80/FAT32."""
from pathlib import Path
import os,sys,shutil,struct,json,hashlib
b=Path(__file__).resolve().parents[1];stage=Path(os.environ.get('HMM2_STAGE1_BASE',str(b/'stage')));os.environ['HMM2_STAGE1_BASE']=str(stage)
sys.path.insert(0,str(b/'tests'))
from test_stage1_joint import base,fixture
from fix4_z80 import patched_machine
out=stage/'save_z80';out.mkdir(parents=True,exist_ok=True)
target=stage/'launcher'
assert (target/'build/hmm2_z80.bin').exists(),'Build the Z80 launcher in HMM2_STAGE1_BASE/launcher first'
dep=target/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
for name in ['fat32.bin','fat32-work.bin']:shutil.copyfile(target/'build'/name,dep/name)
shutil.copyfile(target/'build/hmm2_z80.bin',target/'build/hmm2.bin')
cases=[]
for line in (stage/'build138_units/protocol.txt').read_text().splitlines():
    parts=line.split()
    if parts[0]=='CASE':cases.append(dict(ops=[],files={}))
    elif parts[0]=='FILE':cases[-1]['files'][parts[1]]=bytes.fromhex(parts[3]) if len(parts)>3 else b''
    else:
        _,op,name,dest,at,n,status,size,*data=parts
        cases[-1]['ops'].append(dict(op=int(op),name=name,dest='' if dest=='-' else dest,at=int(at),n=int(n),status=int(status),size=int(size),data=bytes.fromhex(data[0]) if data else b''))
rows=[]
for nested in [False,True]:
    for number,case in enumerate(cases):
        index=bytearray(8192);index[:8]=b'old.sav\0';index[128:138]=b'other.sav\0'
        model={'HMM2.ELF':base.ELF,'HMM2.DAT':bytes(8192),'OGG.ELF':bytes(512),'HMM2.IDX':bytes(index),'H2S00000.SAV':bytes([0x51])*128,'H2S00001.SAV':bytes([0x72])*211}
        disk=fixture(model,nested)
        directory=7 if nested else 2
        plan=[]
        for r in case['ops']:
            op=r['op'];expect=('missing',None) if r['status']==0x19 else ('stat',len(model[r['name']])) if op==0 else ('ok',None)
            assert r['status'] in [0,0x19]
            # Legacy E5 uses 960-byte packets; the same server also serves the
            # unchanged production stream transport (op7/op8, tested separately).
            if op in [1,3]:
                for offset in range(0,r['n'],960):
                    data=r['data'][offset:offset+960]
                    plan.append((0xe5,base.fs(op,r['name'],offset=r['at']+offset,data=data if op==3 else b'',length=len(data)),('data',data) if op==1 else expect))
            else:plan.append((0xe5,base.fs(op,r['name'],r['dest'],offset=r['at']),expect))
            if not r['status']:
                if op==2:model[r['name']]=b''
                if op==3:
                    raw=bytearray(model[r['name']]);raw[r['at']:r['at']+r['n']]=r['data'];model[r['name']]=bytes(raw)
                if op==4:model[r['dest']]=model.pop(r['name'])
                if op==5:del model[r['name']]
        request_count=len(plan)
        class App(base.Esp):
            def __init__(self):super().__init__(cached=True,paths={});self.plan=plan
            def __call__(self,c,p):
                if c==0x6c:self.started=True;return base.frame(0xec,b'\0')
                if c==0x72:return base.frame(0xf2,b'\1')
                if c==0x70:return base.frame(0xf0,b'\1')+self.next_request()
                return super().__call__(c,p)
        app=App();m=patched_machine(base)(disk,app,'hmm2');m.ports.windows[2],m.ports.windows[32]=0x8000,0xc000;m.run()
        assert app.done and len(app.answers)==request_count,(app.done,len(app.answers),request_count)
        for name,expected in case['files'].items():
            entry=next(e['raw'] for e in disk.entries(directory) if e['name'].upper()==name)
            first=struct.unpack_from('<H',entry,26)[0]|struct.unpack_from('<H',entry,20)[0]<<16
            size=struct.unpack_from('<I',entry,28)[0]
            actual=b''.join(disk.cluster_data(c) for c in disk.chain(first))[:size]
            assert actual==expected,(nested,number,name,len(actual),len(expected))
        names={e['name'].upper() for e in disk.entries(directory)}
        assert not any(name.endswith(('.IBK','.ITM')) or name.startswith(('H2T','H2B')) for name in names)
        rows.append(dict(nested=nested,case='new' if number else 'overwrite',logical_operations=len(case['ops']),z80_requests=request_count,all_files_exact=True))
        print(rows[-1],flush=True)
(out/'result.json').write_text(json.dumps(dict(pass_all=True,spg_sha256=hashlib.sha256((target/'_HMM2.SPG').read_bytes()).hexdigest(),cases=rows),indent=2))
