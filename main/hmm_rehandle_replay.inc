/* Codex: плотный DL сохраняет назначение ручек между кадрами. При движении меняются
 * вершины и несколько текстур, но заново сортировать использования 32 ручек не нужно.
 * Подсказка не является кэшем состояния FT812: все пять полей каждой вершины берутся
 * из текущего входа и точно восстанавливаются перед рисованием. Неудачная подсказка
 * оставляет вход целым и передаёт его полному планировщику. */
HMM_RENDER_NOINLINE bool rehandle_replay(){
    if(rehandle_route.empty())return false;
    struct Replay {uint32_t in[32][5]{},out[32][5]{};uint8_t known[32]{},written[32]{};};
    std::unique_ptr<Replay> work(new(std::nothrow) Replay);
    if(!work)return false;
    // Codex: максимум 8 КиБ, только на время прохода; больших кусков PSRAM нет.
    const unsigned limit=std::min(2000u,std::min(words-1,rehandle_words+32));
    std::vector<uint32_t> result;
    try{result.reserve(limit);}catch(...){return false;}
    unsigned cur=0,out_cur=0,prim=0,v=0;
    for(unsigned i=0;i<words;++i){const uint32_t w=dl[i];const unsigned op=w>>24;
        if((w>>30)==2&&prim==1){
            const unsigned from=(w>>7)&31;
            if(work->known[from]!=31||v>=rehandle_route.size())return false;
            const unsigned to=rehandle_route[v++];
            unsigned changes=0;for(unsigned f=0;f<5;++f)
                if(!(work->written[to]&(1u<<f))||work->out[to][f]!=work->in[from][f])changes|=1u<<f;
            if(result.size()+7>limit)return false;
            if(changes){
                if(to!=out_cur){result.push_back(0x05000000|to);out_cur=to;}
                for(unsigned f=0;f<5;++f)if(changes&(1u<<f)){
                    const uint32_t field=work->in[from][f];result.push_back(field);work->out[to][f]=field;}
                work->written[to]=31;
            }
            result.push_back((w&~(31u<<7))|(to<<7));continue;
        }
        if((w>>30)==1&&prim==1)return false;
        if((w>>30)==0){
            if(op==5){cur=w&31;continue;}
            const int f=bitmap_field(w);if(f>=0){work->in[cur][f]=w;work->known[cur]|=1u<<f;continue;}
            if(op==0x1f)prim=w&15;
            else if(op==0x1d||op==0x1e||op==0x22||op==0x23||op==0x24||op==0x25)return false;
        }
        if(result.size()==limit)return false;result.push_back(w);
    }
    if(v!=rehandle_route.size())return false;
    words=unsigned(result.size());std::memcpy(dl,result.data(),words*4);
    std::memcpy(compact_mask,work->written,sizeof(compact_mask));
    std::memcpy(compact_bitmap,work->out,sizeof(compact_bitmap));
    compact_handle=out_cur;compact_valid[5]=1;compact_state[5]=0x05000000|out_cur;
    return true;
}
HMM_RENDER_NOINLINE bool rehandle_dl(){
    Prof prof(P_REHANDLE);
#ifdef HMM2_HOST
    static const bool off=std::getenv("HMM2_NO_REHANDLE")!=nullptr;
    if(off)return false;
    const std::vector<uint32_t> before(dl,dl+words);
    static const bool no_replay=std::getenv("HMM2_NO_REHANDLE_REPLAY")!=nullptr;
#else
    constexpr bool no_replay=false;
#endif
    bool ok=!rehandle_force_full&&!no_replay&&rehandle_replay();
    rehandle_fast_used|=ok;
    if(ok)++stat_handles_reuse;
    if(!ok){
        ++stat_handles_full;
        ok=rehandle_dl_full();rehandle_route.clear();
        if(ok){
            unsigned prim=0;
            try{for(unsigned i=0;i<words;++i){const uint32_t w=dl[i];
                if((w>>24)==0x1f)prim=w&15;
                else if((w>>30)==2&&prim==1)rehandle_route.push_back(uint8_t((w>>7)&31));}}
            catch(...){rehandle_route.clear();}
            rehandle_words=words;
        }
    }
#ifdef HMM2_HOST
    // Codex: проверка полного состояния каждой вершины действует и для повторного
    // плана. Изменение любых небитмапных команд или их порядка также обнаруживается.
    if(ok){
        auto resolve=[](const uint32_t *p,unsigned count){std::vector<uint32_t> r;uint32_t fields[32][5]{};unsigned h=0,prim=0;
            for(unsigned i=0;i<count;++i){const uint32_t w=p[i];const unsigned op=w>>24;
                if((w>>30)==2&&prim==1){const unsigned v=(w>>7)&31;r.insert(r.end(),fields[v],fields[v]+5);r.push_back(w&~(31u<<7));continue;}
                if((w>>30)==0&&op==5){h=w&31;continue;}
                const int f=(w>>30)==0?bitmap_field(w):-1;if(f>=0){fields[h][f]=w;continue;}
                if((w>>30)==0&&op==0x1f)prim=w&15;r.push_back(w);}
            return r;};
        if(resolve(before.data(),unsigned(before.size()))!=resolve(dl,words))hmm_fail("rehandle replay changed the display list");
    }
#endif
    return ok;
}
