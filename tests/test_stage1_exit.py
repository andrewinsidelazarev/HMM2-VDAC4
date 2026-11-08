"""Codex: Q и сброс Evo→SLOT0; два запуска приложения на одном живом транспорте ESP."""
from pathlib import Path
import argparse,json,os,shutil,struct,sys
from test_stage1_joint import BASE,ROOT,base,fixture,Native,Machine

def check_closed(row):
    state=row['lifecycle']
    assert state['done']==1 and state['state']==3,row
    assert state['core']==1 and not state['ft_cfg']&4 and state['ft_closes']>=1,row

def main():
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['q','reset']);parser.add_argument('--timeout',type=int,default=180)
    parser.add_argument('--before-poll',action='store_true');parser.add_argument('--during-load',action='store_true')
    args=parser.parse_args();os.environ.pop('HMM2_TEST_MAP',None)
    out=BASE/'joint'/('exit_'+args.mode+('_before_poll' if args.before_poll else '')+('_during_load' if args.during_load else ''))
    if out.exists():
        i=1
        while out.with_name(out.name+'.old'+str(i)).exists():i+=1
        out.rename(out.with_name(out.name+'.old'+str(i)))
    out.mkdir(parents=True)
    dep=base.ROOT/'third_party/fat32/build';dep.mkdir(parents=True,exist_ok=True)
    if not os.environ.get('HMM2_SKIP_SHARED_SETUP'):
        for name in ['fat32.bin','fat32-work.bin']:shutil.copy2(BASE/'launcher/build'/name,dep/name)
        shutil.copy2(BASE/'launcher/build/hmm2_z80.bin',BASE/'launcher/build/hmm2.bin')
    elf=(BASE/'module/HMM2.ELF').read_bytes();spg=(BASE/'launcher/_HMM2.SPG').read_bytes()
    data=bytearray((BASE/'data/HMM2.DAT').read_bytes());count=struct.unpack_from('<I',data,8)[0]
    entries=[data[16+i*72:16+(i+1)*72] for i in range(count)]
    entries=[e for e in entries if not e.startswith(b'maps/') or e.split(b'\0',1)[0].lower()==b'maps/kingdth.mx2']
    assert sum(e.startswith(b'maps/') for e in entries)==1
    # Codex: меняется только каталог тестового SD; адреса и содержимое ресурсов исходного DAT сохраняются.
    struct.pack_into('<I',data,8,len(entries));data[16:16+72*len(entries)]=b''.join(entries)
    disk=fixture({'HMM2.ELF':elf,'HMM2.DAT':bytes(data),'FHEROES2.CFG':(ROOT/'main/fheroes2.cfg').read_bytes()})
    native=Native(out,elf);machine=Machine(disk,native,spg,out,0)
    machine.reset_before_poll=args.before_poll
    if args.during_load:machine.reset_during_load=True;machine.exit_target=93
    first=machine.run_joint(args.timeout,play=True,exit_mode=args.mode);check_closed(first)
    assert first['exit_frame']>=(93 if args.during_load else 245),first
    assert first['world_ready']==(not args.during_load),first
    if args.mode=='reset':
        assert first['reset_stop_s']<3 and first['lifecycle']['core_backs']==0,first
        assert first['lifecycle']['loaded']==(0 if os.environ.get('HMM2_TEST_AUTOUNLOAD') else 1),first
        if os.environ.get('HMM2_TEST_AUTOUNLOAD'):
            native.release_app();assert native.unloads==1
            resource=(__import__('ctypes').c_uint32*4)();native.d.joint_resources(resource)
            assert list(resource)==[0,0,0,0],list(resource)
        if args.before_poll and not os.environ.get('HMM2_TEST_AUTOUNLOAD'):assert first['lifecycle']['invalid_ft']>0,first
    else:
        assert first['pc']==hex(machine.symbols['Halt']) and first['code']==0,first
        assert first['lifecycle']['core_backs']==1,first
    machine.events.close()
    again=out/'restart';again.mkdir();native.app_out=again
    # Codex: новый запуск WC моделируется новым Z80; SPI ESP и состояние Tang не переинициализируются.
    second_machine=Machine(disk,native,spg,again,0)
    second=second_machine.run_joint(args.timeout,exit_mode='q');check_closed(second);native.release_app()
    assert second['pc']==hex(second_machine.symbols['Halt']) and second['code']==0,second
    assert second['frames']>=20 and second['lifecycle']['starts']==2 and second['lifecycle']['stops']==2,second
    assert native.unloads==2 and second['commands'].get('6C')==2,second
    result=dict(scenario=args.mode,first=first,restart=second,application_unloads=native.unloads,
                esp_transport_restarts=0,native_build=native.build,auto_unload=bool(os.environ.get('HMM2_TEST_AUTOUNLOAD')))
    (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='native_build'},indent=2),flush=True)
    native.trace.close();second_machine.events.close();return 0

if __name__=='__main__':
    # Codex: постоянные потоки модели ESP живут до конца процесса, а обе DLL игры уже выгружены.
    try:code=main()
    except BaseException:
        import traceback;traceback.print_exc();sys.stderr.flush();code=1
    sys.stdout.flush();os._exit(code)
