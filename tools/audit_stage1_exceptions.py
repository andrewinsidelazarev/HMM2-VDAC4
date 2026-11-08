"""Codex: считаем активные throw/catch исходных единиц и притянутого PIC runtime; EH не отключаем."""
from pathlib import Path
import re,json,subprocess,concurrent.futures
from compile_probe import ROOT,SRC,TC,SDK
from prepare_stage1 import BASE
from link_probe import FW
RT=BASE.parent/'stage0b/pic_runtime';RS=BASE.parent/'stage0b/runtime_sources/gcc-15.2.0/libstdc++-v3'
OUT=BASE.parent/'fix1/exception_audit'
lex=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',re.S)
def clean(s):return lex.sub(lambda m:'\n'*m[0].count('\n'),s)
def tokens(s):
    # Codex: старые спецификации throw() и throw(T) не являются операторами выброса.
    s=re.sub(r'\bthrow\s*\([^()]*\)','',clean(s))
    return {k:len(re.findall(r'\b'+k+r'\b',s)) for k in ['throw','catch']}
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    original={str(p.relative_to(SRC)):tokens(p.read_text(encoding='utf-8')) for p in SRC.rglob('*.cpp') if 'thirdparty' not in p.parts}
    original={n:c for n,c in original.items() if any(c.values())}
    objs={Path(p).name for p in json.loads((BASE/'xtensa_packed/objects.json').read_text())}
    files=[p for p in (BASE/'src').rglob('*.cpp') if str(p.relative_to(BASE/'src')).replace('\\','_').replace('/','_')+'.o' in objs]
    files += [ROOT/'main/hmm_failure.cpp',ROOT/'main/hmm_resources.cpp',ROOT/'main/hmm_system.cpp']
    used=set(re.findall(r'libstdcpp_pic\.a\(([^)]+)\)',(BASE/'module/HMM2.map').read_text()))
    runtime=[RS/r['source'] for r in json.loads((RT/'compile.json').read_text()) if Path(r['object']).name in used]
    inc=[ROOT/'main',ROOT/'prototype',ROOT/'tools/probe_include',BASE/'src/engine',BASE/'src/thirdparty/libsmacker',SDK,SDK/'SDL2',FW/'modules/sdk/include',*[p for p in (BASE/'src/fheroes2').iterdir() if p.is_dir()]]
    rinc=[RS/'libsupc++',RS/'src',RS/'src/c++11',RS/'include',RS.parent/'include',RS.parent/'libgcc']
    def check(item):
        p,kind=item
        if kind=='game':flags=['-std=c++17','-DNDEBUG','-DHMM2_PACKED'];includes=inc
        else:
            flags=['-std='+('gnu++98' if p.parent.name=='c++98' else 'gnu++11' if p.parent.name=='c++11' else 'gnu++17'),'-fchar8_t','-D_GNU_SOURCE'];includes=rinc
        r=subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),*flags,*['-I'+str(x) for x in includes],'-E',str(p)],capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace')
        active=[];current='';own=str(p).replace('\\','/').lower()
        for line in r.stdout.decode(errors='replace').splitlines():
            mark=re.match(r'# \d+ "([^"]+)"',line)
            if mark:current=mark[1].replace('\\\\','/').replace('\\','/').lower()
            elif current==own:active.append(line)
        counts=tokens('\n'.join(active));return str(p.relative_to(ROOT) if p.is_relative_to(ROOT) else p.relative_to(RS) if kind=='runtime' else p.relative_to(BASE/'src')),counts,kind
    # Codex: препроцессор нужен также для макросов runtime, которые разворачиваются в throw.
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(check,[(p,'game') for p in files]+[(p,'runtime') for p in runtime]))
    result={'method':'Активные операторы в .cpp/.cc после препроцессора; шаблоны заголовков отдельно не суммируются. Включение TU архивом не означает достижимость каждого её метода после gc-sections.','original_cpp':original}
    for kind in ['game','runtime']:
        result[kind]={n:c for n,c,k in rows if k==kind and any(c.values())}
    result['totals']={kind:{k:sum(c[k] for c in result[kind].values()) for k in ['throw','catch']} for kind in ['original_cpp','game','runtime']}
    result['runtime_translation_units']=len(runtime);result['game_translation_units']=len(files)
    result['exceptions_enabled']=True
    (ROOT/'Docs/evidence/stage1_fix1_exceptions.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(result['totals'])
if __name__=='__main__':main()
