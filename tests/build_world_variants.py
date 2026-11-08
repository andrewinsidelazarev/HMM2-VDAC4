"""Codex: два одинаково инструментированных движка; во втором только потоковое чтение MP2 addons."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
variant=sys.argv[1];assert variant in ['baseline','stream']
text=(ROOT/'tests/build_stage0b_profile.py').read_text(encoding='utf-8')
text=text[:text.index('exec(compile(')]
ns={'__file__':str(ROOT/'tests/build_stage0b_profile.py')}
exec(compile(text,str(ROOT/'tests/build_stage0b_profile.py'),'exec'),ns)
text=ns['text']
text=text.replace("'/DHMM2_STAGE0B'","'/DHMM2_STAGE0B','/DHMM2_WORLD_COMPARE'")
text=text.replace("executable=OUT/('profile_smk_stream.exe' if stream_video else 'profile_smk.exe' if args.video else 'profile.exe')",f"executable=OUT/'world_{variant}.exe'")
text=text.replace("        elif src.name=='image.cpp':",'''        elif src.name=='rand.cpp':
            # Codex: пазлы королевств создаются ещё глобальными конструкторами, до profile_run.
            s=replace_once(s,'thread_local PCG32 gen( rd );','thread_local PCG32 gen(12345,6789);')
            modified=True
        elif src.name=='image.cpp':''')
if variant=='stream':
    insertion='''        elif src.name=='world_loadmap.cpp':
            s=replace_once(s,'    std::vector<MP2::MP2AddonInfo> vec_mp2addons( addonCount );','    const size_t addonStart = fs.tell();')
            s=replace_once(s,'    for ( auto & mp2addon : vec_mp2addons ) {\\n        MP2::loadAddon( fs, mp2addon );\\n    }','    fs.skip(addonCount * MP2::MP2_ADDON_STRUCTURE_SIZE);')
            s=replace_once(s,'vec_mp2addons.size() <= addonIndex','addonCount <= addonIndex')
            s=replace_once(s,'            const auto & objectInfo = vec_mp2addons[addonIndex];',
                '            // Codex: читаем тот же addon без хранения всей таблицы; возвращаем позицию потока тайлов.\\n'
                '            const size_t tilePosition=fs.tell();\\n'
                '            fs.seek(addonStart+addonIndex*MP2::MP2_ADDON_STRUCTURE_SIZE);\\n'
                '            MP2::MP2AddonInfo objectInfo;MP2::loadAddon(fs,objectInfo);\\n'
                '            fs.seek(tilePosition);')
            s=replace_once(s,'addonIndex = vec_mp2addons[addonIndex].nextAddonIndex;','addonIndex = objectInfo.nextAddonIndex;')
            modified=True
'''
    text=text.replace("        elif src.name=='image.cpp':",insertion+"        elif src.name=='image.cpp':")
sys.argv=[sys.argv[0],'--video']
exec(compile(text,str(ROOT/'tests/build_profile.py'),'exec'),{'__file__':str(ROOT/'tests/build_profile.py'),'__name__':'__main__'})
