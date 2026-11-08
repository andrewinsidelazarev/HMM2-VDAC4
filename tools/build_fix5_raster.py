"""Codex: копия точного ядра 1.34 и потактового стенда; исходное дерево Tang только читается."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess

SOURCE=Path('E:/zx/Tang Nano 20k VDAC/fpga/vdac2')
OUT=Path('E:/zx/cache/tmp/hmm2_esp/fix5/raster')
VR=Path('E:/zx/tools/rtl/ucrt64/share/verilator')

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    files=list((SOURCE/'out/core_1.34/rtl').glob('*'))
    files += [SOURCE/'tb/vsys'/n for n in ['sdram_cyc.sv','vsys_top.sv','vsys_main.cpp']]
    manifest={}
    for src in files:
        if not src.is_file():continue
        dst=OUT/('rtl' if src.parent.name=='rtl' else 'tb')/src.name
        dst.parent.mkdir(exist_ok=True);shutil.copy2(src,dst)
        manifest[str(src)]=hashlib.sha256(src.read_bytes()).hexdigest()
    # Codex: только оболочка стенда задаёт установившееся значение FT_CFG=1.
    # RAM_G 3 МиБ, SPI Evo включён, исходная логика RTL не редактируется.
    p=OUT/'tb/vsys_top.sv';s=p.read_text(encoding='utf-8')
    s=s.replace('    // --- замеры', "    initial force core.ft_cfg_3mb = 1'b1;\n\n    // --- замеры",1)
    p.write_text(s,encoding='utf-8')
    # Codex: отвод RGB и выбор стандартного HSIZE/VSIZE — только в оболочке измерителя.
    s=s.replace('    output wire        vid_de,','    output wire        vid_de,\n    output wire [7:0] vid_r,vid_g,vid_b,')
    s=s.replace('.vid_r(), .vid_g(), .vid_b(),','.vid_r(vid_r), .vid_g(vid_g), .vid_b(vid_b),')
    p.write_text(s,encoding='utf-8')
    p=OUT/'tb/vsys_main.cpp';s=p.read_text(encoding='utf-8')
    s=s.replace('    double now = 0.0;', '''    double now = 0.0;
    std::vector<uint8_t> hdmi=std::vector<uint8_t>(1280*720*3), hdmi_last;
    unsigned hdmi_at=0;''')
    s=s.replace('                t_pix += PS_PIX / 2;\n                top->eval();','''                t_pix += PS_PIX / 2;
                top->eval();
                if(top->pixel_clk && top->vid_de){
                    hdmi[hdmi_at++]=top->vid_r;hdmi[hdmi_at++]=top->vid_g;hdmi[hdmi_at++]=top->vid_b;
                    if(hdmi_at==hdmi.size()){hdmi_last=hdmi;hdmi_at=0;}
                }''')
    s=s.replace('    double seconds = 5.0, z80_mhz = 14.0;','    int native_w=1024,native_h=768;const char *hdmi_path=nullptr;\n    double seconds = 5.0, z80_mhz = 14.0;')
    s=s.replace('        else if (a == "--ref") refp = next();','''        else if (a == "--ref") refp = next();
        else if (a == "--native640") {native_w=640;native_h=480;}
        else if (a == "--hdmi") hdmi_path=next();''')
    s=s.replace('spi.write32(REG_HSIZE, 1024)','spi.write32(REG_HSIZE, native_w)').replace('spi.write32(REG_VSIZE, 768)','spi.write32(REG_VSIZE, native_h)')
    begin,separator,end=s.rpartition('    double secs = std::chrono::duration<double>')
    assert separator
    s=begin+'''    if(hdmi_path){FILE *f=std::fopen(hdmi_path,"wb");
        if(!f||sim.hdmi_last.empty())return 3;
        std::fwrite(sim.hdmi_last.data(),1,sim.hdmi_last.size(),f);std::fclose(f);}
'''+separator+end
    s=s.replace('(1L << 20) - 4)) & ~3u, g_hi = 1u << 20','(3L << 20) - 4)) & ~3u, g_hi = 3u << 20')
    p.write_text(s,encoding='utf-8')
    src=(SOURCE/'out/core_1.34/scripts/run_fb_sim.sh').read_text(encoding='utf-8')
    rtl=re.search(r'SRC="(.*?)"',src,re.S)[1].split();rtl=[str(OUT/p) for p in rtl if p.startswith('rtl/')]
    env=os.environ.copy();env['VERILATOR_ROOT']=str(VR)
    commands=[['E:/zx/tools/rtl/ucrt64/bin/verilator_bin.exe','--cc','-O3','--x-assign','fast','--x-initial','fast','--noassert','-Wno-fatal','-Wno-lint','-Wno-style','-Wno-MULTIDRIVEN','-DSIMULATION','-DVSYS_SD','-I'+str(OUT/'rtl'),'--top-module','vsys_top','--Mdir',str(OUT/'obj'),*rtl,str(OUT/'tb/sdram_cyc.sv'),str(OUT/'tb/vsys_top.sv')]]
    r=subprocess.run(commands[0],cwd=OUT,env=env,capture_output=True);(OUT/'verilator.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    commands.append(['E:/zx/llvm/bin/clang++.exe','-O2','-std=c++20','-w','-D_CRT_SECURE_NO_WARNINGS','-I'+str(OUT/'obj'),'-I'+str(VR/'include'),'-I'+str(VR/'include/vltstd'),*map(str,(OUT/'obj').glob('*.cpp')),str(VR/'include/verilated.cpp'),str(VR/'include/verilated_threads.cpp'),str(OUT/'tb/vsys_main.cpp'),'-o',str(OUT/'vsys.exe')])
    r=subprocess.run(commands[1],cwd=OUT,env=env,capture_output=True);(OUT/'clang.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    (OUT/'build.json').write_text(json.dumps(dict(sources=manifest,commands=commands,sha256=hashlib.sha256((OUT/'vsys.exe').read_bytes()).hexdigest()),indent=2),encoding='utf-8')
    print(OUT/'vsys.exe',flush=True)

if __name__=='__main__':main()
