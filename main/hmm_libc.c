/* Codex: недостающая часть libc для однопоточного игрового движка, без системных файлов и локалей.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
#include "hmm_core.h"
#include "hmm_clock.h"
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static int local_errno;
int *__errno(void){return &local_errno;}
uint64_t hmm_millis(void){static uint32_t last;static uint64_t high;uint32_t t=hmm_core->millis();if(t<last)high+=UINT64_C(1)<<32;last=t;return high+t;}
/* Клод: сон игры (SDL_Delay, delayforMs) суммируется в hmm_slept_ms (hmm_video.c): строка HMM2 PERF отделяет счёт процессора
 * от ожидания таймеров игры. */
void hmm_delay(uint32_t ms){hmm_slept_ms+=ms;hmm_core->delay(ms);}
void hmm_log(const char *s){while(*s){char part[121];unsigned n=0;while(*s&&n<120)part[n++]=*s++;part[n]=0;hmm_core->log("%s",part);}}
int clock_gettime(clockid_t id,struct timespec*t){uint64_t ms=hmm_millis();t->tv_sec=id==CLOCK_REALTIME?(time_t)hmm_wall_seconds():(time_t)(ms/1000);t->tv_nsec=(long)((ms%1000)*1000000);return 0;}
/* Codex: календарь приходит от RTC Ево; uptime не используется как дата сохранения. */
time_t time(time_t *out){return hmm_time(out);}
int tolower(int c){return c>='A'&&c<='Z'?c+32:c;}
int toupper(int c){return c>='a'&&c<='z'?c-32:c;}
int isspace(int c){return c==' '||(c>='\t'&&c<='\r');}
int isprint(int c){return c>=32&&c<127;}
int isalnum(int c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
long strtol(const char *s,char **end,int base){
    const char *begin=s;while(isspace((unsigned char)*s))++s;int neg=0;if(*s=='+'||*s=='-')neg=*s++=='-';
    if(base<0||base==1||base>36){local_errno=EINVAL;if(end)*end=(char*)begin;return 0;}
    if((!base||base==16)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')&&((s[2]>='0'&&s[2]<='9')||(tolower(s[2])>='a'&&tolower(s[2])<='f'))){s+=2;base=16;}
    if(!base)base=*s=='0'?8:10;
    const char *digits=s;unsigned long value=0,limit=neg?(unsigned long)LONG_MAX+1:(unsigned long)LONG_MAX;int overflow=0;
    for(;;s++){unsigned c=(unsigned char)*s;unsigned n=c>='0'&&c<='9'?c-'0':tolower((int)c)>='a'&&tolower((int)c)<='z'?(unsigned)tolower((int)c)-'a'+10:36;
        if(n>=(unsigned)base)break;if(value>(limit-n)/(unsigned)base)overflow=1;else value=value*(unsigned)base+n;}
    if(end)*end=(char*)(s==digits?begin:s);if(overflow){local_errno=ERANGE;return neg?LONG_MIN:LONG_MAX;}
    return neg?(long)(0ul-value):(long)value;
}
double round(double x){if(!isfinite(x))return x;double a=x<0?-x:x,b=floor(a);if(a-b>=0.5)b+=1;return x<0?-b:b;}
long lround(double x){return (long)round(x);}
double hypot(double x,double y){x=fabs(x);y=fabs(y);if(isinf(x)||isinf(y))return INFINITY;if(x<y){double z=x;x=y;y=z;}if(!x)return 0;double r=y/x;return x*sqrt(1+r*r);}
unsigned int __popcountsi2(unsigned int x){x=x-((x>>1)&0x55555555u);x=(x&0x33333333u)+((x>>2)&0x33333333u);x=(x+(x>>4))&0x0f0f0f0fu;return (x*0x01010101u)>>24;}
size_t strftime(char *out,size_t size,const char *fmt,const struct tm*t){
    static const char months[]="JanFebMarAprMayJunJulAugSepOctNovDec";size_t n=0;
    while(*fmt){char tmp[20];const char*s=tmp;size_t len;
        if(*fmt!='%'){tmp[0]=*fmt++;tmp[1]=0;}
        else{++fmt;char c=*fmt?*fmt++:0;int v=0,w=2;
            switch(c){case 'b':if(t->tm_mon<0||t->tm_mon>11)return 0;memcpy(tmp,months+t->tm_mon*3,3);tmp[3]=0;break;
            case '%':strcpy(tmp,"%");break;
            default:switch(c){case 'd':v=t->tm_mday;break;case 'm':v=t->tm_mon+1;break;case 'Y':v=t->tm_year+1900;w=4;break;case 'y':v=(t->tm_year+1900)%100;break;case 'H':v=t->tm_hour;break;case 'M':v=t->tm_min;break;case 'S':v=t->tm_sec;break;default:return 0;}snprintf(tmp,sizeof(tmp),"%0*d",w,v);break;}
        }
        len=strlen(s);if(n+len>=size)return 0;memcpy(out+n,s,len);n+=len;
    }if(!size)return 0;out[n]=0;return n;
}
