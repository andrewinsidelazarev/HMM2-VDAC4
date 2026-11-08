/* Codex: сравниваем все поля таблиц монстров и артефактов, без адресов и padding. */
#include "monster.h"
#include "monster_info.h"
#include "artifact.h"
#include "artifact_info.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
static FILE *output;
template<class T> static void value(T x){if(std::fwrite(&x,sizeof(x),1,output)!=1)std::abort();}
static void text(const char *s){uint32_t n=s?uint32_t(std::strlen(s)):UINT32_MAX;value(n);if(s)std::fwrite(s,1,n,output);}
int main(int argc,char **argv){
    if(argc!=2||(output=std::fopen(argv[1],"wb"))==nullptr)return 2;
    for(int i=0;i<Monster::MONSTER_COUNT;i++){
        const auto &m=fheroes2::getMonsterData(i);value(i);value(m.icnId);text(m.binFileName);
        const auto &s=m.sounds;value(s.meleeAttack);value(s.death);value(s.movement);value(s.wince);value(s.rangeAttack);value(s.takeoff);value(s.landing);value(s.explosion);
        const auto &b=m.battleStats;value(b.attack);value(b.defense);value(b.damageMin);value(b.damageMax);value(b.hp);value(b.speed);value(b.shots);value(b.monsterBaseStrength);
        value(uint32_t(b.abilities.size()));for(const auto &a:b.abilities){value(a.type);value(a.percentage);value(a.value);}
        value(uint32_t(b.weaknesses.size()));for(const auto &a:b.weaknesses){value(a.type);value(a.percentage);value(a.value);}
        const auto &g=m.generalStats;text(g.untranslatedName);text(g.untranslatedPluralName);value(g.baseGrowth);value(g.race);value(g.level);
        const auto &c=g.cost;value(c.gold);value(c.wood);value(c.mercury);value(c.ore);value(c.sulfur);value(c.crystal);value(c.gems);
    }
    for(int i=0;i<Artifact::ARTIFACT_COUNT;i++){
        const auto &a=fheroes2::getArtifactData(i);value(i);text(a.untranslatedName);text(a.untranslatedBaseDescription);text(a.untranslatedDiscoveryEventDescription);
        value(uint32_t(a.bonuses.size()));for(const auto &b:a.bonuses){value(b.type);value(b.value);}
        value(uint32_t(a.curses.size()));for(const auto &b:a.curses){value(b.type);value(b.value);}
    }
    std::fclose(output);std::printf("monsters=%d artifacts=%d\n",Monster::MONSTER_COUNT,Artifact::ARTIFACT_COUNT);
}
