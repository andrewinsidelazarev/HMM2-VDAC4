// The game and cursor hold the same writer guard around this state.
// Every DLSWAP writer invalidates the confirmation before touching FT812.
bool swap_pending=true;
HMM_SIZE_RENDER void wait_swap_locked(){
    if(!swap_pending)return;
    const uint32_t start=hmm_core->millis();uint32_t swap;
    do{
        if(!hmm_running)hmm_fail("Application stopped");
        if(hmm_core->ft_read(0x302054,&swap)!=ZM_OK)hmm_fail("FT812 DLSWAP read failed");
        if(!swap){swap_pending=false;return;}
        if(uint32_t(hmm_core->millis()-start)>2000)hmm_fail("FT812 DLSWAP timeout");
        hmm_core->delay(1);
    }while(true);
}
