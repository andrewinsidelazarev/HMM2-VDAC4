// Codex: редкая IDLE-фаза могла впервые появиться через четыре минуты боя.
// Читаем исходные списки BIN заранее; Rand, состояние войска и таймеры не меняются.
#include "bin_info.h"
#include "hmm_resources.h"
#ifdef HMM2_HOST
#include <cstdio>
#endif
// Codex: CPU-представление нужно лишь войскам под программными эффектами.
// Первый реальный запрос группы подготавливает остальные фазы в той же арене.
static struct {uint16_t group,id;} hmm_battle_groups[10];
static void hmm_warm_frames(unsigned id,unsigned group,unsigned cpu){
    const auto info=Bin_Info::GetMonsterInfo(id);
    for(unsigned kind=Bin_Info::MonsterAnimInfo::STATIC;kind<Bin_Info::MonsterAnimInfo::IDLE1+info.idleAnimationCount;++kind)
        if(info.hasAnim(kind))for(int frame:info.animationFrames[kind])hmm_gpu_warm_sprite((cpu<<30)|(group<<16)|uint32_t(frame));
}
void hmm_warm_battle_group(unsigned group,bool effect){
    if(!hmm_resource_warm_allowed())return;
    for(auto &v:hmm_battle_groups)if(v.group==group&&v.id){v.id|=uint16_t(effect?0x8000:0xc000);break;}
}
// Codex: предзагрузка начинается после завершения примитива. Иначе все её фазы
// закреплялись одной внешней эпохой вместе с ещё используемыми пикселями карты.
void hmm_warm_battle_pending(){
    for(auto &v:hmm_battle_groups)if(v.id&0x8000){const unsigned id=v.id&0x3fff,cpu=(v.id&0x4000)?2u:1u;v.id=0;hmm_warm_frames(id,v.group,cpu);}
}
static void hmm_warm_battle(const Battle::Force &first,const Battle::Force &second){
    for(auto &v:hmm_battle_groups)v={0,0};
    if(!hmm_resource_warm_allowed())return;
#ifdef HMM2_HOST
    std::puts("WARM IDLE BEGIN");
#endif
    bool seen[Monster::WATER_ELEMENT+1]{};unsigned count=0;
    for(const auto *force:{&first,&second})for(const auto *unit:force->getUnits()){
        const int id=unit->GetID();if(id<Monster::PEASANT||id>Monster::WATER_ELEMENT||seen[id])continue;seen[id]=true;
        const unsigned group=unit->GetMonsterSprite();
        if(count<10)hmm_battle_groups[count++]={uint16_t(group),uint16_t(id)};
        hmm_warm_frames(id,group,0);
    }
#ifdef HMM2_HOST
    std::puts("WARM IDLE END");
#endif
}
