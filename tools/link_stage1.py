"""Codex: окончательная линковка приложения с принимаемыми загрузчиком секциями."""
from pathlib import Path
import argparse,json,subprocess,struct,sys
from compile_probe import ROOT,TC,SDK
from link_probe import FW
from prepare_stage1 import BASE
from pack_stage1_module import pack
from prepare_stage1_puff import prepare_puff
RT=Path('E:/zx/cache/tmp/hmm2_esp/stage0b/pic_runtime')
OUT=BASE/'module'
def run(cmd,name):
    p=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True);(OUT/name).write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace')[-6500:])
    return p.stdout.decode(errors='replace')
def main():
    global OUT
    # Codex: отдельный результат линковки не меняет ELF выполняемого стенда.
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=OUT);parser.add_argument('--lto-partitions',type=int,default=6);parser.add_argument('--compact-branches',action='store_true')
    options=parser.parse_args();OUT=options.out.resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    flags=['-Oz','-fPIC','-fvisibility=hidden','-mlongcalls','-ffunction-sections','-fdata-sections','-fexceptions','-fno-builtin','-fstack-usage','-Wstack-usage=2048']
    inc=[ROOT/'main',FW/'modules/sdk/include',SDK,SDK/'SDL2']
    objects=json.loads((BASE/'xtensa_packed/objects.json').read_text())
    for p in [*map(lambda n:ROOT/'main'/n,['hmm_entry.c','hmm_app.c','hmm_session.c','hmm_io.c','hmm_files.c','hmm_input.c','hmm_libc.c','hmm_heap.c','hmm_video.c','hmm_failure.cpp'])]:
        obj=OUT/(p.stem+'.o');cc=TC/('xtensa-esp32s3-elf-g++.exe' if p.suffix=='.cpp' else 'xtensa-esp32s3-elf-gcc.exe')
        # Share cold file/transaction helpers with the engine through LTO.
        # Keep the transport, bootstrap and other C units on their existing flags.
        extra=['-flto'] if p.name=='hmm_files.c' else []
        run([cc,*flags,*extra,*['-I'+str(x) for x in inc],'-c',p,'-o',obj],p.stem+'.log');objects.append(str(obj))
    puff=prepare_puff();obj=OUT/'puff.o'
    run([TC/'xtensa-esp32s3-elf-gcc.exe',*flags,'-fno-exceptions','-DHMM_PUFF_TARGET','-c',puff,'-o',obj],'puff.log');objects.append(str(obj))
    script=(ROOT/'bench/runtime.ld').read_text(encoding='utf-8').replace('.data : {','.data : { __hmm_data_begin = .;').replace('  .dynamic :','  . = ALIGN(16);\n  .rodata : { KEEP(*(.hmm_pack)) KEEP(*(.hmm_boot)) }\n  .dynamic :')
    (OUT/'runtime.ld').write_text(script,encoding='utf-8')
    # Codex: core вызывает только числовую точку входа; внутренние SDL/C++ символы не экспортируются.
    exports=OUT/'exports.map';exports.write_text('{ local: *; };\n',encoding='ascii')
    # Клод: шесть разделов LTO, как у выданной сборки 59: с восемью ELF на 5 КиБ больше и не входит в лимит.
    args=['-nostdlib','-nostartfiles','-shared','-fPIC','-Oz','-flto=6','--param=lto-partitions='+str(options.lto_partitions),'-fstack-usage','-Wstack-usage=2048','-save-temps','-Wl,--gc-sections','-Wl,-Bsymbolic','-Wl,--exclude-libs,ALL','-Wl,--version-script='+str(exports),'-Wl,-e,main','-T',str(OUT/'runtime.ld'),*objects,'-Wl,--start-group',str(RT/'libstdcpp_pic.a'),str(RT/'libunwind_pic.a'),str(BASE/'runtime/libhmm_pic.a'),'-Wl,--end-group','-o',str(OUT/'HMM2.raw.ELF'),'-Wl,-Map,'+str(OUT/'HMM2.map')]
    if options.compact_branches:args.insert(0,'-mno-target-align')
    rsp=OUT/'link.rsp';rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args),encoding='utf-8')
    # Codex: GCC -save-temps повторно использовал старые ltrans.o после изменения числа разделов.
    # Удаляем только его временные файлы в проверенном каталоге этой сборки, сохраняя ELF и исходники.
    for old in OUT.glob('HMM2.raw.ELF.ltrans*'):
        assert old.parent.resolve()==OUT.resolve()
        if old.is_file():old.unlink()
    run([TC/'xtensa-esp32s3-elf-g++.exe','@'+str(rsp)],'link.log')
    symbols=run([TC/'xtensa-esp32s3-elf-nm.exe','HMM2.raw.ELF'],'nm.txt');syms={s.split()[-1]:int(s.split()[0],16) for s in symbols.splitlines() if len(s.split())==3}
    raw=(OUT/'HMM2.raw.ELF').read_bytes();marker=struct.pack('<I',0x4d324548);assert raw.count(marker)==1
    raw=raw.replace(marker,struct.pack('<i',syms['anchor']-syms['__hmm_eh_begin']));(OUT/'HMM2.ELF').write_bytes(raw)
    run([TC/'xtensa-esp32s3-elf-strip.exe','--strip-unneeded',*['--remove-section='+n for n in ['.hash','.gnu.hash','.comment','.got.loc','.dynamic','.xt.lit','.xt.prop','.xtensa.info']],'HMM2.ELF'],'strip.log')
    (OUT/'HMM2.unpacked.ELF').write_bytes((OUT/'HMM2.ELF').read_bytes())
    print(json.dumps(pack(OUT/'HMM2.ELF'),indent=2),flush=True)
    print('ELF:',(OUT/'HMM2.ELF').stat().st_size,flush=True)
    # Match ZmCache::footprint/canFit without charging the OGG header twice.
    # Installed OGG is 35,420 bytes: reserve up to 36,864 payload bytes,
    # i.e. ten blocks TOTAL including its header. Physical capacity/reserve
    # remain unchanged. Delivery must check the decoder size on the board.
    size=(OUT/'HMM2.ELF').stat().st_size;footprint=lambda n:(n+4095)//4096*4096+4096
    budget=dict(capacity=1703936,module_bytes=size,module_footprint=footprint(size),ogg_payload_max=36864,
                ogg_footprint=footprint(36864),index_footprint=footprint(680))
    budget['spare']=budget['capacity']-sum(budget[k] for k in ('module_footprint','ogg_footprint','index_footprint'))
    (OUT/'cache_budget.json').write_text(json.dumps(budget,indent=2),encoding='utf-8')
    assert budget['spare']>=0,f'ELF {size} bytes does not fit alongside OGG.ELF'
    print(run([TC/'xtensa-esp32s3-elf-size.exe','-A','HMM2.ELF'],'size.txt'),flush=True)
    p=subprocess.run([sys.executable,str(FW.parent/'Sound/tools/check_module_symbols.py'),str(OUT/'HMM2.ELF')],capture_output=True)
    (OUT/'check.log').write_bytes(p.stdout+p.stderr);print((p.stdout+p.stderr).decode(errors='replace'),flush=True);return p.returncode
if __name__=='__main__':raise SystemExit(main())
