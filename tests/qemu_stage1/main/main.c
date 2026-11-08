#include "test_paths.h"
/* Codex: настоящий Xtensa ELF и неизменённый загрузчик; SD и FT812 обслуживает стенд.
 * Стенд не измеряет скорость платы и никогда не прошивается. */
#include "zm_app.h"
#include "zm_source.h"
#include "esp_elf.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_debug_helpers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

extern const uint8_t module_start[] asm("_binary_HMM2_ELF_start");
extern const uint8_t module_end[] asm("_binary_HMM2_ELF_end");
extern const uint8_t ogg_start[] asm("_binary_OGG_ELF_start");
extern const uint8_t ogg_end[] asm("_binary_OGG_ELF_end");
extern char __adddf3,__subdf3,__muldf3,__divdi3,__udivdi3,__moddi3,__umoddi3;
/* Codex: эти libgcc-символы уже экспортирует zm_symbols.cpp настоящего ядра. */
extern char __ashldi3,__ashrdi3,__lshrdi3;
extern char __extendsfdf2,__fixdfsi,__floatdidf,__floatsidf,__divsf3,__divdf3,__floatunsidf,__ledf2,__truncdfsf2;
static const zm_app_t *app;
static long package,trace,sd_journal=-1,read_journal=-1;
static unsigned frames,reads,read_bytes,reply_bytes;static uint8_t response[1024];
/* Codex: DMA-ответ не занимает стек задачи игры. */
static uint8_t sd_answer[8192];
static uint32_t frame_limit=30;static unsigned testing;
static uint32_t finish_after,scenario_started,file_read_bytes,file_write_bytes;static volatile unsigned capture_done;
static uint8_t ui_keys[128]={0x31,0x1b,0x5a,0x5a,0x5a,0x5a,0x2c,0x5a};static unsigned key_count=8;
static uint32_t ui_times[128]={2500,5000,7500,10000,16000,22000,30000,35000,40000,45000,50000,55000};
static uint32_t package_size,sd_latency[2];
static unsigned exit_test;static volatile unsigned task_finished,exit_loading_active;
static uint32_t ft_fault_after;static unsigned ft_fault_started;
static uint32_t cmd_torn_after,cmd_torn_words;static unsigned cmd_torn_done,cmd_cursor_resumed;
static uint32_t alloc_fault_after,alloc_fault_min;static unsigned alloc_fault_started;
static int is_game_task(void);
static SemaphoreHandle_t trace_lock;
static uint8_t *catalogue_override;static unsigned catalogue_bytes,catalogue_offset;
/* Codex: накладные 16 байт и PSRAM соответствуют AllocHeader штатного трекера VDAC4. */
static void *app_alloc(size_t n,uint32_t caps){(void)caps;
    if(alloc_fault_after&&frames>=alloc_fault_after&&n>=alloc_fault_min&&is_game_task()){
        if(!alloc_fault_started){alloc_fault_started=1;printf("QEMU: injected allocation failure frame=%u request=%u\n",frames,(unsigned)n);fflush(stdout);}
        return NULL;
    }
    uint32_t*p=heap_caps_malloc(n+16,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p){printf("QEMU: OOM запрос=%u свободно=%u блок=%u\n",(unsigned)n,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));esp_backtrace_print(10);fflush(stdout);return NULL;}p[0]=n;return p+4;}
static void app_free(void*p){if(p)heap_caps_free((uint32_t*)p-4);}
static void *app_realloc(void*p,size_t n){if(!p)return app_alloc(n,0);if(!n){app_free(p);return NULL;}void*q=app_alloc(n,0);if(q){size_t old=((uint32_t*)p)[-4];memcpy(q,p,n<old?n:old);app_free(p);}return q;}
/* Codex: semihosting существует только в стенде, не импортируется модулем игры. */
static long simcall(long call,long p1,long p2,long p3){
    register long a2 asm("a2")=call;register long a3 asm("a3")=p1;
    register long a4 asm("a4")=p2;register long a5 asm("a5")=p3;
    asm volatile("simcall":"+r"(a2),"+r"(a3):"r"(a4),"r"(a5):"memory");return a2;
}
/* Codex: SIMCALL QEMU передаёт флаги open прямо хосту Windows, включая O_BINARY. */
static long host_open(const char *path,int mode){return simcall(5,(long)path,0x8000|(mode==5?0x301:mode==3?2:0),0666);}
static void host_close(long f){simcall(6,f,0,0);}
static int host_seek(long f,uint32_t at){return simcall(19,f,at,0);}
static unsigned host_read(long f,void *p,unsigned n){long r=simcall(3,f,(long)p,n);return r<0?0:r;}
static void host_write(long f,const void *p,unsigned n){if(simcall(4,f,(long)p,n)!=n)abort();}
static uint32_t host_size(long f){long at=simcall(19,f,0,1),size=simcall(19,f,0,2);simcall(19,f,at,0);return size;}
/* Клод: журнал чтений DAT (reads.log): «T вид смещение байт» на каждый запрос E4 (R) и E6 (G) — разбор по файлам
 * (окно найма читало на плате 8 МБ за 100 кадров). Только стенд; ELF не меняется. */
static void journal_read(char kind,unsigned at,unsigned bytes){
    if(read_journal<0)return;
    char text[64];int n=snprintf(text,sizeof(text),"%lu %c %u %u\n",(unsigned long)(esp_timer_get_time()/1000),kind,at,bytes);
    if(n>0)host_write(read_journal,text,(unsigned)n);
}
/* Codex: воспроизводимый мир только в стенде. Смещения получены из nm именно
 * встроенного ELF. Вызывается исходный getter PCG32, алгоритм игры не заменяется.
 * Состояние соответствует исходному конструктору PCG32(seed, 54). Сброс перед
 * World::Reset/Defaults исключает зависимость мира от скорости меню и аллокатора. */
static struct {uint32_t magic;int32_t getter_delta,data_delta;uint32_t seed;uint64_t state,stream;} rng_fixture;
static unsigned rng_ready,rng_resets;
static void reset_world_rng(void){
    void *(*getter)(void)=(void *(*)(void))((uintptr_t)app->command+rng_fixture.getter_delta);
    void *generator=getter();
    if((intptr_t)generator-(intptr_t)app!=rng_fixture.data_delta)abort();
    memcpy(generator,&rng_fixture.state,16);
    printf("QEMU: RNG world_reset=%u seed=%lu state=%llu stream=%llu\n",++rng_resets,
        (unsigned long)rng_fixture.seed,(unsigned long long)rng_fixture.state,(unsigned long long)rng_fixture.stream);
}
static void logline(const char*f,...){
    va_list a;va_start(a,f);
    if(rng_ready&&!strcmp(f,"%s")){va_list copy;va_copy(copy,a);const char *text=va_arg(copy,const char*);if(!strncmp(text,"HMM2 MEM load:",14))reset_world_rng();va_end(copy);}
    // Codex: тест сброса ждёт настоящий вход в LOAD, а не прежнее время без музыки.
    if(exit_test>=3&&!strcmp(f,"%s")){va_list copy;va_copy(copy,a);const char *text=va_arg(copy,const char*);if(!strcmp(text,"LOAD CURSOR begin"))exit_loading_active=1;else if(!strncmp(text,"LOAD CURSOR end",15))exit_loading_active=0;va_end(copy);}
    /* Codex: десятки тысяч строк SD пишем через semihosting, чтобы эмулированный
     * UART не сдвигал сценарные нажатия на минуты. Все строки сохранены; ELF
     * и его вызовы core->log те же. Тайминги SPI проверяет совместный стенд. */
    if(sd_journal>=0&&!strncmp(f,"HMM2 SD",7)){
        char text[192];int n=vsnprintf(text,sizeof(text)-1,f,a);if(n<0)n=0;if(n>190)n=190;
        text[n++]='\n';host_write(sd_journal,text,(unsigned)n);
    }else{
        /* Клод: виртуальное время смены музыки: по нему считается длина хода ИИ (-icount, 1 мс = 1 млн команд). */
        if(!strncmp(f,"AUDIO track",11))printf("T=%lu ",(unsigned long)(esp_timer_get_time()/1000));
        vprintf(f,a);putchar('\n');fflush(stdout);
    }
    va_end(a);
}
static void *mutex_create(void){return xSemaphoreCreateMutex();}
static void mutex_lock(void*m){xSemaphoreTake(m,portMAX_DELAY);}
static void mutex_unlock(void*m){xSemaphoreGive(m);}
static void *signal_create(void){return xSemaphoreCreateBinary();}
static void signal_give(void*s){xSemaphoreGive(s);}
static int signal_wait(void*s,uint32_t ms){return xSemaphoreTake(s,pdMS_TO_TICKS(ms))==pdTRUE?ZM_OK:ZM_TIMEOUT;}
static void delay(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms)?pdMS_TO_TICKS(ms):1);}
static uint32_t millis(void){return (uint32_t)(esp_timer_get_time()/1000);}

static TaskHandle_t game_handle,audio_handle,worker_handle,cursor_handle;static uint32_t worker_stack,cursor_stack,game_stack,internal_limited=1,psram_budget=7340032;
static int is_game_task(void){return game_handle&&xTaskGetCurrentTaskHandle()==game_handle;}
/* Codex: оставляем два раздельных блока внутренней RAM; это только стенд.
 * Крупнейший соответствует плате, суммарный остаток немного строже её 49 843.
 * Удерживаемые блоки живут до завершения QEMU, в игру этот код не входит. */
static void *internal_reserve[1024];static unsigned internal_reserve_count;
static void limit_internal(void){
    const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    void *holes[2]={0};
    for(unsigned step=0;step<3;step++){
        unsigned size=step==0?24576:step==1?1024:16;
        for(;;){
            void *p=heap_caps_malloc(size,caps);if(!p)break;
            if(internal_reserve_count==1024){abort();}
            internal_reserve[internal_reserve_count++]=p;
            if(!step){
                if(!holes[0]||(uintptr_t)p<(uintptr_t)holes[0])holes[0]=p;
                if(!holes[1]||(uintptr_t)p>(uintptr_t)holes[1])holes[1]=p;
            }
        }
    }
    if(!holes[0]||(uintptr_t)holes[1]-(uintptr_t)holes[0]<49152)abort();
    for(unsigned i=0;i<2;i++)heap_caps_free(holes[i]);
    unsigned free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    logline("QEMU: внутренняя RAM ограничена free=%u largest=%u",free,largest);
    if(free>49843||free<49000||largest>24576||largest<24000)abort();
}
/* Codex: ABI прошивки даёт8слотов; игра, PCM и чистая математика графики — отдельные задачи. */
static struct {zm_task_fn fn;void *arg;int game;} task_slots[8];static unsigned task_count;
static void task_run(void*p){unsigned slot=(unsigned)(uintptr_t)p;task_slots[slot].fn(task_slots[slot].arg);if(task_slots[slot].game)task_finished=1;vTaskDelete(NULL);}
static int task_start(zm_task_fn fn,void*arg,uint32_t stack,uint8_t priority,int8_t cpu){
    if(task_count>=8){return ZM_BUSY;}unsigned slot=task_count++;task_slots[slot].fn=fn;task_slots[slot].arg=arg;task_slots[slot].game=slot==0;
    if(!slot){game_stack=stack;if(internal_limited)limit_internal();}
    const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;unsigned before=heap_caps_get_free_size(caps);TaskHandle_t handle;
    const int ok=xTaskCreatePinnedToCore(task_run,slot?(priority==5?"audio":"graphic-hash"):"hmm2",stack,(void*)(uintptr_t)slot,priority,&handle,cpu)==pdPASS;
    if(ok&&!slot)game_handle=handle;else if(ok&&priority==5)audio_handle=handle;
    else if(ok&&priority==3){worker_handle=handle;worker_stack=stack;}
    else if(ok&&priority==4){cursor_handle=handle;cursor_stack=stack;}
    logline("QEMU: task_start stack=%u ok=%d internal_before=%u internal_after=%u largest=%u priority=%u core=%d",stack,ok,before,(unsigned)heap_caps_get_free_size(caps),(unsigned)heap_caps_get_largest_free_block(caps),priority,cpu);return ok?ZM_OK:ZM_ERROR;
}
static esp_elf_t ogg_elf;static int ogg_loaded;
static int module_open(const char*name,void*arg1,void*arg2,void**handle,int*entry){
    if(strcmp(name,"OGG.ELF")||ogg_loaded)return ZM_NOT_FOUND;
    int result=esp_elf_init(&ogg_elf);size_t n=ogg_end-ogg_start;void *copy=app_alloc(n,ZM_MEM_PSRAM);if(!copy)return ZM_NO_MEMORY;memcpy(copy,ogg_start,n);
    if(!result){result=esp_elf_relocate(&ogg_elf,copy);}app_free(copy);if(result){logline("QEMU: OGG relocate=%d",result);return ZM_ERROR;}
    char *args[]={"OGG.ELF",arg1,arg2};*entry=ogg_elf.entry(3,args);*handle=&ogg_elf;ogg_loaded=1;logline("QEMU: OGG loaded entry=%d",*entry);return ZM_OK;
}
static void module_close(void *handle){if(handle==&ogg_elf&&ogg_loaded){esp_elf_deinit(&ogg_elf);ogg_loaded=0;logline("QEMU: OGG unloaded");}}
static uint32_t pcm_gaps,pcm_max_gap;static uint64_t pcm_until;static uint32_t pcm_rate,pcm_frames,pcm_nonzero,pcm_report;static float pcm_peak;
static int pcm_open(const zm_pcm_format_t*f){pcm_rate=f->rate;pcm_until=0;logline("QEMU: PCM open %u/%u",f->rate,f->channels);return f->channels==2?ZM_OK:ZM_ERROR;}
static uint32_t pcm_queued(void){uint64_t now=(uint64_t)millis()*pcm_rate/1000;return pcm_until>now?(pcm_until-now)*1000/pcm_rate:0;}
static int pcm_write(const float *p,uint32_t n){if(pcm_queued()>250)return 0;uint64_t now=(uint64_t)millis()*pcm_rate/1000;if(pcm_until<now){if(pcm_until){unsigned gap=now-pcm_until;++pcm_gaps;if(gap>pcm_max_gap)pcm_max_gap=gap;/* Клод: каждый обрыв звука дольше 10 мс — со временем и кадром, чтобы отличать обрывы в игре от пауз загрузки. */if(gap>pcm_rate/100)logline("QEMU: PCM gap %u ms at T=%lu frame=%u",(unsigned)(gap*1000/pcm_rate),(unsigned long)millis(),frames);}pcm_until=now;}pcm_until+=n;pcm_frames+=n;
    for(unsigned i=0;i<n*2;i++){float v=fabsf(p[i]);if(!isfinite(v)||v>1.00001f)abort();if(v>0.0001f)++pcm_nonzero;if(v>pcm_peak)pcm_peak=v;}if(pcm_frames/8192>pcm_report){pcm_report=pcm_frames/8192;logline("QEMU: PCM frames=%u nonzero=%u peak=%f queued=%u gaps=%u max_gap_samples=%u",pcm_frames,pcm_nonzero,pcm_peak,pcm_queued(),pcm_gaps,pcm_max_gap);}return n;}
static void pcm_flush(void){pcm_until=0;}static void pcm_close(void){logline("QEMU: PCM close frames=%u nonzero=%u peak=%f",pcm_frames,pcm_nonzero,pcm_peak);}
/* Клод: поток записи op 8 (сохранения Codex): на один 76 ESP отвечает потоком F6 [seq, 1, смещение, число, данные] — пишем прямо
 * в файл стенда, проверяя смещения подряд; stream_file<0 — прежний одиночный ответ. */
static long stream_file=-1;static unsigned stream_done;static int stream_bad;
static int reply(uint8_t code,const void *data,uint16_t n){
    const uint8_t *p=data;
    if(code==0xf6&&stream_file>=0){
        if(n<7||!p[2]||(unsigned)(p[3]|p[4]<<8)!=stream_done||(unsigned)(p[5]|p[6]<<8)!=n-7u){stream_bad=1;return ZM_OK;}
        host_write(stream_file,p+7,n-7);file_write_bytes+=n-7;stream_done+=n-7;return ZM_OK;
    }
    if(code==0xf6||code==0xf3){memcpy(response,data,n);reply_bytes=n;}return ZM_OK;
}
static uint16_t rd16(const uint8_t*p){return p[0]|p[1]<<8;}
static uint32_t rd32(const uint8_t*p){return rd16(p)|(uint32_t)rd16(p+2)<<16;}
static void wr16(uint8_t*p,uint16_t n){p[0]=n;p[1]=n>>8;}
static void wr32(uint8_t*p,uint32_t n){wr16(p,n);wr16(p+2,n>>16);}
static int event(uint8_t code,const void *data,uint16_t n,uint32_t timeout){
    (void)timeout;const uint8_t*p=data;
    if(code==0xe7 && n==8){
        // Codex: подсказка музыкальному кэшу Z80; его банки проверяет совместный стенд.
        unsigned at=rd32(p),bytes=rd32(p+4);
        if(at>package_size || bytes>package_size-at)abort();
        logline("QEMU: music_hint offset=%u bytes=%u",at,bytes);return ZM_OK;
    }
    if(code==0xe6 && n==12){
        /* Codex: периферийная модель прямого SPI Z80 → RAM_G; код ELF тот же.
         * Такты Z80 и очередь Tang проверяются отдельным совместным стендом. */
        unsigned at=rd32(p+2),address=rd32(p+6),bytes=rd16(p+10),done=0;
        if(!bytes||(at&511)||(bytes&511)||at>package_size||bytes>package_size-at||address>0x300000||bytes>0x300000-address)abort();
        // Codex: стресс-задержка применяется и к графическому SD-каналу, не только к E4.
        if(sd_latency[1])delay(sd_latency[0]+(bytes+sd_latency[1]-1)/sd_latency[1]);
        host_seek(package,at);
        while(done<bytes){unsigned count=bytes-done;if(count>sizeof(sd_answer))count=sizeof(sd_answer);
            if(host_read(package,sd_answer,count)!=count)abort();
            uint32_t header[]={0x80000000u|(address+done),count/4};
            host_write(trace,header,sizeof(header));host_write(trace,sd_answer,count);done+=count;
        }
        uint8_t answer[3]={p[0],p[1],0};reads++;read_bytes+=bytes;journal_read('G',at,bytes);
        app->command(0x77,answer,3);return ZM_OK;
    }
    if(code==0xe4 && n==8){
        unsigned bytes=rd16(p+6),at=rd32(p+2),done=0;
        if(bytes>32768 || at>package_size || bytes>package_size-at)abort();
        // Codex: управляемая задержка SD только для стресс-проверки; отдельный PCM продолжает работать.
        if(sd_latency[1])delay(sd_latency[0]+(bytes+sd_latency[1]-1)/sd_latency[1]);
        const unsigned header=bytes>1016?6:4;memcpy(sd_answer,p,2);host_seek(package,at);
        do {unsigned count=bytes-done;if(count>7680)count=7680;
            count=host_read(package,sd_answer+header,count);if(!count&&bytes)abort();
            /* Codex: в FS3 отдельно подменяем заголовок и таблицу в хвосте, не затрагивая исходные файлы. */
            if(catalogue_bytes){
                unsigned begin=at+done,end=begin+count;
                if(begin<16){unsigned changed=16-begin;if(changed>count)changed=count;memcpy(sd_answer+header,catalogue_override+begin,changed);}
                unsigned first=begin>catalogue_offset?begin:catalogue_offset,last=end<catalogue_offset+catalogue_bytes-16?end:catalogue_offset+catalogue_bytes-16;
                if(first<last)memcpy(sd_answer+header+first-begin,catalogue_override+16+first-catalogue_offset,last-first);
            }
            if(header==6){wr16(sd_answer+2,done);wr16(sd_answer+4,count);}else wr16(sd_answer+2,count);
            done+=count;app->command(header==6?0x31:0x71,sd_answer,count+header);
        }while(done<bytes);
        reads++;read_bytes+=bytes;journal_read('R',at,bytes);return ZM_OK;
    }
    if(code==0xe5 && n==35){
        unsigned op=p[2],bytes=rd16(p+33),at=rd32(p+29);uint8_t answer[969]={p[0],p[1],0};
        char path[256];snprintf(path,sizeof(path),HMM2_TEST_BASE "/qemu_stage1/run/%s",(const char*)p+3);
        long f=host_open(path,op==2?5:op==3||op==8?3:1);uint32_t size=0;unsigned count=0;
        if(f<0)answer[2]=1;
        else{
            size=host_size(f);
            if(op==1){host_seek(f,at);count=host_read(f,answer+9,bytes);file_read_bytes+=count;if(count!=bytes)answer[2]=1;}
            /* Клод: op 7 — чтение до 7680 байт одним пакетом 31 [seq, смещение 0, число, данные], затем 74 без данных. */
            if(op==7){
                static uint8_t big[6+7680];if(bytes>7680)abort();
                host_seek(f,at);unsigned got=host_read(f,big+6,bytes);file_read_bytes+=got;
                big[0]=p[0];big[1]=p[1];wr16(big+2,0);wr16(big+4,got);if(got)app->command(0x31,big,got+6);
                if(got!=bytes)answer[2]=1;
            }
            /* Клод: op 8 — запись: один 76 [seq, 0], поток F6 пишется в файл (reply), затем 74 со статусом. */
            if(op==8){
                host_seek(f,at);stream_file=f;stream_done=0;stream_bad=0;
                uint8_t pull[4]={p[0],p[1],0,0};app->command(0x76,pull,4);stream_file=-1;
                if(stream_bad||stream_done!=bytes)answer[2]=1;
                size=host_size(f);
            }
            if(op==3){
                host_seek(f,at);unsigned done=0;
                while(done<bytes){uint8_t pull[4]={p[0],p[1]};wr16(pull+2,done);reply_bytes=0;app->command(0x76,pull,4);
                    if(reply_bytes<7||!response[2]||rd16(response+3)!=done){abort();}
                    unsigned got=rd16(response+5);if(got>bytes-done){abort();}host_write(f,response+7,got);file_write_bytes+=got;done+=got;}
                size=host_size(f);
            }
            // Клод: op 6 — синхронизация на закрытии файла (hmm_fclose, сохранения Codex): у файла стенда сбрасывать нечего.
            if(op>3&&op!=6&&op!=7&&op!=8)answer[2]=1;
            host_close(f);
        }
        wr16(answer+3,count);wr32(answer+5,size);app->command(0x74,answer,count+9);return ZM_OK;
    }
    abort();return ZM_ERROR;
}
/* Codex: LINK сообщает действительное состояние модели, как совместный стенд. */
static uint32_t tang_core=1,ft_cfg,command_position,mouse_test[2];
/* Клод: щелчки мыши по расписанию (ui_clicks.bin: время мс, x, y, кнопка 1 левая/2 правая) — чтобы на стенде ходил герой и начинался бой на карте.
 * Указатель сначала уводится в угол (0,0), затем ведётся к точке; счётчики Kempston меняются не больше чем на 127 за пакет. */
static uint32_t ui_clicks[512][4],click_count,click_next,click_phase,click_step;static uint8_t click_x=0x80,click_y=0x80;static int click_rest_x,click_rest_y;
/* Codex: после первой калибровки тест не задевает края перед каждым щелчком. */
static uint32_t pointer_direct;static int pointer_x,pointer_y;
/* Codex: только поиск маршрута на стенде: можно дописывать ввод, затем повторить сохранённое расписание без live. */
static unsigned live_input;static uint32_t live_checked;
static void refresh_input(void){
    long f=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_clicks.bin",1);
    if(f>=0){click_count=host_read(f,ui_clicks,sizeof(ui_clicks))/16;host_close(f);}
    f=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_times.bin",1);
    if(f>=0){host_read(f,ui_times,sizeof(ui_times));host_close(f);}
    f=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_keys.bin",1);
    if(f>=0){key_count=host_read(f,ui_keys,sizeof(ui_keys));host_close(f);}
    f=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_test.bin",1);
    if(f>=0){host_read(f,&frame_limit,4);host_read(f,&finish_after,4);host_close(f);}
}
/* Codex: длительность щелчка остаётся параметром сценария сборки 91. */
static uint32_t click_hold=1;
static int ft_open(uint32_t ms){(void)ms;tang_core=2;ft_cfg=5;logline("QEMU: FT812 открыт");return ZM_OK;}
static void ft_close(void){ft_cfg&=~4u;}
static int core_back(uint32_t ms){(void)ms;tang_core=1;ft_cfg=0;return ZM_OK;}
/* Codex: ввод сценария начинается после первого показанного кадра, как у игрока. */
static volatile uint32_t first_frame_ready;
static int ft_write(uint32_t addr,const uint32_t *words,uint32_t count){
    xSemaphoreTake(trace_lock,portMAX_DELAY);
    if(addr>=0x308000&&addr<0x309000&&xTaskGetCurrentTaskHandle()!=game_handle){
        if(cmd_torn_after&&frames>=cmd_torn_after&&!cmd_torn_done){
            unsigned accepted=count<cmd_torn_words?count:cmd_torn_words;cmd_torn_words-=accepted;
            if(accepted<count){
                if(accepted){uint32_t header[]={addr,accepted};host_write(trace,header,sizeof(header));host_write(trace,words,accepted*4);}
                cmd_torn_done=1;logline("QEMU: partial cursor command injected frame=%u words=%u",frames,accepted);
                xSemaphoreGive(trace_lock);return ZM_ERROR;
            }
        }else if(cmd_torn_done&&!cmd_cursor_resumed){cmd_cursor_resumed=1;logline("QEMU: cursor resumed after command recovery frame=%u",frames);}
    }
    if(addr==0x3020fc)command_position=*words;
    uint32_t header[]={addr,count};host_write(trace,header,sizeof(header));host_write(trace,words,count*4);
    if(addr==0x302054 && count==1 && *words==2){
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        /* Codex: счётчик только задачи игры исключает ожидание SD и idle.
         * В режиме -icount shift=0 микросекунда модели равна 1000 инструкциям;
         * перевод в время ESP указывается в отчёте отдельно от физического замера. */
        taskYIELD();TaskStatus_t task_status;vTaskGetInfo(game_handle,&task_status,pdFALSE,eRunning);
        logline("QEMU CPU frame=%u runtime_us=%lu",frames+1,(unsigned long)task_status.ulRunTimeCounter);
        if(worker_handle){vTaskGetInfo(worker_handle,&task_status,pdFALSE,eRunning);logline("QEMU CPU worker frame=%u runtime_us=%lu",frames+1,(unsigned long)task_status.ulRunTimeCounter);}
        if(cursor_handle){vTaskGetInfo(cursor_handle,&task_status,pdFALSE,eRunning);logline("QEMU CPU cursor frame=%u runtime_us=%lu",frames+1,(unsigned long)task_status.ulRunTimeCounter);}
        if(audio_handle){vTaskGetInfo(audio_handle,&task_status,pdFALSE,eRunning);logline("QEMU CPU audio frame=%u runtime_us=%lu",frames+1,(unsigned long)task_status.ulRunTimeCounter);}
#endif
        if(!frames)first_frame_ready=millis();
        // Клод: T — виртуальное время кадра в мс (-icount): по нему меряются паузы между кадрами (окно пазла и т. п.).
        frames++;logline("QEMU: кадр=%u T=%lu SD=%u/%u PSRAM свободно=%u минимум=%u STACK=%u/%u",frames,(unsigned long)(esp_timer_get_time()/1000),reads,read_bytes,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),(unsigned)uxTaskGetStackHighWaterMark(game_handle),game_stack);
        /* Codex: сумма свободного места не показывает раздробление; тот же замер до и после правки ELF. */
        if(frames%100==0){multi_heap_info_t info;heap_caps_get_info(&info,MALLOC_CAP_SPIRAM);
            logline("QEMU MEM frame=%u free=%u largest=%u free_blocks=%u used_blocks=%u",frames,(unsigned)info.total_free_bytes,(unsigned)info.largest_free_block,(unsigned)info.free_blocks,(unsigned)info.allocated_blocks);}
        if(audio_handle)logline("QEMU: AUDIO_FREE=%u/5632",(unsigned)uxTaskGetStackHighWaterMark(audio_handle));
        if(worker_handle)logline("QEMU: WORKER_FREE=%u/%u",(unsigned)uxTaskGetStackHighWaterMark(worker_handle),worker_stack);
        if(cursor_handle)logline("QEMU: CURSOR_FREE=%u/%u",(unsigned)uxTaskGetStackHighWaterMark(cursor_handle),cursor_stack);
        if(frames>=frame_limit && millis()-scenario_started>=finish_after){host_close(trace);logline("QEMU: FILEX read=%u write=%u",file_read_bytes,file_write_bytes);capture_done=1;logline("QEMU: захват кадров завершён, %u кадров записаны",frames);for(;;)delay(1000);}
    }
    xSemaphoreGive(trace_lock);return ZM_OK;
}
static int ft_read(uint32_t addr,uint32_t *value){
    if(!ft_fault_started&&ft_fault_after&&frames>=ft_fault_after&&addr==0x3020f8&&xTaskGetCurrentTaskHandle()==game_handle){
        ft_fault_started=1;logline("QEMU: injected FT CMD fault frame=%u",frames);
    }
    if(ft_fault_started){
        if(addr==0x3020f8||addr==0x3020fc){*value=4095;return ZM_OK;}
        return ZM_ERROR;
    }
    *value=(addr==0x3020f8||addr==0x3020fc)?command_position:0;return ZM_OK;
}
static int link_read(uint32_t addr,uint32_t*words,uint32_t count){
    for(uint32_t i=0;i<count;i++,addr+=4)words[i]=addr==0?0x56444334u:addr==4?(tang_core<<24)|0x0120u:addr==0x5c?((ft_cfg&4)<<10)|((ft_cfg&1)<<27):0;
    return ZM_OK;
}
static int link_write(uint32_t addr,const uint32_t*words,uint32_t count){if(addr!=0x5c||count!=1||(*words&5)!=1)abort();ft_cfg=*words;return ZM_OK;}
void app_main(void){
    long live=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_live.bin",1);if(live>=0){live_input=1;host_close(live);}
    long options=host_open(HMM2_TEST_BASE "/qemu_stage1/internal_limit.bin",1);
    if(options>=0){host_read(options,&internal_limited,4);host_close(options);}
    long catalogue=host_open(HMM2_TEST_BASE "/qemu_stage1/catalogue.bin",1);
    if(catalogue>=0){catalogue_bytes=host_size(catalogue);if(catalogue_bytes<16||catalogue_bytes>16+2048*72)abort();catalogue_override=heap_caps_malloc(catalogue_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!catalogue_override||host_read(catalogue,catalogue_override,catalogue_bytes)!=catalogue_bytes)abort();host_close(catalogue);catalogue_offset=rd32(catalogue_override+4)>=3?rd32(catalogue_override+12):16;logline("QEMU: catalogue_override bytes=%u offset=%u",catalogue_bytes,catalogue_offset);}
    long control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_test.bin",1);
    if(control>=0){host_read(control,&frame_limit,4);host_read(control,&finish_after,4);host_close(control);testing=1;}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_keys.bin",1);
    if(control>=0){key_count=host_read(control,ui_keys,sizeof(ui_keys));host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_times.bin",1);
    if(control>=0){host_read(control,ui_times,sizeof(ui_times));host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/exit_test.bin",1);
    if(control>=0){host_read(control,&exit_test,4);host_close(control);testing=1;frame_limit=0xffffffffu;if(exit_test<3)key_count=0;}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ft_fault.bin",1);
    if(control>=0){host_read(control,&ft_fault_after,4);host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/cmd_torn.bin",1);
    if(control>=0){host_read(control,&cmd_torn_after,4);host_read(control,&cmd_torn_words,4);host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/alloc_fault.bin",1);
    if(control>=0){host_read(control,&alloc_fault_after,4);host_read(control,&alloc_fault_min,4);host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_clicks.bin",1);
    if(control>=0){click_count=host_read(control,ui_clicks,sizeof(ui_clicks))/16;host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_click_hold.bin",1);
    if(control>=0){host_read(control,&click_hold,4);host_close(control);if(!click_hold||click_hold>20)click_hold=1;}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_mouse.bin",1);
    if(control>=0){host_read(control,mouse_test,sizeof(mouse_test));host_close(control);}
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/ui_pointer_direct.bin",1);
    if(control>=0){host_read(control,&pointer_direct,4);host_close(control);}
    // Codex: отдельный исходный DAT для честного воспроизведения старого установленного ELF.
    static char data_path[256]=HMM2_TEST_BASE "/data/HMM2.DAT";
    control=host_open(HMM2_TEST_BASE "/qemu_stage1/data_path.bin",1);if(control>=0){host_read(control,data_path,sizeof(data_path)-1);host_close(control);}
    package=host_open(data_path,1);
    trace=host_open(HMM2_TEST_BASE "/qemu_stage1/menu.ft",5);
    trace_lock=xSemaphoreCreateMutex();if(!trace_lock)abort();
    sd_journal=host_open(HMM2_TEST_BASE "/qemu_stage1/sd.log",5);
    read_journal=host_open(HMM2_TEST_BASE "/qemu_stage1/reads.log",5);
    logline("QEMU: файлы данных=%ld, кадров=%ld",package,trace);if(package<0||trace<0)return;
    package_size=host_size(package);
    /* Codex: только имена из штатной таблицы ядра; ABI не расширяется. */
#define S(n) {#n,&n}
    static const struct esp_elfsym extra[]={S(memmove),S(memcmp),S(memchr),S(strncmp),S(strcpy),S(strncpy),S(snprintf),S(vsnprintf),S(strtoul),S(atan2),S(cos),S(fabs),S(floor),S(log10),S(pow),S(sin),S(sqrt),S(__adddf3),S(__subdf3),S(__muldf3),S(__divdi3),S(__udivdi3),S(__moddi3),S(__umoddi3),S(__ashldi3),S(__ashrdi3),S(__lshrdi3),S(__extendsfdf2),S(__fixdfsi),S(__floatdidf),S(__floatsidf),S(__divsf3),S(__divdf3),S(__floatunsidf),S(__ledf2),S(__truncdfsf2),S(log),S(qsort),S(ldexp),S(strncasecmp),S(strnlen),S(exp),ESP_ELFSYM_END};
#undef S
    esp_elf_register_symbol(extra);
    static zm_core_t core={.size=sizeof(zm_core_t),.abi_major=1,.abi_minor=4,.alloc=app_alloc,.realloc=app_realloc,.free=app_free,.log=logline,.millis=millis,.delay=delay,.task_start=task_start,
       .mutex_create=mutex_create,.mutex_lock=mutex_lock,.mutex_unlock=mutex_unlock,.signal_create=signal_create,.signal_give=signal_give,.signal_wait=signal_wait,
       .reply=reply,.event=event,.pcm_open=pcm_open,.pcm_write=pcm_write,.pcm_queued_ms=pcm_queued,.pcm_flush=pcm_flush,.pcm_close=pcm_close,.module_open=module_open,.module_close=module_close,.ft_open=ft_open,.ft_close=ft_close,.ft_read=ft_read,.ft_write=ft_write,.link_read=link_read,.link_write=link_write,.core_back=core_back};
    /* Codex: бюджет задаёт снимок платы; прежние 7,5 МиБ скрывали отказ загрузки GHOSTPLT.
     * Без файла используем 7 МиБ: на плате после выгрузки игры измерено 7423084 байта. */
    options=host_open(HMM2_TEST_BASE "/qemu_stage1/sd_latency.bin",1);
    if(options>=0){host_read(options,sd_latency,sizeof(sd_latency));host_close(options);}
    logline("QEMU: SD latency base_ms=%u bytes_per_ms=%u",sd_latency[0],sd_latency[1]);
    options=host_open(HMM2_TEST_BASE "/qemu_stage1/psram_budget.bin",1);
    if(options>=0){host_read(options,&psram_budget,4);host_close(options);}
    if(psram_budget<4*1024*1024||psram_budget>8*1024*1024)abort();
    size_t free_before=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(free_before>psram_budget){void *reserve=heap_caps_malloc(free_before-psram_budget,MALLOC_CAP_SPIRAM);if(!reserve)abort();}
    logline("QEMU: PSRAM budget=%lu free=%u",(unsigned long)psram_budget,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_elf_t elf;int result=esp_elf_init(&elf);logline("QEMU: init=%d PSRAM=%u",result,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));if(result)return;
    size_t module_bytes=module_end-module_start;void *copy=heap_caps_malloc(module_bytes,MALLOC_CAP_SPIRAM);if(!copy)abort();memcpy(copy,module_start,module_bytes);
    result=esp_elf_relocate(&elf,copy);heap_caps_free(copy);logline("QEMU: relocate=%d",result);if(result)return;
    char*argv[]={"HMM2.ELF",(char*)&core,(char*)&app};result=elf.entry(3,argv);logline("QEMU: entry=%d app=%p main_stack_free=%u",result,app,(unsigned)uxTaskGetStackHighWaterMark(NULL));if(result!=1||!app)return;
    options=host_open(HMM2_TEST_BASE "/qemu_stage1/rng_fixture.bin",1);
    if(options>=0){
        if(host_read(options,&rng_fixture,sizeof(rng_fixture))!=sizeof(rng_fixture)||rng_fixture.magic!=0x474e5248u
            ||rng_fixture.getter_delta<=-0x400000||rng_fixture.getter_delta>=0x400000
            ||rng_fixture.data_delta<=-0x400000||rng_fixture.data_delta>=0x400000)abort();
        host_close(options);rng_ready=1;
    }
    result=app->start(&core);logline("QEMU: start=%d",result);if(result)return;
    const uint8_t rtc[]={2,0,0,0x14,0x29,0x10,0x26,0x80};app->command(0x72,rtc,sizeof(rtc));
    scenario_started=millis();uint8_t config[13];wr32(config,package_size);memcpy(config+4,"HMM2.DAT",9);app->command(0x70,config,13);
    if(testing){
        /* Codex: настоящие PS/2 пакеты, те же клавиши N, S, Enter, которые нажимает игрок. */
        unsigned next=0,exit_step=0,mouse_started=0,input_ready=0;uint32_t start=millis();
        while(!capture_done){
            if(live_input&&!click_phase&&millis()-live_checked>=1000){live_checked=millis();refresh_input();}
            if(!input_ready&&frames){start=first_frame_ready;input_ready=1;logline("QEMU: input ready after first frame at=%lu",(unsigned long)start);}
            uint8_t input[7]={0,0xff,0xff,0xff};unsigned n=4;
            /* Codex: только стенд: реальные счётчики мыши для кадров курсора и прокрутки. */
            if(mouse_test[0]&&input_ready){
                uint32_t elapsed=millis()-start;input[2]=0;input[3]=0;
                if(elapsed>=mouse_test[1]){uint32_t tick=(elapsed-mouse_test[1])/50;
                    /* Codex: начало трассы до первого движения, пока окно ещё не ушло в туман. */
                    if(!mouse_started){logline("QEMU: mouse started mode=%lu",(unsigned long)mouse_test[0]);mouse_started=1;}
                    if(mouse_test[0]==3){
                        input[2]=(uint8_t)((tick%256<128?tick%128:255-tick%256)*3);
                        input[3]=(uint8_t)((tick%192<96?tick%96:191-tick%192)*2);
                    }else input[2]=mouse_test[0]==1?(uint8_t)(tick%8<4?tick%4:7-tick%8):(uint8_t)(tick<2?(tick+1)*100:200);
                }
            }
            if(click_count&&input_ready){
                uint8_t buttons=0xff;
                if(click_next<click_count&&millis()-start>=ui_clicks[click_next][0]){
                    const uint32_t *c=ui_clicks[click_next];
                    if(click_phase==0){
                        if(pointer_direct&&click_next){click_phase=1;click_rest_x=(int)c[1]/2-pointer_x;click_rest_y=(int)c[2]/2-pointer_y;}
                        else {click_x-=127;click_y+=127;if(++click_step>=3){click_phase=1;click_step=0;click_rest_x=(int)c[1]/2;click_rest_y=(int)c[2]/2;}}
                    }
                    else if(click_phase==1){int sx=click_rest_x>127?127:click_rest_x<-127?-127:click_rest_x,sy=click_rest_y>127?127:click_rest_y<-127?-127:click_rest_y;click_x+=sx;click_y-=sy;click_rest_x-=sx;click_rest_y-=sy;if(!click_rest_x&&!click_rest_y){click_phase=2;pointer_x=(int)c[1]/2;pointer_y=(int)c[2]/2;}}
                    /* Codex: кнопка 0 только перемещает мышь, для настоящей прокрутки у края. */
                    else if(click_phase==2){buttons=(uint8_t)~(c[3]==0?0u:c[3]==2?2u:1u);if(++click_step>=click_hold){click_step=0;click_phase=3;}}
                    else {logline("QEMU: click %lu at %lu,%lu input_ms=%lu",(unsigned long)click_next,(unsigned long)c[1],(unsigned long)c[2],(unsigned long)(millis()-start));click_phase=0;++click_next;}
                }
                input[1]=buttons;input[2]=click_x;input[3]=click_y;
            }
            if(input_ready&&next<key_count&&millis()-start>=ui_times[next]){input[4]=ui_keys[next];input[5]=0xf0;input[6]=ui_keys[next];n=7;logline("QEMU: PS2=%02x",ui_keys[next]);++next;}
            /* Codex: выход проверяется настоящим Xtensa ELF, включая его деструкторы и снятие FDE. */
            if(exit_test>=3 && exit_loading_active && !exit_step && millis()-start>=(exit_test==3?16500u:12500u)){
                exit_step=1;tang_core=1;ft_cfg=0;logline("QEMU: M_SEL=0, Tang SLOT0 during load; Z80 stopped");
            }else if(exit_test && exit_test<3 && !exit_step && frames>=20){
                exit_step=1;
                if(exit_test==2){tang_core=1;ft_cfg=0;logline("QEMU: Evo reset, Tang SLOT0");}
                else{input[4]=0x15;input[5]=0xf0;input[6]=0x15;n=7;}
            }else if(exit_test==1 && exit_step==1 && frames>=50){
                exit_step=2;input[4]=0x5a;input[5]=0xf0;input[6]=0x5a;n=7;
            }
            if(exit_test<2||!exit_step)app->command(0x75,input,n);
            if(app->poll)app->poll(millis());
            delay(50);
            if((ft_fault_after||alloc_fault_after) && task_finished){
                app->command(0x73,NULL,0);unsigned state=response[0];app->stop();
                logline("QEMU: exit %s fault_injected=%u state=%u core=%u host=%s task_finished=%u",
                    (ft_fault_started||alloc_fault_started)&&state==255&&tang_core==1&&!(ft_cfg&4)?"verified":"FAILED",
                    ft_fault_started||alloc_fault_started,state,tang_core,(ft_cfg&4)?"ESP":"Evo",task_finished);
                host_close(trace);capture_done=1;
            }else if(exit_test && task_finished){
                if(exit_test>=3){
                    // Codex: без запроса статуса проверяется завершение задачи и закрытие видео.
                    app->stop();
                    logline("QEMU: exit %s no_status_query=1 core=%u host=%s task_finished=%u",
                        tang_core==1&&!(ft_cfg&4)&&task_finished?"verified":"FAILED",tang_core,(ft_cfg&4)?"ESP":"Evo",task_finished);
                }else{
                    app->command(0x73,NULL,0);unsigned state=response[0];app->stop();
                    logline("QEMU: exit %s state=%u core=%u host=%s task_finished=%u",
                        state==3&&tang_core==1&&!(ft_cfg&4)?"verified":"FAILED",state,tang_core,(ft_cfg&4)?"ESP":"Evo",task_finished);
                }
                host_close(trace);capture_done=1;
            }
        }
    }
}
