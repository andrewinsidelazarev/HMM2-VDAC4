"""Codex: компиляция копий двух единиц прошивки с её настоящими флагами и заголовками."""
from pathlib import Path
import ctypes as C,json,subprocess,sys
R=Path(__file__).resolve().parents[1];P=R/'patches/slot0_autounload'
F=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware')
O=Path('E:/zx/cache/tmp/hmm2_esp/slot0_autounload');O.mkdir(exist_ok=True)
def split(command):
    n=C.c_int();f=C.windll.shell32.CommandLineToArgvW;f.argtypes=[C.c_wchar_p,C.POINTER(C.c_int)];f.restype=C.POINTER(C.c_wchar_p)
    p=f(command,C.byref(n));a=[p[i] for i in range(n.value)];C.windll.kernel32.LocalFree(C.cast(p,C.c_void_p));return a
def main():
    results=[]
    for row in json.loads((F/'build/compile_commands.json').read_text(encoding='utf-8')):
        source=Path(row['file'])
        if not source.is_relative_to(F):continue
        name=source.relative_to(F).as_posix()
        if name not in ['src/main.cpp','src/apps/module_app.cpp']:continue
        args=split(row['command']);out=O/(source.stem+'.o');dep=O/(source.stem+'.d')
        for i,a in enumerate(args):
            if a.replace('\\','/').casefold()==str(source).replace('\\','/').casefold():args[i]=str(P/'after'/name)
            if i and args[i-1]=='-o':args[i]=str(out)
            if i and args[i-1]=='-MF':args[i]=str(dep)
        args.insert(1,'-I'+str(P/'after/include'))
        (O/(source.stem+'_command.json')).write_text(json.dumps(args,indent=2),encoding='utf-8')
        run=subprocess.run(args,cwd=O,capture_output=True);(O/(source.stem+'.log')).write_bytes(run.stdout+run.stderr)
        results.append(dict(source=name,exit=run.returncode,object_bytes=out.stat().st_size if out.exists() else 0))
        if run.returncode:print((run.stdout+run.stderr).decode(errors='replace')[-4000:]);raise SystemExit(1)
    assert len(results)==2
    (O/'compile_result.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(results)
if __name__=='__main__':main()
