// The two scanout banks contain the same immutable cell at different times.
// Reuse its PAL8 bytes in FT RAM_G instead of decoding source images twice.
uint16_t ring_background_entries[2][RING_BG_COLS];
struct RingCopyCommit {uint64_t id;uint16_t slot,entry;uint8_t bank;};
struct RingCopyQueue {
    // The renderer has one synchronous frame producer, like its DL buffer.
    // Reuse scratch without adding another KiB to the nested window stack.
    static uint32_t commands[128];static RingCopyCommit commits[32];static unsigned count;
    static HMM_SIZE_RENDER void discard()noexcept{
        for(unsigned i=0;i<count;++i){const unsigned entry=commits[i].entry;if(!--map_texture_refs[entry])textures.entries[entry].pins&=~4u;}
        count=0;
    }
    ~RingCopyQueue(){discard();}
    static HMM_SIZE_RENDER void flush();
    static HMM_SIZE_RENDER bool copy(uint64_t,unsigned,unsigned,uint32_t);
};
uint32_t RingCopyQueue::commands[128];RingCopyCommit RingCopyQueue::commits[32];unsigned RingCopyQueue::count;
void RingCopyQueue::flush(){
    if(!count)return;
    GpuBusy busy;copro_send(commands,count*4);copro_wait();
    for(unsigned i=0;i<count;++i){const auto &c=commits[i];ring_background_ids[c.bank][c.slot]=c.id;}
    discard();
}
bool RingCopyQueue::copy(uint64_t key,unsigned bank,unsigned slot,uint32_t target){
    const unsigned other=bank^1u,col=slot/RING_BG_ROWS;
    unsigned source=slot;
    if(ring_background_ids[other][source]!=key){
        if(!(key>>63))return false;
        // A fully opaque image can also be copied from another resident row
        // of the same column. The source bank remains immutable during build.
        for(source=col*RING_BG_ROWS;source<(col+1)*RING_BG_ROWS;++source)
            if(ring_background_ids[other][source]==key)break;
        if(source==(col+1)*RING_BG_ROWS)return false;
    }
    const unsigned entry=ring_background_entries[other][col];auto &e=textures.entries[entry];
    if(!e.valid||e.key!=(UINT64_C(0xd000000000000000)|(other*RING_BG_COLS+col))||e.bytes!=RING_BG_BYTES)return false;
    if(count==32)flush();
    // Source pages remain resident until the queued MEMCPY has completed.
    ++map_texture_refs[entry];e.pins|=4u;e.age=++textures.age;
    uint32_t *p=commands+count*4;
    p[0]=0xffffff1du;p[1]=target;p[2]=e.page*HFT_PAGE_BYTES+(source&15)*1024;p[3]=1024;
    commits[count++]={key,uint16_t(slot),uint16_t(entry),uint8_t(bank)};
    return true;
}
