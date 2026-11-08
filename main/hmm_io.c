/* Codex: транспорт DOOM VDAC4, авторы и лицензии — THIRD_PARTY.md.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_core.h"
#include "hmm_platform.h"
#include "hmm_clock.h"
#include "hmm_loading.h"
#include <string.h>
#include <stdio.h>
static void *io_signal, *io_mutex;
static volatile int game_done, audio_done;
static uint16_t request_id, wanted, received;
static int awaiting;
static int graphics_awaiting,graphics_status;
static int fs_awaiting,fs_status,fs_op;
int hmm_read_hold;
int hmm_audio_busy(void){return awaiting||graphics_awaiting||fs_awaiting||hmm_read_hold;}
static uint32_t fs_size;
/* Codex: буфер вызывающего живёт до ответа 74; лишние копии по 960 байт убраны.
 * Запись: один 76 запускает поток F6, каждый кадр укладывается в ABI (1024 байта). */
#define FS_PIECE 1016
static uint8_t fs_piece[7+FS_PIECE];
static uint16_t fs_write_sent;
/* Codex: поток идёт сразу в буфер вызывающего; он живёт до последнего пакета.
 * При E4 >1016 ответ 71 имеет [seq,u16 offset,u16 length,data], до 1014 байт.
 * F1 отправляется только в конце потока. SPI сохраняет штатный контроль TID/места. */
static uint8_t *response;
/* Клод: запрос к карте один, а просят два потока: игра и задача звука (hmm_audio.cpp). Занятость — флаг, а не мьютекс: hmm_fail уходит
 * исключением, его снимает hmm_io_cancel. Держится только на время запроса: read_block снимает его до hmm_loading_tick. */
#ifdef HMM2_HOST
static void channel_enter(void){}
static void channel_leave(void){}
#else
static int io_channel;
/* Клод: взятие флага только под io_mutex. __atomic_exchange_n здесь не годится: S32C1I на внешней памяти не атомарен (ESP-IDF, cpu.c:
 * «native CAS instruction cannot be used»), а данные ELF лежат в PSRAM. Сборка 67 на плате: оба потока входили разом, номера запросов
 * расходились (reject seq=2068 при ожидании 2069) и чтение падало. В QEMU S32C1I атомарен везде, там этого не видно. */
int hmm_flag_take(int *flag){
    hmm_core->mutex_lock(io_mutex);const int free=!*flag;if(free)*flag=1;hmm_core->mutex_unlock(io_mutex);
    return free;
}
/* Клод: игра при чтении подряд занимала канал сразу после освобождения, и задача звука могла ждать дольше своей очереди PCM (220 мс).
 * Теперь ждущий звук проходит первым: игра пропускает его до взятия флага. */
extern int hmm_audio_self(void);
static volatile int audio_waiting;
static void channel_enter(void){
    const int audio=hmm_audio_self();
    if(audio)audio_waiting=1;else while(audio_waiting&&hmm_running)hmm_core->delay(1);
    while(!hmm_flag_take(&io_channel)){if(!hmm_running){audio_waiting=0;hmm_fail("Application stopped");}hmm_core->delay(1);}
    if(audio)audio_waiting=0;
}
static void channel_leave(void){__atomic_store_n(&io_channel,0,__ATOMIC_RELEASE);}
#endif
int hmm_window_owner;
/* Codex: запретить запоздалому ответу писать в уже освобождённый буфер вызывающего. */
void hmm_io_cancel(void){
    if(!io_mutex)return;
    hmm_core->mutex_lock(io_mutex);
    awaiting=graphics_awaiting=fs_awaiting=0;response=0;
    received=fs_write_sent=0;graphics_status=fs_status=255;
    channel_leave();hmm_window_owner=0;hmm_read_hold=0;
    hmm_core->mutex_unlock(io_mutex);
    if(io_signal)hmm_core->signal_give(io_signal);
}
static void request_lock(void){
    hmm_core->mutex_lock(io_mutex);
    if(!hmm_running){hmm_core->mutex_unlock(io_mutex);hmm_fail("Application stopped");}
}
static uint32_t sd_bytes,sd_wait,sd_requests;
// Клод: из них — картинки с SD прямо в RAM_G (E6, hmm_remote_graphics): HMM2 PERF E6_B отделяет атласы GPU от чтений для процессора.
uint32_t hmm_gfx_bytes;
void hmm_sd_stats(uint32_t *bytes,uint32_t *wait,uint32_t *requests){
    *bytes=sd_bytes;*wait=sd_wait;*requests=sd_requests;
}
/* Codex: один запрос в полёте; диагностический снимок живёт до результата.
 * В журнале остаются и отвергнутые ответы, а экран получает краткую причину. */
static uint32_t io_started,io_offset;
static uint16_t io_length,io_last_seq,io_last_bytes;
static uint8_t io_command,io_operation,io_last_command,io_last_status;
static char io_name[13];
static void trace_begin(uint8_t cmd,unsigned op,const char*name,uint32_t offset,unsigned n){
    io_started=hmm_core->millis();io_command=cmd;io_operation=(uint8_t)op;
    io_offset=offset;io_length=(uint16_t)n;io_last_command=io_last_status=0;io_last_seq=io_last_bytes=0;
    strcpy(io_name,name);
}
static void trace_end(int result){
    // Codex: PCM не конкурирует с тысячами успешных SD-строк; ошибки и ожидания от 100 мс остаются.
    if(result>=0&&!io_last_status&&hmm_core->millis()-io_started<100)return;
    if(hmm_core->log)hmm_core->log("HMM2 SD <%02X seq=%u result=%d wait=%lu rx=%02X/%u/%u/%02X",io_command,request_id,result,
        (unsigned long)(hmm_core->millis()-io_started),io_last_command,io_last_seq,io_last_bytes,io_last_status);
}
static void trace_timeout(const char*reason){
    trace_end(-1);
    hmm_io_cancel();
    char message[96];
    snprintf(message,sizeof(message),"%s %02X/%u %s S%u O%lu L%u RX%02X/%u",reason,io_command,io_operation,io_name,
        request_id,(unsigned long)io_offset,io_length,io_last_command,io_last_seq);
    hmm_fail(message);
}
/* Клавиатура — байты очереди PS/2 контроллера AVR Evo из пакета 75 как есть (загрузчик Z80 читает
 * регистр Gluk #F0, матрицу ZX не читает); разбор — в video.c. Кольцо на 256 байт: не влезло —
 * ps2_lost, и разбор получит #FF, как при переполнении очереди AVR (клавиши отпускаются).
 * joy_state — биты Kempston-джойстика (0 вправо, 1 влево, 2 вниз, 3 вверх, 4 огонь). */
static uint8_t ps2_ring[256];
static uint8_t ps2_head,ps2_tail,ps2_lost,joy_state;
static unsigned ps2_produced,ps2_consumed;
struct MouseSample {int dx,dy;unsigned keys;uint32_t when;uint16_t x,y;uint8_t buttons,edge;};
static struct MouseSample mouse_samples[128];
static unsigned mouse_head,mouse_tail;static int mouse_read_buttons,mouse_queued_buttons,mouse_lost;
static int mouse_gui_x=320,mouse_gui_y=240;
static int mouse_bound(int value,int maximum){return value<0?0:value>maximum?maximum:value;}
/* Клод: джойстик и кнопки мыши принимаются, только если одинаковы в двух пакетах ввода подряд. Движок останавливает идущего
 * героя при любом нажатии или щелчке, и одиночный выброс с порта Kempston #1F (лаунчер считает «нет джойстика» только #FF)
 * или кнопок #FADF ставил героя посреди пути (пользователь, 03.11: «посреди зелёных»). Настоящее нажатие длится десятки мс. */
static uint8_t joy_raw,buttons_raw,joy_inhibit,buttons_inhibit;
static uint32_t keys_time;
/* Мышь Kempston из пакета 72: последние счётчики X/Y (по модулю 256), кнопки (бит 0 левая,
 * 1 правая, 2 средняя) и приращения, накопленные до следующего hmm_mouse_take. Разность
 * считается на каждом принятом пакете — между пакетами Z80 (единицы мс) счётчик не успевает
 * уйти на 128. mouse_sync=0 — следующий пакет только задаёт отсчёт. */
static uint8_t mouse_x,mouse_y,mouse_buttons;
static int mouse_sync;
static int32_t mouse_dx,mouse_dy;
static uint16_t rd16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p)|((uint32_t)rd16(p+2)<<16); }
static void wr16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8; }
static void wr32(uint8_t *p,uint32_t v) { wr16(p,v); wr16(p+2,v>>16); }

/* Codex: Z80 читает DAT и пишет RAM_G через свой SPI; ESP передаёт только адреса.
 * Подтверждение приходит после DMA, поэтому DL не использует незавершённый битмап. */
void hmm_remote_graphics(uint32_t offset,uint32_t address,uint32_t size) {
    if(!size||(offset&511)||(size&511)||address>=0x300000||size>0x300000-address
       ||offset>hmm_package_size||size>hmm_package_size-offset)hmm_fail("Invalid graphics transfer");
    while(size){
        uint16_t count=size>8192?8192:(uint16_t)size;
        uint8_t request[12];
        // Клод: пока идёт E6, задача курсора не трогает сопроцессор: очередь Evo в RAM_G держит 8 байт (FT_STAT 25).
        hmm_gpu_busy_enter();channel_enter();
        request_lock();
        ++request_id;graphics_awaiting=1;graphics_status=255;
        wr16(request,request_id);wr32(request+2,offset);wr32(request+6,address);wr16(request+10,count);
        trace_begin(0xE6,0,"HMM2.DAT",offset,count);
        hmm_core->mutex_unlock(io_mutex);
        if(hmm_core->event(0xE6,request,sizeof(request),1000)!=ZM_OK)trace_timeout("Graphics queue full");
        const uint32_t start=hmm_core->millis();
        for(;;){
            if(!hmm_running)hmm_fail("Application stopped");
            hmm_core->signal_wait(io_signal,20);
            hmm_core->mutex_lock(io_mutex);
            int done=!graphics_awaiting,status=graphics_status;
            hmm_core->mutex_unlock(io_mutex);
            if(done)channel_leave();
            hmm_loading_tick();
            if(!hmm_running)hmm_fail("Application stopped");
            if(done){if(status)trace_timeout("Graphics SD error");break;}
            if((uint32_t)(hmm_core->millis()-start)>10000)trace_timeout("Graphics timeout");
        }
        uint32_t status;
        if(hmm_core->link_read(0x5c,&status,1)!=ZM_OK)hmm_fail("FT_STAT read failed");
        if(status&(1u<<25))hmm_fail("FT RAM_G SPI queue overflow");
        hmm_gpu_busy_leave();
        sd_bytes+=count;hmm_gfx_bytes+=count;sd_wait+=hmm_core->millis()-start;++sd_requests;
        trace_end(count);offset+=count;address+=count;size-=count;
    }
}
static size_t read_block(uint32_t offset,void *data,size_t size) {
    uint8_t request[8];
    if(!size)return 0;
    if(!data||size>32768)hmm_fail("SD request exceeds 32 KiB");
    if(!hmm_running) hmm_fail("Application stopped");
    channel_enter();
    request_lock();
    ++request_id; wanted=(uint16_t)size; received=0; awaiting=1;response=data;
    wr16(request,request_id); wr32(request+2,offset); wr16(request+6,wanted);
    trace_begin(0xE4,1,"HMM2.DAT",offset,(unsigned)size);
    hmm_core->mutex_unlock(io_mutex);
    if(hmm_core->event(0xE4,request,sizeof(request),1000)!=ZM_OK) trace_timeout("SD queue full");
    uint32_t start=hmm_core->millis();
    for(;;) {
        if(!hmm_running) hmm_fail("Application stopped");
        hmm_core->signal_wait(io_signal,20);
        hmm_core->mutex_lock(io_mutex);
        int done=!awaiting;
        if(done) { size=received;response=0; }
        hmm_core->mutex_unlock(io_mutex);
        if(done)channel_leave();
        hmm_loading_tick();
        if(!hmm_running)hmm_fail("Application stopped");
        if(done) {
            sd_bytes+=(uint32_t)size;sd_wait+=hmm_core->millis()-start;++sd_requests;
            trace_end((int)size);
            break;
        }
        if((uint32_t)(hmm_core->millis()-start)>10000) trace_timeout("SD timeout");
    }
    return size;
}
/* Codex: между короткими обменами декодер и SFX получают безопасную точку обслуживания. */
size_t hmm_remote_read(uint32_t offset,void *data,size_t size){
    if(size>32768||offset>hmm_package_size||size>hmm_package_size-offset)hmm_fail("SD read bounds");
    size_t done=0;
    while(done<size){
        size_t count=size-done;if(count>8192)count=8192;
        /* Клод: малое чтение (до 1016 байт) с нечётного смещения шло на Z80 в DMA с нечётным адресом назначения: хвост файла OGG читался бы неверно
         * (стенд с Z80: assert в модели DMA на конце трека). Первый байт — отдельным запросом, дальше смещение чётное. Поток E4 больше 1016 байт выравнивает себя сам. */
        if(((offset+done)&1)&&count>1&&count<=1016)count=1;
        size_t got=read_block(offset+done,(uint8_t*)data+done,count);done+=got;
        if(got!=count)break;
    }
    return done;
}
/* Байты PS/2 с прошлого вызова (не больше max) и джойстик. Пакетов нет дольше 500 мс (загрузчик
 * не опрашивает клавиатуру) — *stale: клавиши и джойстик отпустить. */
size_t hmm_keys_take(uint8_t *out,size_t max,int *joy,int *stale) {
    size_t n=0;
    hmm_core->mutex_lock(io_mutex);
    *stale=(uint32_t)(hmm_core->millis()-keys_time)>500;
    *joy=*stale?0:joy_state;
    if(ps2_lost && n<max) { out[n++]=0xFF; ps2_lost=0; }
    while(n<max && ps2_tail!=ps2_head && (mouse_tail==mouse_head||ps2_consumed!=mouse_samples[mouse_tail&127].keys)){
        out[n++]=ps2_ring[ps2_tail++];++ps2_consumed;
    }
    hmm_core->mutex_unlock(io_mutex); return n;
}
/* Кнопки мыши и приращения X/Y (Y растёт вверх) с прошлого вызова. Пакетов нет дольше
 * 500 мс — кнопки отпущены, как клавиши. */
int hmm_mouse_take(int *dx,int *dy) {
    hmm_core->mutex_lock(io_mutex);
    int buttons=(uint32_t)(hmm_core->millis()-keys_time)>500?0:mouse_buttons;
    *dx=mouse_dx; *dy=mouse_dy; mouse_dx=mouse_dy=0;
    hmm_core->mutex_unlock(io_mutex); return buttons;
}
/* Keep accepted button transitions in packet order while the game renders.
 * Motion between transitions is coalesced; cursor and game share one consumer. */
static void mouse_sample(int dx,int dy,int buttons){
    const int edge=buttons!=mouse_queued_buttons;mouse_queued_buttons=buttons;
    if(mouse_head!=mouse_tail){struct MouseSample *last=&mouse_samples[(mouse_head-1)&127];
        if(!edge&&!last->edge&&last->buttons==buttons&&last->keys==ps2_produced){last->dx+=dx;last->dy+=dy;last->x=(uint16_t)mouse_gui_x;last->y=(uint16_t)mouse_gui_y;last->when=keys_time;return;}}
    if(mouse_head-mouse_tail==128){mouse_tail=mouse_head;mouse_lost=1;}
    struct MouseSample *sample=&mouse_samples[mouse_head++&127];sample->dx=dx;sample->dy=dy;sample->buttons=(uint8_t)buttons;sample->keys=ps2_produced;sample->edge=(uint8_t)edge;
    sample->x=(uint16_t)mouse_gui_x;sample->y=(uint16_t)mouse_gui_y;
    sample->when=keys_time;
}
void hmm_mouse_position(int *x,int *y){
    hmm_core->mutex_lock(io_mutex);
    *x=mouse_gui_x;*y=mouse_gui_y;
    mouse_dx=mouse_dy=0;hmm_core->mutex_unlock(io_mutex);
}
int hmm_mouse_next(int *dx,int *dy,int *buttons,int *x,int *y,uint32_t *when){
    int got=0;hmm_core->mutex_lock(io_mutex);*dx=*dy=0;*x=mouse_gui_x;*y=mouse_gui_y;*when=0;
    if(mouse_lost){mouse_lost=0;*buttons=mouse_read_buttons=0;got=1;}
    else if(mouse_tail!=mouse_head){const struct MouseSample *sample=&mouse_samples[mouse_tail&127];
        if(sample->keys==ps2_consumed){++mouse_tail;*dx=sample->dx;*dy=sample->dy;*x=sample->x;*y=sample->y;*when=sample->when;*buttons=mouse_read_buttons=sample->buttons;got=1;}}
    else{const int current=(uint32_t)(hmm_core->millis()-keys_time)>500?0:mouse_buttons;
        *buttons=current;if(current!=mouse_read_buttons){mouse_read_buttons=current;got=1;}}
    hmm_core->mutex_unlock(io_mutex);return got;
}
/* p — кнопки (#FADF, нажатая — 0), X (#FBDF), Y (#FFDF). Все три #FF — мыши нет или AVR
 * её переинициализирует (счётчики потом начнутся с 0/1): отсчёт заново, без скачка. После
 * паузы в пакетах дольше 500 мс — тоже заново. Вызов под io_mutex. */
static void mouse_buttons_packet(uint8_t b){
    if(b==buttons_raw){buttons_inhibit&=b;mouse_buttons=b&~buttons_inhibit;}buttons_raw=b;
}
static void mouse_packet(const uint8_t *p,int stale) {
    if(p[0]==0xFF && p[1]==0xFF && p[2]==0xFF) {
        // FF/FF counters may be a valid released mouse or a transient port read.
        // Use the same two-packet button debounce before synthesizing a release.
        const int previous=mouse_buttons;
        if(previous&&buttons_raw&&hmm_core->log)hmm_core->log("HMM2 INPUT mouseff held=%u",(unsigned)previous);
        mouse_buttons_packet(0);
        if(previous!=mouse_buttons)mouse_sample(0,0,mouse_buttons);
        mouse_sync=0;return;
    }
    int dx=0,dy=0,previous=mouse_buttons;
    if(mouse_sync && !stale) {
        dx=(int8_t)(uint8_t)(p[1]-mouse_x);dy=(int8_t)(uint8_t)(p[2]-mouse_y);
        mouse_dx+=dx;mouse_dy+=dy;
    }
    mouse_x=p[1]; mouse_y=p[2]; mouse_sync=1;
    mouse_gui_x=mouse_bound(mouse_gui_x+dx*2,639);mouse_gui_y=mouse_bound(mouse_gui_y-dy*2,479);
    mouse_buttons_packet((uint8_t)(~p[0]&7));
    if(dx||dy||previous!=mouse_buttons)mouse_sample(dx,dy,mouse_buttons);
}
int hmm_fs_call(unsigned op,const char *name,const char *other,uint32_t offset,void *data,unsigned length,uint32_t *size) {
    uint8_t request[35]={0};
    if(op>6 || length>7680 || (length && !data) || strlen(name)>12 || (other && strlen(other)>12)) return -1;
    if(!hmm_running) hmm_fail("Application stopped");
    channel_enter();
    request_lock();
    ++request_id; fs_awaiting=1; received=0; wanted=length;fs_op=op;fs_write_sent=0;response=data;awaiting=op==1;
    wr16(request,request_id); request[2]=op==1?7:op==3?8:op; strcpy((char*)request+3,name);
    if(other) strcpy((char*)request+16,other);
    wr32(request+29,offset); wr16(request+33,length);
    trace_begin(0xE5,op,name,offset,length);
    hmm_core->mutex_unlock(io_mutex);
    if(hmm_core->event(0xE5,request,sizeof(request),1000)!=ZM_OK) trace_timeout("SD queue full");
    uint32_t start=hmm_core->millis();
    for(;;) {
        if(!hmm_running) hmm_fail("Application stopped"); hmm_core->signal_wait(io_signal,20);
        hmm_core->mutex_lock(io_mutex);
        int done=!fs_awaiting,result=fs_status;
        if(done) {
            if(size) *size=fs_size;
            if(op==1 && !result && received!=length) result=-1;
            response=0;awaiting=0;
        }
        hmm_core->mutex_unlock(io_mutex);
        if(done)channel_leave();
        hmm_loading_tick();
        if(!hmm_running)hmm_fail("Application stopped");
        if(done) {trace_end(result);return result;}
        if((uint32_t)(hmm_core->millis()-start)>10000) trace_timeout("SD timeout");
    }
}
int hmm_command(uint8_t cmd,const uint8_t *p,uint16_t n) {
    uint8_t ok=0;
    if(cmd==0x70) {
        if(hmm_running && hmm_state==0 && n>=6 && n<=17 && p[n-1]==0 && rd32(p)>=12) {
            if(!strcmp((const char*)p+4,"HMM2.DAT")) {
                hmm_package_size=rd32(p); ok=1;
            }
        }
        hmm_core->reply(0xF0,&ok,1);
        if(ok) { __atomic_store_n(&hmm_state,1,__ATOMIC_RELEASE); hmm_core->signal_give(io_signal); }
    } else if(cmd==0x72) {
        /* Codex: RTC Ево синхронизируется до конфигурации70 и входа в игровой main. */
        if(hmm_running&&hmm_state==0)ok=hmm_clock_sync(p,n);
        if(!ok)hmm_core->log("HMM2: RTC Evo invalid or already started");
        hmm_core->reply(0xF2,&ok,1);
    } else if(cmd==0x71 || cmd==0x31) {
        hmm_core->mutex_lock(io_mutex);
        io_last_command=cmd;io_last_seq=n>=2?rd16(p):0;io_last_bytes=n;io_last_status=0;
        int finished=0;
        if(hmm_running && n>=4 && n<=(cmd==0x31?8192:1020) && awaiting && rd16(p)==request_id) {
            if(cmd==0x71 && !fs_awaiting && wanted<=1016 && rd16(p+2)==n-4 && n-4<=wanted){
                received=n-4;memcpy(response,p+4,received);ok=finished=1;
            } else if((fs_awaiting || wanted>1016) && n>=6 && rd16(p+4)==n-6){
                unsigned at=rd16(p+2),count=n-6;
                if(at==received && count<=wanted-received){
                    memcpy(response+received,p+6,count);received+=count;ok=1;
                    finished=received==wanted||!count;
                } else if(count && at<received && count<=received-at && !memcmp(response+at,p+6,count))ok=1;
            }
        }
        /* Codex: последний ответ уходит до пробуждения игры и следующего E4. */
        if((finished||!ok) && hmm_core->reply(0xF1,&ok,1)!=ZM_OK)ok=0;
        if(!ok&&hmm_core->log)hmm_core->log("HMM2 SD reject %02x seq=%u len=%u wanted=%u/%u",cmd,io_last_seq,n,request_id,wanted);
        if(ok&&finished) awaiting=0;
        hmm_core->mutex_unlock(io_mutex);
        if(ok&&finished) hmm_core->signal_give(io_signal);
    } else if(cmd==0x77) {
        hmm_core->mutex_lock(io_mutex);
        io_last_command=cmd;io_last_seq=n>=2?rd16(p):0;io_last_bytes=n;io_last_status=n==3?p[2]:255;
        if(hmm_running&&n==3&&graphics_awaiting&&rd16(p)==request_id){graphics_status=p[2];ok=1;}
        if(hmm_core->reply(0xF7,&ok,1)!=ZM_OK)ok=0;
        if(ok)graphics_awaiting=0;
        hmm_core->mutex_unlock(io_mutex);
        if(ok)hmm_core->signal_give(io_signal);
    } else if(cmd==0x74) {
        hmm_core->mutex_lock(io_mutex);
        io_last_command=cmd;io_last_seq=n>=2?rd16(p):0;io_last_bytes=n;io_last_status=n>=3?p[2]:255;
        if(hmm_running && n==9 && fs_awaiting && rd16(p)==request_id && !rd16(p+3)
           && (fs_op!=3 || p[2]!=0 || fs_write_sent==wanted)) {
            fs_status=p[2]; fs_size=rd32(p+5); ok=1;
        }
        if(hmm_core->reply(0xF4,&ok,1)!=ZM_OK)ok=0;
        if(!ok&&hmm_core->log)hmm_core->log("HMM2 SD reject 74 seq=%u len=%u status=%u wanted=%u/%u",io_last_seq,n,io_last_status,request_id,wanted);
        if(ok) fs_awaiting=0;
        hmm_core->mutex_unlock(io_mutex);
        if(ok) hmm_core->signal_give(io_signal);
    } else if(cmd==0x76) {
        /* Codex: поток можно повторить с подтверждённого смещения после таймаута. */
        uint8_t rejected[7]={0};
        if(n>=2)memcpy(rejected,p,2);
        hmm_core->mutex_lock(io_mutex);
        unsigned at=n==4?rd16(p+2):0;
        if(hmm_running && n==4 && fs_awaiting && fs_op==3 && rd16(p)==request_id && at<wanted && at<=fs_write_sent) {
            while(at<wanted){
                unsigned count=wanted-at; if(count>FS_PIECE)count=FS_PIECE;
                memcpy(fs_piece,p,2);fs_piece[2]=1;wr16(fs_piece+3,at);wr16(fs_piece+5,count);
                memcpy(fs_piece+7,response+at,count);
                if(hmm_core->reply(0xF6,fs_piece,7+count)!=ZM_OK)break;
                at+=count;if(at>fs_write_sent)fs_write_sent=at;
            }
        } else {
            if(hmm_core->log)hmm_core->log("HMM2 SD reject 76 seq=%u len=%u off=%u wanted=%u/%u",n>=2?rd16(p):0,n,at,request_id,wanted);
            hmm_core->reply(0xF6,rejected,sizeof(rejected));
        }
        hmm_core->mutex_unlock(io_mutex);
    } else if(cmd==0x75) {
        /* [джойстик Kempston, кнопки мыши, X, Y, байты очереди PS/2…] — загрузчик 0.1.7+ */
        if(n>=4) {
            hmm_core->mutex_lock(io_mutex);
            uint32_t now=hmm_core->millis();
            const int stale=(uint32_t)(now-keys_time)>500;
            /* Codex: после паузы старое нажатие не должно возвращаться, пока
             * два свежих пакета ещё подтверждают уже отпущенную кнопку. */
            if(stale){
                // SDL has already released timed-out inputs. A still-held
                // physical button must not reappear as a new route-stop press.
                // Re-arm each bit only after its debounced physical release.
                joy_inhibit|=joy_state;buttons_inhibit|=mouse_buttons;
                if((joy_state||mouse_buttons)&&hmm_core->log)
                    hmm_core->log("HMM2 INPUT gap=%u joy=%u mouse=%u",(unsigned)(now-keys_time),joy_state,mouse_buttons);
                joy_state=mouse_buttons=0;joy_raw=buttons_raw=0xFF;
            }
            {const uint8_t j=p[0]&31;if(j==joy_raw){joy_inhibit&=j;joy_state=j&~joy_inhibit;}joy_raw=j;}
            keys_time=now;
            for(unsigned i=4;i<n;i++) {
                if((uint8_t)(ps2_head+1)==ps2_tail) { ps2_lost=1; break; }
                ps2_ring[ps2_head++]=p[i];++ps2_produced;
            }
            mouse_packet(p+1,stale);
            hmm_core->mutex_unlock(io_mutex); ok=1;
        }
        hmm_core->reply(0xF5,&ok,1);
    } else if(cmd==0x73) {
        uint8_t status[6+95+1]={(uint8_t)__atomic_load_n(&hmm_state,__ATOMIC_ACQUIRE),1,0,0,0,0};
        unsigned length=6;
        if(status[0]==255) {
            unsigned n=(unsigned)strlen(hmm_error)+1;
            memcpy(status+6,hmm_error,n);length+=n;
        }
        wr32(status+2,hmm_frames);
        /* Codex: ABI разрешает app_done только из command, после ответа о завершении. */
        if(hmm_core->reply(0xF3,status,length)==ZM_OK &&
           __atomic_load_n(&hmm_done,__ATOMIC_ACQUIRE) && (status[0]==3 || status[0]==255) &&
           hmm_core->size>=offsetof(zm_core_t,app_done)+sizeof(hmm_core->app_done) && hmm_core->app_done)
            hmm_core->app_done();
    } else return ZM_ERROR;
    return ZM_OK;
}
int hmm_io_init(void){
    io_signal=hmm_core->signal_create();io_mutex=hmm_core->mutex_create();
    request_id=0;awaiting=graphics_awaiting=fs_awaiting=0;response=0;
    ps2_head=ps2_tail=ps2_lost=0;ps2_produced=ps2_consumed=0;
    mouse_head=mouse_tail=0;mouse_read_buttons=mouse_queued_buttons=mouse_lost=0;mouse_buttons=0;
    mouse_gui_x=320;mouse_gui_y=240;
    mouse_sync=0;mouse_dx=mouse_dy=0;
    return io_signal && io_mutex;
}
