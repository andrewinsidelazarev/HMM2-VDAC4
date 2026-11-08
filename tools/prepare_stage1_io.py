"""Codex: перенос проверенного транспорта DOOM без изменения пакетов ZiFi."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    s=Path('E:/zx/DOOM VDAC4/main/app.c').read_text(encoding='utf-8')
    start=s.index('static void *io_signal');end=s.index('\nstatic int command(')
    shared=s[start:end]
    a=shared.index('static uint8_t *cache;');b=shared.index('static uint16_t rd16',a)
    shared=shared[:a]+shared[b:]
    a=shared.index('size_t vd_wad_read(');b=shared.index('/* Байты PS/2',a)
    shared=shared[:a]+shared[b:]
    shared=shared.replace('static size_t remote_read','size_t hmm_remote_read')
    shared=shared.replace(' vd_load_bytes+=size;','')
    a=shared.index('            uint32_t took=');b=shared.index('            break;',a)
    shared=shared[:a]+shared[b:]
    cmd=s[end:s.index('\n}',end)+2].replace('static int command','int hmm_command')
    cmd=cmd.replace('!strcmp((const char*)p+4,"DOOM1.WAD") || !strcmp((const char*)p+4,"DOOM.WAD") || !strcmp((const char*)p+4,"DOOM2.WAD")','!strcmp((const char*)p+4,"HMM2.DAT")')
    cmd=cmd.replace('memcpy(vd_wad_name,p+4,n-4); ','')
    cmd=cmd.replace('VD_ERROR_MAX','95')
    combined=shared+cmd
    for old,new in [('vd_core','hmm_core'),('vd_state','hmm_state'),('vd_wad_size','hmm_package_size'),('vd_frames','hmm_frames'),('vd_error','hmm_error'),('vd_fail','hmm_fail'),('vd_fs_call','hmm_fs_call'),('vd_keys_take','hmm_keys_take'),('vd_mouse_take','hmm_mouse_take')]:combined=combined.replace(old,new)
    combined=combined.replace('vd_checkpoint();','if(!hmm_running) hmm_fail("Application stopped");')
    combined=combined.replace('WAD request queue full','Evo read request queue full').replace('Evo WAD read timeout','Evo read timeout')
    combined+='''
int hmm_io_init(void){
    io_signal=hmm_core->signal_create();io_mutex=hmm_core->mutex_create();
    request_id=0;awaiting=fs_awaiting=0;ps2_head=ps2_tail=ps2_lost=0;mouse_sync=0;
    return io_signal && io_mutex;
}
'''
    (ROOT/'main/hmm_io.c').write_text('/* Codex: транспорт DOOM VDAC4, авторы и лицензии — THIRD_PARTY.md.\n * SPDX-License-Identifier: GPL-2.0-or-later */\n#include "hmm_core.h"\n#include <string.h>\n'+combined,encoding='utf-8')
if __name__=='__main__':main()
