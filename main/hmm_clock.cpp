/* Codex: Gluk RTC Ево; год регистра09 относится к 2000..2099, ячейки NVRAM не трогаем.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_clock.h"
#include "hmm_platform.h"
#include <cstdio>
namespace {
uint32_t epoch;uint64_t at_ms;bool synchronized;
int number(uint8_t v,bool binary){if(binary)return v;if((v&15)>9||(v>>4)>9)return -1;return (v>>4)*10+(v&15);}
bool leap(unsigned year){return year%4==0&&(year%100!=0||year%400==0);}
}
extern "C" void hmm_clock_reset(){epoch=0;at_ms=0;synchronized=false;}
extern "C" int hmm_clock_sync(const uint8_t *p,size_t n){
    // Codex: B,сек,мин,час,день,месяц,год,D. Передаётся до запуска main, без гонки чтения.
    /* Клод: признаки VRT (регистр D, бит 7) и SET (регистр B, бит 7) не требуются: на плате ELF отвергал время Evo («RTC Evo invalid»),
     * time() оставался 0 и сохранения шли с нулевой датой (пользователь, 03.11). Время проверяется по самим полям ниже. Сырые байты — в журнал. */
    if(n==8){char m[96];std::snprintf(m,sizeof(m),"RTC Evo bytes B=%02x s=%02x m=%02x h=%02x d=%02x mo=%02x y=%02x D=%02x",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);hmm_log(m);}
    if(n!=8)return 0;
    const bool binary=p[0]&4,day24=p[0]&2;const int sec=number(p[1],binary),min=number(p[2],binary),hour=number(p[3]&127,binary),day=number(p[4],binary),month=number(p[5],binary),year=number(p[6],binary);
    if(sec<0||sec>59||min<0||min>59||day<1||month<1||month>12||year<0||year>99||hour<0||hour>(day24?23:12)||(!day24&&!hour)||(day24&&(p[3]&128)))return 0;
    const unsigned full=2000+unsigned(year);static const uint8_t days_in_month[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(day>days_in_month[month-1]+(month==2&&leap(full)))return 0;
    unsigned days=10957;for(unsigned y=2000;y<full;++y)days+=365+leap(y);
    for(int m=1;m<month;++m)days+=days_in_month[m-1]+(m==2&&leap(full));days+=day-1;
    const unsigned h=day24?unsigned(hour):unsigned(hour%12)+((p[3]&128)?12:0);
    epoch=((days*24+h)*60+unsigned(min))*60+unsigned(sec);at_ms=hmm_millis();synchronized=true;
    hmm_log("RTC Evo synchronized");return 1;
}
extern "C" uint64_t hmm_wall_seconds(){return synchronized?uint64_t(epoch)+(hmm_millis()-at_ms)/1000:0;}
extern "C" time_t hmm_time(time_t *out){const time_t result=time_t(hmm_wall_seconds());if(out)*out=result;return result;}
