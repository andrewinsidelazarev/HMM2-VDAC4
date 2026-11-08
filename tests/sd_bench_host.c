/* Codex: исполняем код модуля на ПК; часы искусственные, это проверка данных и тайм-аутов. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "zm_app.h"
extern int sd_module_entry(int,char**);
static unsigned now_ms,attempts,queued,reply_state;
static uint8_t event_data[8];
static void logline(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);putchar('\n');va_end(a);}
static uint32_t clock_ms(void){return now_ms;}
static void *make_lock(void){return (void*)1;}
static void no_lock(void*p){(void)p;}
static int event(uint8_t c,const void*p,uint16_t n,uint32_t timeout){
    (void)timeout;if(c!=0xe4||n!=8)abort();
    if(++attempts%73==0)return ZM_BUSY;
    if(queued)abort();memcpy(event_data,p,8);queued=1;return ZM_OK;
}
static int reply(uint8_t c,const void*p,uint16_t n){if(c==0xf3){if(n!=6)abort();reply_state=*(const uint8_t*)p;}return ZM_OK;}
int main(int argc,char**argv){
    if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 3;fseek(f,0,SEEK_END);unsigned size=ftell(f);
    static zm_core_t c;memset(&c,0,sizeof(c));c.size=sizeof(c);c.abi_major=1;c.log=logline;c.millis=clock_ms;c.mutex_create=make_lock;c.mutex_lock=no_lock;c.mutex_unlock=no_lock;c.event=event;c.reply=reply;
    const zm_app_t *a=0;char*args[]={"HSD.ELF",(char*)&c,(char*)&a};if(sd_module_entry(3,args)!=1||!a||a->start(&c))return 4;
    uint8_t config[17]={0};memcpy(config,&size,4);memcpy(config+4,"HEROES2.AGG",12);a->command(0x70,config,16);
    unsigned completed=0;
    for(unsigned i=0;i<100000;i++){
        a->poll(now_ms++);
        if(queued){
            unsigned at=event_data[2]|event_data[3]<<8|event_data[4]<<16|event_data[5]<<24;
            unsigned n=event_data[6]|event_data[7]<<8;if(n>1016||at>size||n>size-at)return 5;
            uint8_t response[1020];memcpy(response,event_data,2);response[2]=n;response[3]=n>>8;
            fseek(f,at,SEEK_SET);if(fread(response+4,1,n,f)!=n)return 6;
            queued=0;now_ms+=3;
            if(!completed){response[0]^=0x80;a->command(0x71,response,n+4);response[0]^=0x80;}
            a->command(0x71,response,n+4);completed++;
        }
        a->command(0x73,0,0);if(reply_state==3)break;if(reply_state==255)return 7;
    }
    if(reply_state!=3||completed!=(size+1015)/1016+2048)return 8;a->stop();fclose(f);
    /* Codex: отказ SD и отсутствие ответа обязаны завершиться ошибкой. */
    for(unsigned test=0;test<2;test++){
        a->start(&c);a->command(0x70,config,16);while(!queued)a->poll(now_ms++);queued=0;
        if(test){now_ms+=30001;a->poll(now_ms);}else{uint8_t short_reply[4]={event_data[0],event_data[1],0,0};a->command(0x71,short_reply,4);}
        a->command(0x73,0,0);if(reply_state!=255)return 9;a->stop();
    }
    puts("HOST: данные, обратное давление, старый seq, короткий ответ и тайм-аут проверены");return 0;
}
