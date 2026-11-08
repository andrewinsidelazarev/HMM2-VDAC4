"""Codex: перенос временных таблиц со стека не меняет ни одно игровое поле."""
from pathlib import Path
import subprocess,sys,json,hashlib,re
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from build_stage1 import VC,KIT
from compile_probe import SRC,SDK
from prepare_stage1_stack import transform,closing
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix2/tables')

def table_functions(source):
    # Codex: стенд линкует только исходные заполнители, их зависимости и доступ к таблицам.
    # Описания UI не исполняются и не требуют всей игры в отдельном тесте.
    keep={'getMonsterBaseStrength','populateMonsterData','getMonsterData','isAbilityPresent',
          'populateArtifactData','getArtifactData','getCastleWallRangedPenalty'}
    pattern=r'^(?:    )?[\w:<>& *]+?\s+([\w:]+)\([^;{}]*\)(?: const)?\n(?:    )?\{'
    for m in reversed(list(re.finditer(pattern,source,re.M))):
        if m[1].split('::')[-1] not in keep:
            end=closing(source,m.end()-1)
            source=source[:m.start()]+source[end+1:]
    return source

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    includes=[VC/'include',*[KIT/'Include/10.0.26100.0'/n for n in ['ucrt','shared','um']],SRC/'engine',SDK,*[p for p in (SRC/'fheroes2').iterdir() if p.is_dir()]]
    libraries=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/n/'x64' for n in ['ucrt','um']]]
    cc=VC/'bin/Hostx64/x64/cl.exe'
    def run(args):
        p=subprocess.run(list(map(str,args)),cwd=OUT,capture_output=True);assert p.returncode==0,(p.stdout+p.stderr).decode(errors='replace');return p.stdout.decode()
    binaries=[];counts=[]
    for packed in [False,True]:
        label='packed' if packed else 'original';objects=[]
        sources=[SRC/'fheroes2/monster/monster_info.cpp',SRC/'fheroes2/resource/artifact_info.cpp',SRC/'fheroes2/game/game_static.cpp',ROOT/'tests/stage1_rules_tables.cpp']
        for source in sources:
            cpp=OUT/(label+'_'+source.name);s=source.read_text(encoding='utf-8')
            if source.name!='stage1_rules_tables.cpp':s=table_functions(s)
            cpp.write_text(transform(source.name,s) if packed else s,encoding='utf-8')
            obj=cpp.with_suffix('.obj');objects.append(obj)
            run([cc,'/nologo','/O2','/MT','/EHsc','/DNDEBUG','/Gy','/Gw','/GL','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS',*(['/DHMM2_PACKED'] if packed else []),*['/I'+str(p) for p in includes],'/c',cpp,'/Fo'+str(obj)])
        exe=OUT/(label+'.exe');run([cc.with_name('link.exe'),'/NOLOGO','/LTCG','/OPT:REF','/OUT:'+str(exe),*objects,*['/LIBPATH:'+str(p) for p in libraries]])
        data=OUT/(label+'.bin');counts.append(run([exe,data]).strip());binaries.append(data.read_bytes())
    assert counts[0]==counts[1] and binaries[0]==binaries[1]
    report=dict(counts=counts[0],bytes=len(binaries[0]),sha256=hashlib.sha256(binaries[0]).hexdigest(),all_fields_exact=True)
    (ROOT/'Docs/evidence/stage1_fix2_tables.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
if __name__=='__main__':main()
