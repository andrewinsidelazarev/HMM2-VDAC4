"""Codex: измеряем удаление трёх входов редактора; игровые ветви остаются исходными."""
from pathlib import Path
import subprocess,json
from compile_probe import TC,ROOT,SRC,SDK,OUT as BASE
from build_pic_runtime import OUT as RT,BASE as STAGE
def main():
    out=STAGE/'editor';out.mkdir(exist_ok=True)
    p=BASE/'staged_fheroes2_game_game_mainmenu.cpp'
    if not p.exists():p=next(BASE.glob('staged_*game_mainmenu.cpp'))
    s=p.read_text(encoding='utf-8')
    for old in ['Editor::menuMain( false )','Editor::menuMain( true )','Editor::menuLoadMap()']:
        assert s.count(old)==1;s=s.replace(old,'fheroes2::GameMode::MAIN_MENU')
    staged=out/'game_mainmenu.cpp';staged.write_text(s,encoding='utf-8')
    inc=[BASE/'headers',ROOT/'tools/probe_include',SRC/'engine',SRC/'thirdparty/libsmacker',SDK,SDK/'SDL2',*sorted(p for p in (SRC/'fheroes2').iterdir() if p.is_dir())]
    flags=['-std=c++17','-Os','-DNDEBUG','-fPIC','-fvisibility=hidden','-ffunction-sections','-fdata-sections','-mlongcalls','-fno-strict-aliasing','-fpermissive','-flto',*['-I'+str(i) for i in inc]]
    r=subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),*flags,'-c',str(staged),'-o',str(out/'mainmenu.o')],capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace')
    args=[s.strip().strip('"') for s in (STAGE/'lto/all.rsp').read_text().splitlines()]
    args=[str(out/'mainmenu.o') if a.endswith('fheroes2_game_game_mainmenu.cpp.o') else a for a in args]
    assert str(out/'mainmenu.o') in args
    for mode in ['no_runtime','pic_runtime']:
        cmd=[str(out/(mode+'.elf')) if a.endswith('/all.elf') else '-Wl,-Map,'+str(out/(mode+'.map')) if a.startswith('-Wl,-Map,') else a for a in args]
        if mode=='pic_runtime':cmd+=['-Wl,--exclude-libs,ALL','-Wl,--start-group',str(RT/'libstdcpp_pic.a'),str(RT/'libunwind_pic.a'),'-Wl,--end-group']
        rsp=out/(mode+'.rsp');rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in cmd),encoding='utf-8')
        r=subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),'@'+str(rsp)],capture_output=True);(out/(mode+'.log')).write_bytes(r.stdout+r.stderr)
        print(mode,r.returncode,flush=True);assert not r.returncode,r.stderr.decode(errors='replace')
        sec=subprocess.check_output([str(TC/'xtensa-esp32s3-elf-size.exe'),'-A',str(out/(mode+'.elf'))],text=True)
        (out/(mode+'_size.txt')).write_text(sec,encoding='utf-8');print(sec,flush=True)
if __name__=='__main__':main()
