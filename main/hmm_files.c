/* Codex: файлы игры читаются порциями из HMM2.DAT штатным E4; записи — E5 DOOM.
 * Каталог содержит пути и смещения, данные и AGG целиком в PSRAM не загружаются.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
#include "hmm_core.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#define DISK_CHUNK 7680u
struct hmm_file {uint32_t base,size,pos;int packed,writable;union {char name[13];uint32_t prefix;};uint8_t *buf;uint32_t buf_at;unsigned buf_n;int dirty,failed;};
struct entry {const char *path;uint32_t offset,size,prefix;};
static struct entry *entries;
static uint32_t entry_count,age;
#define CACHE_PAGE 32768u
#define WINDOW 2048u
#define WINDOW_BASE 16u
#define WINDOW_MAX WINDOW_BASE
/* Codex: для 16 окон достаточно 2048 корзин; прежние 4096 отнимали ещё 4 КиБ
 * в тесном мире, где измеренный минимум составил 49868 байт. */
#define WINDOW_INDEX 2048u
#define WINDOW_NONE 0xffffffffu
extern int hmm_read_hold,hmm_window_owner;
/* Клод: окна читают два потока, игра и задача звука. Флаг, а не мьютекс: при hmm_fail его снимает hmm_io_cancel. */
#ifdef HMM2_HOST
#define WINDOW_ENTER() (hmm_window_owner=1)
#define WINDOW_LEAVE() (hmm_window_owner=0)
#else
extern int hmm_flag_take(int *flag);
#define WINDOW_ENTER() do{while(!hmm_flag_take(&hmm_window_owner)){if(!hmm_running)hmm_fail("Application stopped");hmm_core->delay(1);}}while(0)
#define WINDOW_LEAVE() __atomic_store_n(&hmm_window_owner,0,__ATOMIC_RELEASE)
#endif
/* Клод: одна страница длинного чтения и базовые окна в свободном хвосте восстановленной памяти ELF: 64 КиБ PSRAM обратно. На GateHell запас при загрузке был 33 КиБ. */
static uint8_t base_page[CACHE_PAGE];
#ifdef HMM2_HOST
static uint8_t window_base[WINDOW_BASE*WINDOW];
#else
extern unsigned char *hmm_recycled_data(size_t bytes);
static uint8_t *window_base;
#endif
static struct {uint32_t at,n;int valid;} page;
static uint32_t window_at[WINDOW_MAX];
static uint16_t window_index[WINDOW_INDEX];
#define WINDOW_RUN 4u
#define SEQ_STREAMS 8u
static uint32_t seq_next[SEQ_STREAMS];static unsigned seq_at;
static unsigned window_next,window_ready,window_busy,town_cache;
static void windows_reset(void){
#ifndef HMM2_HOST
    if(!window_base){uint8_t *recycled=hmm_recycled_data(HMM_RECYCLED_AUDIO_BYTES+WINDOW_BASE*WINDOW);window_base=recycled?recycled+HMM_RECYCLED_AUDIO_BYTES:malloc(WINDOW_BASE*WINDOW);if(!window_base)hmm_fail("Window cache memory");}
#endif
    memset(window_index,0,sizeof(window_index));for(unsigned i=0;i<WINDOW_MAX;i++)window_at[i]=WINDOW_NONE;window_next=0;window_ready=1;}
// Codex: базовые 32 КиБ остаются в восстановленной памяти ELF весь сеанс.
// Дополнительные окна переданы пулу средних блоков после измеренного отказа ИИ.
void hmm_files_plan(void *region,unsigned bytes){
    (void)region;(void)bytes;WINDOW_ENTER();windows_reset();WINDOW_LEAVE();
}
void hmm_files_trim(void){}
void hmm_files_menu_cache(int enable){(void)enable;}
void hmm_files_town_cache(int enable){town_cache=(unsigned)enable;}
unsigned hmm_files_cached(void){return 0;}
static uint8_t *window_data(unsigned slot){return window_base+slot*WINDOW;}
size_t hmm_package_read(uint32_t offset,void *dst,size_t count){
    if(offset>hmm_package_size)return 0;if(count>hmm_package_size-offset)count=hmm_package_size-offset;
    WINDOW_ENTER();if(!window_ready)windows_reset();
    size_t done=0;while(done<count){
        const uint32_t at=offset&~(WINDOW-1);const unsigned hash=((at/WINDOW)*2654435761u)>>21;
        unsigned slot=window_index[hash];
        if(slot&&window_at[slot-1]==at)--slot;
        else {
            // Codex: вытеснение только внутри уже выделенной области.
            if(window_next>=WINDOW_BASE)window_next=0;
            /* Клод: окно продолжает одно из недавних чтений — следующие WINDOW_RUN окон идут одним E4. Запрос на плате стоит около 14 мс, а 8 КиБ
             * читаются почти столько же: зацикленные звуки карты шли по 400 запросов на 100 кадров. Чтение вразброс остаётся по одному окну. */
            unsigned run=1,stream=seq_at;
            for(unsigned i=0;i<SEQ_STREAMS;i++)if(seq_next[i]==at){stream=i;run=WINDOW_RUN;break;}
            if(stream==seq_at)seq_at=(seq_at+1)%SEQ_STREAMS;
            /* Окна одного E4 лежат в памяти подряд: только внутри одного блока кэша. */
            const unsigned block_left=WINDOW_BASE-window_next;
            if(run>block_left)run=block_left;
            if((uint64_t)at+(uint64_t)run*WINDOW>hmm_package_size)run=1;
            slot=window_next;window_next+=run;
            for(unsigned i=0;i<run;i++)window_at[slot+i]=WINDOW_NONE;
            const uint32_t n=hmm_package_size-at<run*WINDOW?hmm_package_size-at:run*WINDOW;
            /* Клод: read_block обслуживает звук, уже получив данные. Вложенное чтение звука могло занять по кругу это же окно
             * и испортить данные игры (сборка 64 на плате), поэтому до закрытия окна звук ждёт. */
            window_busy=1;hmm_read_hold=1;const size_t got=hmm_remote_read(at,window_data(slot),n);hmm_read_hold=0;window_busy=0;
            if(got!=n){WINDOW_LEAVE();hmm_fail("Evo: short package read");return done;}
            for(unsigned i=0;i<run;i++){const uint32_t where=at+i*WINDOW;window_at[slot+i]=where;window_index[((where/WINDOW)*2654435761u)>>21]=(uint16_t)(slot+i+1);}
            seq_next[stream]=at+run*WINDOW;
        }
        size_t n=WINDOW-(offset-at);if(n>count-done)n=count-done;
        memcpy((uint8_t*)dst+done,window_data(slot)+(offset-at),n);done+=n;offset+=(uint32_t)n;
    }
    WINDOW_LEAVE();return done;
}
static uint32_t rd32(const uint8_t*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void normalize(const char *p,char out[128]){
    unsigned n=0;while(p[0]=='.'&&(p[1]=='/'||p[1]=='\\'))p+=2;
    while(*p&&n<127){char c=*p++;if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;out[n++]=c;}
    while(n&&out[n-1]=='/')--n;out[n]=0;if(n==1&&out[0]=='.')out[0]=0;
}
/* Keep up to 32 KiB, but fill only the requested 2 KiB-aligned span. Interleaved
 * window strips must not reload a whole 32 KiB page for each small source ROI. */
static size_t read_package(uint32_t offset,void *dst,size_t count){
    if(offset>hmm_package_size)return 0;if(count>hmm_package_size-offset)count=hmm_package_size-offset;
    size_t done=0;while(done<count){
        if(!page.valid||offset<page.at||offset-page.at>=page.n){
            const uint32_t at=offset&~(WINDOW-1);
            size_t n=count-done;if(n>CACHE_PAGE)n=CACHE_PAGE;
            n+=offset-at;if(n>CACHE_PAGE)n=CACHE_PAGE;
            n=(n+WINDOW-1)&~(WINDOW-1);
            page.valid=0;page.at=at;page.n=hmm_package_size-at;if(page.n>n)page.n=(uint32_t)n;
            if(hmm_remote_read(at,base_page,page.n)!=page.n){hmm_fail("Evo: short package read");return done;}
            page.valid=1;
        }
        size_t n=page.n-(offset-page.at);if(n>count-done)n=count-done;
        memcpy((uint8_t*)dst+done,base_page+(offset-page.at),n);done+=n;offset+=(uint32_t)n;
    }return done;
}
static int catalogue(void){
    if(entries)return 1;uint8_t h[16];if(read_package(0,h,sizeof(h))!=sizeof(h)||rd32(h)!=0x31465348)return 0;
    uint32_t version=rd32(h+4);if(version<1||version>4)return 0;
    /* Клод: FS4 — таблица FS2 (имя 60 байт + префикс заголовка карты) в хвосте DAT: пакет на карте пользователя FS2 с роликами,
     * места под 47 записей кампаний перед префиксами карт нет; данные при дописывании не сдвигаются, как у FS3. */
    const int fs2=version==2||version==4;
    /* Codex: FS3 хранит таблицу FS1 в хвосте DAT; старые данные при дописывании не сдвигаются. */
    uint32_t at=version>=3?rd32(h+12):16;
    uint32_t n=rd32(h+8);if(!n||n>2048||at<16||at>hmm_package_size||n*72>hmm_package_size-at)return 0;
    uint8_t *raw=malloc(n*72);if(!raw)return 0;
    if(read_package(at,raw,n*72)!=n*72){free(raw);return 0;}
    size_t names=0;
    for(uint32_t i=0;i<n;i++){
        const uint8_t *row=raw+i*72,*end=memchr(row,0,fs2?60:64);
        uint32_t offset=rd32(row+64),size=rd32(row+68),prefix=fs2?rd32(row+60):0;
        if(!end||offset>hmm_package_size||size>hmm_package_size-offset||
           (prefix&&((prefix&511)||prefix>hmm_package_size||512>hmm_package_size-prefix))){free(raw);return 0;}
        names+=(size_t)(end-row)+1;
    }
    /* Codex: нулевое дополнение 64-байтовых имён не держим в PSRAM: расширенный каталог
     * иначе отнимал последние 14 КиБ при загрузке сохранения GateHell. Один блок содержит записи и строки. */
    struct entry *table=malloc(n*sizeof(*table)+names);if(!table){free(raw);return 0;}
    char *name=(char*)(table+n);
    for(uint32_t i=0;i<n;i++){
        const uint8_t *row=raw+i*72;size_t length=strlen((const char*)row)+1;
        memcpy(name,row,length);table[i].path=name;name+=length;
        table[i].offset=rd32(row+64);table[i].size=rd32(row+68);table[i].prefix=fs2?rd32(row+60):0;
    }
    free(raw);
    entries=table;entry_count=n;return 1;
}
static struct entry *find(const char *path){
    if(!catalogue()){hmm_fail("HMM2.DAT: invalid catalogue");return 0;}
    char name[128];normalize(path,name);for(uint32_t i=0;i<entry_count;i++)if(!strcmp(name,entries[i].path))return entries+i;return 0;
}
static int save_name(const char *path,char name[13]){
    const char*p=strrchr(path,'/');p=p?p+1:path;size_t n=strlen(p);if(!n||n>12)return 0;
    for(size_t i=0;i<n;i++){char c=p[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return 0;name[i]=c;}name[n]=0;return 1;
}
/* Codex: FILEX принимает 8.3; имена диалога оригинала сохраняются в маленьком индексе.
 * Игра видит files/save, физические H2Sxxxxx.SAV лежат рядом с HMM2.DAT на SD Evo. */
static char saves[64][128];static int saves_ready;
static int save_index(void){
    if(saves_ready)return saves_ready>0;
    uint32_t size=0;if(hmm_fs_call(0,"HMM2.IDX",0,0,0,0,&size)){saves_ready=1;return 1;}
    if(size!=sizeof(saves)){saves_ready=-1;return 0;}
    for(unsigned at=0;at<sizeof(saves);){unsigned n=sizeof(saves)-at;if(n>DISK_CHUNK)n=DISK_CHUNK;
        if(hmm_fs_call(1,"HMM2.IDX",0,at,(char*)saves+at,n,0)){saves_ready=-1;return 0;}at+=n;}
    for(unsigned i=0;i<64;i++)if(!memchr(saves[i],0,128)||strchr(saves[i],'/')||strchr(saves[i],'\\')){saves_ready=-1;return 0;}
    saves_ready=1;return 1;
}
static int write_index(void){
    if(hmm_fs_call(2,"HMM2.IDX",0,0,0,0,0))return 0;
    for(unsigned at=0;at<sizeof(saves);){unsigned n=sizeof(saves)-at;if(n>DISK_CHUNK)n=DISK_CHUNK;
        if(hmm_fs_call(3,"HMM2.IDX",0,at,(char*)saves+at,n,0)){hmm_fs_call(6,"HMM2.IDX",0,0,0,0,0);return 0;}at+=n;}return !hmm_fs_call(6,"HMM2.IDX",0,0,0,0,0);
}
static int disk_name(const char *path,char name[13],int create){
    char normalized[128];normalize(path,normalized);
    if(strncmp(normalized,"files/save/",11))return save_name(path,name);
    if(strlen(path)>=128||!normalized[11]||strchr(normalized+11,'/')||!save_index())return 0;
    unsigned i;for(i=0;i<64;i++){char lower[128];normalize(saves[i],lower);if(!strcmp(lower,normalized+11))break;}
    if(i==64){if(!create)return 0;for(i=0;i<64&&saves[i][0];i++);if(i==64)return 0;
        const char*p=strrchr(path,'/');strcpy(saves[i],p?p+1:normalized+11);
        if(!write_index()){saves[i][0]=0;return 0;}}
    snprintf(name,13,"H2S%05u.SAV",i);return 1;
}
hmm_file *hmm_fopen(const char *path,const char *mode){
    struct entry *e=find(path);int write=strchr(mode,'w')!=0||strchr(mode,'a')!=0||strchr(mode,'+')!=0;
    if(e&&write)return 0;
    hmm_file *f=calloc(1,sizeof(*f));if(!f)return 0;
    if(e){f->base=e->offset;f->size=e->size;f->packed=1;f->prefix=e->prefix;return f;}
    if(!disk_name(path,f->name,write)){free(f);return 0;}
    uint32_t size=0;unsigned op=strchr(mode,'w')?2:0;
    if(hmm_fs_call(op,f->name,0,0,0,0,&size)){free(f);return 0;}
    f->writable=write;f->size=op==2?0:size;f->pos=strchr(mode,'a')?f->size:0;return f;
}
/* Codex: порция SD 7680 байт; последовательный поток открыт на Z80 до закрытия.
 * Малые чтения заголовка обслуживаются одним заполнением буфера. */
static int disk_flush(hmm_file *f){
    if(!f->dirty)return 1;f->dirty=0;
    if(hmm_fs_call(3,f->name,0,f->buf_at,f->buf,f->buf_n,0)){f->failed=1;f->buf_n=0;return 0;}
    return 1;
}
static int disk_buffer(hmm_file *f){if(!f->buf)f->buf=malloc(f->packed?960:DISK_CHUNK);return f->buf!=0;}
int hmm_fclose(hmm_file *f){int ok=disk_flush(f)&&!f->failed;
    /* Codex: SYNC нужен и после отказа flush; ошибку закрытия возвращаем вызывающему. */
    if(!f->packed&&hmm_fs_call(6,f->name,0,0,0,0,0))ok=0;
    free(f->buf);free(f);return ok?0:-1;}
uint32_t hmm_file_offset(hmm_file *f){if(!f||!f->packed)hmm_fail("Graphics must be in HMM2.DAT");return f->base;}
size_t hmm_fread(void *dst,size_t size,size_t count,hmm_file *f){
    if(!size||count>SIZE_MAX/size)return 0;size_t bytes=size*count;if(f->pos>=f->size)return 0;if(bytes>f->size-f->pos)bytes=f->size-f->pos;
    size_t done=0;
    if(f->packed)while(done<bytes){
        size_t n=bytes-done;uint32_t pos=f->pos+(uint32_t)done,at=f->base+pos;
        // Codex: FS2 хранит копии первых секторов карт подряд; логические байты файла прежние.
        if(f->prefix&&pos<512){if(n>512-pos)n=512-pos;at=f->prefix+pos;}
        /* Клод: карта читается через get8 по байту, и каждый байт брал мьютекс кэша окон: около 400 команд, больше половины загрузки новой карты (трасса QEMU).
         * Мелкие чтения — из своего буфера файла, в кэш окон один раз на 960 байт. */
        else if(bytes<=64&&disk_buffer(f)){
            if(pos<f->buf_at||pos-f->buf_at>=f->buf_n){
                const uint32_t left=f->size-pos;const unsigned want=left>960?960:(unsigned)left;
                const size_t got=hmm_package_read(at,f->buf,want);if(got!=want)break;
                f->buf_at=pos;f->buf_n=want;
            }
            size_t part=f->buf_n-(pos-f->buf_at);if(part>n)part=n;
            memcpy((uint8_t*)dst+done,f->buf+(pos-f->buf_at),part);done+=part;continue;
        }
        /* Клод: запись спрайта, кусок его пикселей и другие мелкие чтения — окнами: только нужное и один раз. */
        // Клод: в городе и длинные чтения (целая рамка окна, 145 КиБ) идут через окна: следующий раз — из PSRAM.
        size_t got=bytes<=4096||town_cache?hmm_package_read(at,(uint8_t*)dst+done,n):read_package(at,(uint8_t*)dst+done,n);done+=got;if(got!=n)break;
    }
    else if(disk_buffer(f))while(done<bytes){
        const uint32_t pos=f->pos+(uint32_t)done;
        if(pos<f->buf_at||pos-f->buf_at>=f->buf_n){
            if(!disk_flush(f))break;
            /* Codex: заголовок списка сохранений помещается в 1 КиБ; тело читаем полными порциями. */
            const unsigned limit=!pos&&bytes<=64?1024:DISK_CHUNK;
            const uint32_t left=f->size-pos;const unsigned n=left>limit?limit:(unsigned)left;
            f->buf_n=0;if(hmm_fs_call(1,f->name,0,pos,f->buf,n,0))break;
            f->buf_at=pos;f->buf_n=n;
        }
        size_t n=f->buf_n-(pos-f->buf_at);if(n>bytes-done)n=bytes-done;
        memcpy((uint8_t*)dst+done,f->buf+(pos-f->buf_at),n);done+=n;
    }
    f->pos+=(uint32_t)done;return done/size;
}
size_t hmm_fwrite(const void *src,size_t size,size_t count,hmm_file *f){
    if(!f->writable||!size||count>SIZE_MAX/size)return 0;size_t bytes=size*count,done=0;
    if(bytes>UINT32_MAX-f->pos)return 0;
    if(f->failed||!disk_buffer(f))return 0;
    while(done<bytes){
        if(!f->dirty||f->pos!=f->buf_at+f->buf_n||f->buf_n==DISK_CHUNK){if(!disk_flush(f))break;f->buf_at=f->pos;f->buf_n=0;f->dirty=1;}
        size_t n=DISK_CHUNK-f->buf_n;if(n>bytes-done)n=bytes-done;
        memcpy(f->buf+f->buf_n,(const uint8_t*)src+done,n);f->buf_n+=(unsigned)n;done+=n;f->pos+=(uint32_t)n;
    }
    if(f->pos>f->size)f->size=f->pos;return done/size;
}
long hmm_ftell(hmm_file*f){return (long)f->pos;}
int hmm_fseek(hmm_file*f,long offset,int whence){
    int64_t pos=(int64_t)offset+(whence==0?0:whence==1?f->pos:whence==2?f->size:INT64_MIN/2);
    if(pos<0||pos>LONG_MAX)return -1;f->pos=(uint32_t)pos;return 0;
}
int hmm_is_file(const char *path){if(find(path))return 1;char name[13];if(!disk_name(path,name,0))return 0;return !hmm_fs_call(0,name,0,0,0,0,0);}
int hmm_is_dir(const char *path){
    if(!catalogue())return 0;char name[128];normalize(path,name);size_t len=strlen(name);if(!len||!strcmp(name,"files/save"))return 1;
    for(uint32_t i=0;i<entry_count;i++)if(!strncmp(name,entries[i].path,len)&&entries[i].path[len]=='/')return 1;return 0;
}
int hmm_mkdir(const char *path){return hmm_is_dir(path);}
int hmm_unlink(const char *path){
    char name[13];if(find(path)||!disk_name(path,name,0)||hmm_fs_call(5,name,0,0,0,0,0))return 0;
    char normalized[128];normalize(path,normalized);if(!strncmp(normalized,"files/save/",11)){
        for(unsigned i=0;i<64;i++){char lower[128];normalize(saves[i],lower);if(!strcmp(lower,normalized+11)){saves[i][0]=0;return write_index();}}}
    return 1;
}
void hmm_list_dir(const char *path,hmm_dir_callback callback,void*user){
    if(!catalogue())return;char name[128];normalize(path,name);size_t len=strlen(name);
    for(uint32_t i=0;i<entry_count;i++){const char*p=entries[i].path;
        if(len){if(strncmp(p,name,len)||p[len]!='/')continue;p+=len+1;}
        if(*p&&!strchr(p,'/'))callback(p,user);
    }
    if(!strcmp(name,"files/save")&&save_index())for(unsigned i=0;i<64;i++)if(saves[i][0]){
        char disk[13];snprintf(disk,13,"H2S%05u.SAV",i);if(!hmm_fs_call(0,disk,0,0,0,0,0))callback(saves[i],user);}
}
