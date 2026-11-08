"""Codex: отдельный replay VDAC4 с ARGB-палитрой; чужая ветка и общий FT812 не меняются."""
from pathlib import Path
import json,hashlib,subprocess,sys,shutil,difflib
from build_stage1 import ROOT,VC,KIT
U=Path('E:/zx/unreal-ng/src/scratch/wt-vdac4')
BASE=Path('E:/zx/cache/tmp/hmm2_esp/integration_audio/unreal_replay')
OUT=BASE/'vdac4_color'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd,name):
 r=subprocess.run(list(map(str,cmd)),cwd=OUT,capture_output=True);(OUT/name).write_bytes(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace')[-5000:])
def main():
 OUT.mkdir(parents=True,exist_ok=True);eve=U/'core/src/3rdparty/eve-emu';source=eve/'src/eve-bitmap.cpp';before=source.read_text(encoding='utf-8');s=before
 anchor='// The texel at (tx, ty) of a bitmap, inside its layout.'
 helper='''#ifdef EVE_VDAC4_PALETTED8
// Codex: RTL VDAC4 ft_rd_exec.sv читает одну полную BGRA-запись, не один канал FT812.
Rgba Vdac4Palette(const EveChip& chip, uint32_t address)
{
    return Rgba{ReadByte(chip, address + 2), ReadByte(chip, address + 1),
                ReadByte(chip, address), ReadByte(chip, address + 3)};
}
#endif

'''
 assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor)
 old='''        const uint32_t v = ReadByte(chip, run.ctx.paletteSource + kPalette32Bytes * ReadByte(chip, row + x));
        return Rgba{v, v, v, v};'''
 new='''#ifdef EVE_VDAC4_PALETTED8
        return Vdac4Palette(chip, run.ctx.paletteSource + kPalette32Bytes * ReadByte(chip, row + x));
#else
'''+old+'''
#endif'''
 assert s.count(old)==1;s=s.replace(old,new)
 old='''        const bool inRamG = static_cast<uint64_t>(source) + kPalette32Bytes * (kPaletteEntries - 1) + 1 <= chip.table->ramGSize;'''
 new='''#ifdef EVE_VDAC4_PALETTED8
        const bool inRamG = static_cast<uint64_t>(source) + kPalette32Bytes * kPaletteEntries <= chip.table->ramGSize;
#else
'''+old+'''
#endif'''
 assert s.count(old)==1;s=s.replace(old,new)
 old='''            cache.entry[i] = v * 0x01010101u;'''
 new='''#ifdef EVE_VDAC4_PALETTED8
            (void)v;
            cache.entry[i] = Pack(Vdac4Palette(chip, source + kPalette32Bytes * i));
#else
'''+old+'''
#endif'''
 assert s.count(old)==1;s=s.replace(old,new)
 local=OUT/'eve-bitmap.cpp';local.write_text(s,encoding='utf-8');(OUT/'vdac4_pal8.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='a/core/src/3rdparty/eve-emu/src/eve-bitmap.cpp',tofile='b/core/src/3rdparty/eve-emu/src/eve-bitmap.cpp')),encoding='utf-8')
 inc=[VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um','winrt']],eve/'include',eve/'src',eve/'vendor',eve/'vendor/config']
 obj=OUT/'hmm2-vdac4-bitmap.obj'
 run([VC/'bin/Hostx64/x64/cl.exe','/nologo','/O2','/MD','/DNDEBUG','/D_ITERATOR_DEBUG_LEVEL=0','/DEVE_VDAC4_PALETTED8=1','/DEVE_HAS_BUILTIN_INFLATE=1','/DEVE_HAS_BUILTIN_JPEG=1','/DEVE_HAS_BUILTIN_PNG=1','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN','/D_CRT_SECURE_NO_WARNINGS','/std:c++20','/EHsc','/utf-8','/W4','/WX',*['/I'+str(i) for i in inc],'/c',local,'/Fo'+str(obj)],'compile.log')
 libs=[]
 for name in ['core.lib','eve-emu.lib','vdac4-firmware.lib']:
  target=OUT/name;shutil.copy2(BASE/name,target);libs.append(target)
 replay=(ROOT/'tests/stage1_unreal_replay.cpp').read_text(encoding='utf-8')
 capture='            uint64_t hash=UINT64_C(14695981039346656037);'
 settle='''            // Codex: DLSWAP=0 означает принятие DL; Picture нужен после полного следующего scanout.
            const uint64_t accepted=device.Frames();
            for(unsigned i=0;i<100&&device.Frames()<=accepted;++i)advance(1);
            if(device.Frames()<=accepted)throw std::runtime_error("Completed scanout timeout");
'''
 assert replay.count(capture)==1;replay=replay.replace(capture,settle+capture)
 local_replay=OUT/'hmm2-color-replay.cpp';local_replay.write_text(replay,encoding='utf-8');replay_obj=OUT/'hmm2-color-replay.obj'
 run([VC/'bin/Hostx64/x64/cl.exe','/nologo','/O2','/MD','/DNDEBUG','/D_ITERATOR_DEBUG_LEVEL=0','/std:c++20','/EHsc','/utf-8','/W4','/WX',*['/I'+str(i) for i in [VC/'include',*[KIT/'Include/10.0.26100.0'/d for d in ['ucrt','shared','um']],U/'core/src']],'/c',local_replay,'/Fo'+str(replay_obj)],'replay_compile.log')
 exe=OUT/'hmm2_unreal_color_replay.exe';paths=[VC/'lib/x64',*[KIT/'Lib/10.0.26100.0'/d/'x64' for d in ['ucrt','um']]]
 run([VC/'bin/Hostx64/x64/link.exe','/NOLOGO','/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),replay_obj,obj,*libs,*['/LIBPATH:'+str(i) for i in paths],'user32.lib','advapi32.lib','shell32.lib','ole32.lib'],'link.log')
 assert source.read_text(encoding='utf-8')==before
 (OUT/'build.json').write_text(json.dumps(dict(source=str(source),source_sha256=sha(source),local_source_sha256=sha(local),libraries={f.name:sha(f) for f in libs},executable_sha256=sha(exe),vdac4_pal8_argb=True,graphics_replay_only=True,foreign_tree_modified=False),indent=2),encoding='utf-8');print(exe)
if __name__=='__main__':main()
