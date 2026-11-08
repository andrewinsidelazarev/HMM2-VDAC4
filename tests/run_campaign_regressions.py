"""Codex: последовательные прогоны одного окончательного ELF; отдельный QEMU не запускается параллельно."""
from pathlib import Path
import json,os,subprocess,sys
root=Path(__file__).resolve().parents[1]
base=Path(os.environ['HMM2_STAGE1_BASE']).resolve().parent
dat='stage/data/HMM2_campaign.DAT'
roland=['--keys','7472245a06','--times','52000,54000,57000,59000,74000','--clicks','6000,520,220,1;10000,520,140,1;14000,520,75,1;20000,320,240,1;26000,490,180,1;32000,320,240,1;38000,430,445,1;47000,320,286,1','--frames','500','--end','82000']
scenes=[
 ('roland_final',dat,roland,{}),
 ('pol_complete',dat,['--keys','5a5a5a747206','--times','19000,25000,47000,52000,54000,60000','--clicks','6000,520,220,1;10000,520,140,1;14000,520,140,1;32000,430,445,1','--frames','300','--end','70000'],{}),
 ('alteris',dat,['--mode','Alteris.mp2','--keys','5a5a5a5a747206','--times','15000,30000,32000,34000,38000,40000,48000','--clicks','6000,520,220,1;10000,520,75,1','--frames','300','--end','58000','--psram','7340032'],{'HMM2_QEMU_CFG':str(base/'board.cfg')}),
 ('gatehell_7m',dat,['--mode','GateHell.mp2','--extended','--key-delay','3500','--frames','900','--end','112000','--psram','7340032'],{'HMM2_QEMU_CFG':str(base/'board.cfg')}),
 ('battle',dat,['--mode','battle','--keys','5a2929','--times','15000,30000,38000','--clicks','6000,520,220,1;10000,520,270,1','--frames','300','--end','48000'],{'HMM2_QEMU_CFG':str(base/'board.cfg')}),
 ('load_previous',dat,['--mode','load','--keys','4b3a335a5a747206','--times','6000,10000,13000,19000,43000,50000,52000,60000','--frames','350','--end','72000','--psram','7340032'],{'HMM2_QEMU_CFG':str(base/'board.cfg'),'HMM2_QEMU_SAVE':str(base/'inputs/GateHell_previous.savh')}),
 ('roland_rebuilt','rebuild/data/HMM2.DAT',roland,{}),
 ('roland_no_video','stage/data/HMM2_maps_only.DAT',['--keys','5a74245a06','--times','19000,42000,46000,48000,60000','--clicks','6000,520,220,1;10000,520,140,1;14000,520,75,1;26000,430,445,1;35000,320,286,1','--frames','300','--end','68000'],{}),
]
for name,package,options,extra in scenes:
    if len(sys.argv)>1 and name not in sys.argv[1:]:continue
    print('BEGIN',name,flush=True)
    env=os.environ.copy();env.update(extra)
    with (base/(name+'.batch.log')).open('wb') as log:
        result=subprocess.run([sys.executable,str(root/'tests/run_campaign_qemu.py'),name,'--dat',package,*options],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT)
    print('END',name,'rc',result.returncode,flush=True)
    if result.returncode:raise SystemExit(result.returncode)
