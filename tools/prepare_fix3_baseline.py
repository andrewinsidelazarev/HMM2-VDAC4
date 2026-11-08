"""Codex: символы старого SPG для повторяемого сравнения до/после.

Исходники откатываются только во временной копии. Результат побайтно сверяется
с сохранённым SPG исправления 2; патч не подменяет исполняемый бинарник.
"""
from pathlib import Path
import shutil,subprocess,hashlib,json,zipfile
from build_stage1 import ROOT,BASE
from prepare_sd_launcher import REF
OUT=BASE/'joint/before';SRC=OUT/'source'
EXPECTED='9d380bd6a7bce7a12f3801fbd445b487f9fb9772b4ff16092c794d51d229b3f0'

def main():
    SRC.mkdir(parents=True,exist_ok=True);(OUT/'build').mkdir(exist_ok=True)
    old=OUT/'_HMM2.SPG'
    if not old.exists():
        archive=ROOT/'Docs/evidence/stage1_fix3_joint.zip'
        if archive.exists():
            with zipfile.ZipFile(archive) as z:old.write_bytes(z.read('before/_HMM2.SPG'))
        else:shutil.copy2(ROOT/'dist/HMM2/_HMM2.SPG',old)
    assert hashlib.sha256(old.read_bytes()).hexdigest()==EXPECTED
    for p in (ROOT/'launcher').glob('*'):
        if p.is_file():shutil.copy2(p,SRC/p.name)
    p=SRC/'hmm2.asm';s=p.read_text(encoding='utf-8')
    s=s.replace('        cp #F5\n        call z,KeysAcknowledged\n','').replace('        xor a\n        ld (StatusPending),a\n        ld a,(ProtoBuf)','        ld a,(ProtoBuf)')
    a=s.index('        ; Codex: максимум один');b=s.index('        ld hl,KeyBuf',a);s=s[:a]+s[b:]
    s=s.replace('        ld a,#75\n        call Proto_Send\n        jp c,Fail','        ld a,#75\n        call Proto_Send')
    a=s.index('        ld a,(StatusPending)');b=s.index('        ld a,#73',a);s=s[:a]+s[b:]
    a=s.index('\nKeysAcknowledged:');b=s.index('\n\n\nPs2Open:',a);s=s[:a]+s[b:]
    s=s.replace('        ld a,#E4\n        call RememberRequest\n','').replace('        jp c,FileReplyFailed\n','')
    a=s.index('\nFileReplyFailed:');b=s.index('\nReadAt:',a);s=s[:a]+s[b:]
    s=s.replace('KeysPending: db 0\nStatusPending: db 0\n','');p.write_text(s,encoding='utf-8')
    p=SRC/'proto.asm';s=p.read_text(encoding='utf-8');a=s.index('        ; Codex: F5 может');b=s.index('        cp RESP_ERROR',a);s=s[:a]+s[b:];p.write_text(s,encoding='utf-8')
    p=SRC/'files.asm';s=p.read_text(encoding='utf-8').replace('        ld a,#E5\n        call RememberRequest\n','').replace('        jp c,FileReplyFailed\n','');p.write_text(s,encoding='utf-8')
    p=SRC/'diagnostic.asm';s=p.read_text(encoding='utf-8');a=s.index('        ld a,(LastRequestValid)');b=s.index('        ld hl,MsgReset',a);s=s[:a]+s[b:];s=s.replace('ld de,#C962','ld de,#C8E2').replace('MsgFileReply: db "SD RESPONSE TRANSFER FAILED",0\n','')
    a=s.index('; Codex: последний обслуживаемый');b=s.index('\n\nPrintWrapped:',a);s=s[:a]+s[b:];p.write_text(s,encoding='utf-8')
    p=SRC/'display.asm';s=p.read_text(encoding='utf-8');a=s.index('        ld a,(LastRequestValid)');b=s.index('        ld hl,MsgReset',a);s=s[:a]+s[b:]
    a=s.index('        ; Codex: имена файлов');b=s.index("        cp ' '",a);s=s[:a]+s[b:]
    s=s.replace("        cp '.'\n        jr nz,.colon\n        ld a,26\n        jr .glyph\n.colon:\n",'');p.write_text(s,encoding='utf-8')
    cmd=['E:/zx/z80/zuma/sjasmplus.exe','--nologo','--inc='+str(SRC),'--inc='+str(REF/'third_party/fat32/include'),'--sym=build/hmm2.sym',str(SRC/'hmm2.asm')]
    r=subprocess.run(cmd,cwd=OUT,capture_output=True);(OUT/'asm.log').write_bytes(r.stdout+r.stderr);assert r.returncode==0,(r.stdout+r.stderr).decode(errors='replace')
    binary=(OUT/'build/hmm2_z80.bin').read_bytes();assert old.read_bytes()[1024:1024+len(binary)]==binary
    shutil.copy2(OUT/'build/hmm2_z80.bin',OUT/'build/hmm2.bin')
    print(json.dumps(dict(spg_sha256=EXPECTED,bytes=len(binary),symbols=str(OUT/'build/hmm2.sym')),indent=2))
if __name__=='__main__':main()
