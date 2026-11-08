"""Codex: пересборка стенда из локальной копии библиотек IDF без каналов Ninja."""
from pathlib import Path
import json, os, re, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
BASE = Path(os.environ['HMM2_STAGE1_BASE']).resolve()
OUT = BASE / 'qemu_stage1'
SOURCE = OUT / 'build_original'
BUILD = OUT / 'manual_build'
OLD = json.loads((SOURCE / 'project_description.json').read_text())['project_path']

def relocate(s):
    for a, b in [(OLD + '/build', BUILD.as_posix()), (OLD, OUT.as_posix())]:
        s = s.replace(a, b).replace(a.replace('/', '\\'), b)
    return s

def main():
    if not BUILD.exists():
        shutil.copytree(SOURCE, BUILD)
        for p in BUILD.rglob('*'):
            if p.is_file() and (p.suffix in ['.ld', '.S', '.rsp', '.json', '.ninja'] or p.parent.name == 'toolchain'):
                try:
                    s = p.read_text(encoding='utf8'); t = relocate(s)
                    if s != t: p.write_text(t, encoding='utf8')
                except UnicodeError: pass
    for p in (ROOT / 'tests/qemu_stage1/main').iterdir():
        if p.is_file(): shutil.copy2(p, OUT / 'main' / p.name)
    (OUT / 'main/test_paths.h').write_text('#define HMM2_TEST_BASE "' + BASE.as_posix() + '"\n')
    module = Path(sys.argv[1]) if len(sys.argv) > 1 else BASE / 'module/HMM2.ELF'
    shutil.copy2(module, OUT / 'main/HMM2.ELF')
    for name in ['HMM2.ELF', 'OGG.ELF']:
        p = OUT / 'main' / name; symbol = name.replace('.', '_')
        (BUILD / (name + '.S')).write_text('.section .rodata.embedded\n.global '+symbol+'\n'+symbol+':\n.global _binary_'+symbol+'_start\n_binary_'+symbol+'_start:\n.incbin "'+p.as_posix()+'"\n.global _binary_'+symbol+'_end\n_binary_'+symbol+'_end:\n.global '+symbol+'_length\n'+symbol+'_length:\n.long '+str(p.stat().st_size)+'\n')
    with (OUT / 'manual_build.log').open('w', encoding='utf8') as log:
        def run(cmd):
            log.write(cmd + '\n'); log.flush()
            subprocess.run(cmd, cwd=BUILD, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        objects = []
        for x in json.loads((SOURCE / 'compile_commands.json').read_text()):
            if '/qemu_stage1/main/' in x['file'].replace('\\', '/') or x['file'].endswith(('HMM2.ELF.S', 'OGG.ELF.S')):
                cmd = relocate(x['command']).removeprefix('ccache ')
                run(cmd); objects.append(re.search(r' -o (.*?) -c ', cmd).group(1))
        from compile_probe import TC
        run('"'+str(TC/'xtensa-esp32s3-elf-ar.exe')+'" rcs esp-idf/main/libmain_walk.a '+' '.join(objects))
        shutil.copy2(BUILD/'esp-idf/main/libmain_walk.a', BUILD/'esp-idf/main/libmain.a')
        block=(BUILD/'build.ninja').read_text().split('build hmm2_runtime_qemu.elf:')[1].split('\n\n')[0]
        v=dict(re.findall(r'^  (\w+) = (.*)$',block,re.M))
        args=' '.join(v.get(k,'') for k in ['FLAGS','LINK_FLAGS'])+' CMakeFiles/hmm2_runtime_qemu.elf.dir/project_elf_src_esp32s3.c.obj '+v.get('LINK_PATH','')+' '+v['LINK_LIBRARIES']+' -o hmm2_runtime_qemu.elf'
        (BUILD/'manual_link.rsp').write_text(args,encoding='utf8')
        run('"'+str(TC/'xtensa-esp32s3-elf-g++.exe')+'" @manual_link.rsp')
        assert module.read_bytes() in (BUILD/'hmm2_runtime_qemu.elf').read_bytes()
        run('"'+sys.executable+'" -m esptool --chip esp32s3 elf2image --flash-mode dio --flash-freq 80m --flash-size 8MB -o hmm2_runtime_qemu.bin hmm2_runtime_qemu.elf')
    for name in ['hmm2_runtime_qemu.elf','hmm2_runtime_qemu.bin','hmm2_runtime_qemu.map','flasher_args.json','bootloader/bootloader.bin','partition_table/partition-table.bin']:
        dst=OUT/'build'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(BUILD/name,dst)
    print('WALK_QEMU_BUILD_OK', flush=True)

if __name__ == '__main__': main()
