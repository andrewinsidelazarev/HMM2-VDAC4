/* Codex: точный палитровый кадр; загрузка идёт в непоказанный буфер RAM_G.
 * Перед повторным использованием буфера подтверждаем завершение DLSWAP.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "hmm_platform.h"
#include "hmm_core.h"
#include <string.h>
static volatile int opened;
uint32_t hmm_slept_ms,hmm_decode_bytes,hmm_sfx_bytes,hmm_loop_bytes;
static uint32_t checked_at;
static int tang_personality(void){
    uint32_t idv[2];
    return hmm_core->link_read(0,idv,2)==ZM_OK && idv[0]==0x56444334u ? (int)(idv[1]>>24) : -1;
}
void hmm_video_check_core(void){
    if(__atomic_load_n(&opened,__ATOMIC_ACQUIRE)!=2 || !hmm_running)return;
    if(tang_personality()==1){
        hmm_core->log("HMM2: Tang core 02 -> 01 (slot 0), stopping");
        hmm_session_request_stop();
    }
}
void hmm_video_poll(uint32_t now){
    /* Codex: опрос ядра независим от SD, подготовки сцены и пакетов Z80. Ошибка LINK не означает SLOT0. */
    if((uint32_t)(now-checked_at)<100)return;
    checked_at=now;hmm_video_check_core();
}
static uint32_t returned,interval,engine_ms,prepare_ms,transfer_ms,swap_ms,link_bytes,sd_previous,sd_wait_previous,sd_requests_previous,slept_previous,raster_max,gfx_previous,decode_previous,sfx_previous;
static void wait_swap(void){
    uint32_t start=hmm_core->millis(),swap;
    do{if(!hmm_running)hmm_fail("Application stopped");if(hmm_core->ft_read(0x302054,&swap)!=ZM_OK)hmm_fail("FT812 DLSWAP read failed");if(!swap)return;if((uint32_t)(hmm_core->millis()-start)>2000)hmm_fail("FT812 DLSWAP timeout");hmm_core->delay(1);}while(1);
}
int hmm_video_open(void){
    if(opened)return opened==2;
    /* Codex: даже неуспешный ft_open мог уже захватить FT812; общий выход обязан вызвать ft_close. */
    opened=1;
    /* Клод: на холодном старте платы Evo запускает игру раньше, чем готова связь с Tang (сборка 97, 04.11: вход в main на 10,7 с,
     * связь с Tang — на 11,8 с). Прежде первая же неудачная проверка возвращала 0 навсегда (opened=1), игра шла дальше без
     * hmm_gpu_init: кэш RAM_G оставался нулевым, ни одна текстура не помещалась («layer skipped», free=3072), и первый кадр ролика
     * завершал игру «RAM_G clip capacity». Теперь ядро FT812 ждём до 20 с; не дождались — понятная ошибка, а не игра вслепую. */
    const int open_result=hmm_core->ft_open(15000);
    int personality=-1;uint32_t status=0;const uint32_t start=hmm_core->millis();
    if(open_result==ZM_OK)for(;;){
        personality=tang_personality();
        if(personality==2&&hmm_core->link_read(0x5c,&status,1)==ZM_OK)break;
        if((uint32_t)(hmm_core->millis()-start)>20000)break;
        if(!hmm_running)hmm_fail("Application stopped");
        hmm_core->delay(50);
    }
    hmm_core->log("HMM2: video open: ft_open=%d personality=%d status=%08lx after %lu ms",open_result,personality,(unsigned long)status,(unsigned long)(hmm_core->millis()-start));
    if(open_result!=ZM_OK)hmm_fail("FT812 open failed");
    if(personality!=2)hmm_fail("Tang FT812 core not ready");
    /* Codex: бит 2 сброшен: SPI Evo доставляет RAM_G, порт слов ESP обслуживает DL. */
    uint32_t cfg=1u|(((status>>13)&1u)<<1);
    if(hmm_core->link_write(0x5c,&cfg,1)!=ZM_OK)hmm_fail("FT812 config write failed");
    /* Клод: кэш текстур рассчитан на RAM_G 3 МБ (HFT_PAGES); без режима 3 МБ (FT_STAT бит 27) верхний мегабайт — шрифты ПЗУ,
     * запись туда не доходит и на экран шёл бы мусор. Проверяем сразу после записи FT_CFG. */
    if(hmm_core->link_read(0x5c,&status,1)!=ZM_OK||!(status&(1u<<27)))hmm_fail("FT812 RAM_G 3 MB mode is off");
    checked_at=hmm_core->millis();__atomic_store_n(&opened,2,__ATOMIC_RELEASE);
    hmm_gpu_init();returned=interval=hmm_core->millis();return 1;
}
void hmm_video_close(void){
    if(!opened)return;
    extern void hmm_gpu_shutdown(void);hmm_gpu_shutdown();
    __atomic_store_n(&opened,0,__ATOMIC_RELEASE);
    int personality=tang_personality(),failed=0;uint32_t status=0;
    /* Codex: чтение RAM_G завершает предшествующие записи очередей. В SLOT0 окна FT812 уже нет. */
    if(personality==2 && hmm_core->ft_read(0,&status)!=ZM_OK){
        hmm_core->log("HMM2: exit: RAM_G fence failed");failed=1;
    }
    hmm_core->ft_close();
    /* Codex: сброс мог произойти во время закрытия; не отправляем BOOT по устаревшему чтению. */
    personality=tang_personality();
    /* Codex: аппаратный SLOT0 уже сбросил очереди FT812; ошибка чтения исчезнувшего окна ожидаема. */
    if(personality==1)failed=0;
    if(personality==2){
        hmm_core->log("HMM2: exit - back to slot 0");
        if(!hmm_core->core_back || hmm_core->core_back(10000)!=ZM_OK)failed=1;
    }
    /* Codex: ft_close возвращает void; проверяем наблюдаемый результат, а не только факт вызова. */
    if(tang_personality()!=1 || hmm_core->link_read(0x5c,&status,1)!=ZM_OK || (status&(1u<<12)))failed=1;
    if(failed && !hmm_error[0])strcpy(hmm_error,"Video shutdown failed");
    hmm_core->log("HMM2: video closed, slot0=%d",!failed);
}
void hmm_video_frame(const uint8_t *pixels,const uint8_t *rgb,int w,int h){
    /* Window destructors can render during error unwinding. Preserve the
     * original failure and let the session release its tasks and FT host. */
    if(!hmm_running||hmm_error[0]||hmm_exception_pending())return;
    if(w!=640||h!=480)hmm_fail("Unsupported game resolution");
    // Клод: кадр без открытого FT812 (hmm_gpu_init не было) — ошибка, а не рисование в нулевой кэш RAM_G (сборка 97 на плате).
    if(__atomic_load_n(&opened,__ATOMIC_ACQUIRE)!=2)hmm_fail("FT812 not opened");
    extern uint32_t hmm_gpu_pending_ms(void);
    uint32_t start=hmm_core->millis(),pending=hmm_gpu_pending_ms(),gap=start-returned;
    engine_ms+=gap>pending?gap-pending:0;
    wait_swap();swap_ms+=hmm_core->millis()-start;
    hmm_gpu_frame(pixels,rgb,w,h);hmm_input_frame();++hmm_frames;
    uint32_t bytes,prepare,transfer;hmm_gpu_stats(&bytes,&prepare,&transfer);
    link_bytes+=bytes;prepare_ms+=prepare;transfer_ms+=transfer;
    returned=hmm_core->millis();
    /* Клод: FT_RTIME (LINK 0x2C, [15:0]) — время последнего кадра растеризатора Tang в тактах 61,875 МГц / 1024. Его QEMU не моделирует:
     * худший кадр за 100 кадров идёт в HMM2 PERF (FT_ms), чтобы журнал платы показывал, не растеризатор ли держит кадр. */
    {uint32_t rtime;if(hmm_core->link_read(0x2c,&rtime,1)==ZM_OK&&(rtime&0xffffu)>raster_max)raster_max=rtime&0xffffu;}
    if(hmm_frames%100==0){
        uint32_t sd,sd_wait,sd_requests;hmm_sd_stats(&sd,&sd_wait,&sd_requests);
        uint32_t elapsed=returned-interval;if(!elapsed)elapsed=1;
        // Codex: суммы за 100 кадров в мс; SD_KiBs включает весь интервал игры.
        uint32_t input[6];hmm_input_stats(input,1);
        hmm_core->log("HMM2 INPUT edges=%lu queue_total=%lu queue_max=%lu frames=%lu present_total=%lu present_max=%lu",(unsigned long)input[0],(unsigned long)input[1],(unsigned long)input[2],(unsigned long)input[3],(unsigned long)input[4],(unsigned long)input[5]);
        // Клод: sleep — сон игры по своим таймерам (входит в engine): engine−sleep — счёт процессора и ожидание SD.
        hmm_core->log("HMM2 PERF frame=%lu N=100 wall=%lu engine=%lu sleep=%lu prepare=%lu LINK_ms=%lu DLSWAP=%lu LINK_Bpf=%lu SD_B=%lu SD_ms=%lu SD_req=%lu SD_KiBs=%lu",
            (unsigned long)hmm_frames,(unsigned long)elapsed,(unsigned long)engine_ms,(unsigned long)(hmm_slept_ms-slept_previous),(unsigned long)prepare_ms,(unsigned long)transfer_ms,
            (unsigned long)swap_ms,(unsigned long)(link_bytes/100),(unsigned long)(sd-sd_previous),(unsigned long)(sd_wait-sd_wait_previous),
            (unsigned long)(sd_requests-sd_requests_previous),(unsigned long)((uint64_t)(sd-sd_previous)*1000/elapsed/1024));
        /* Клод: отдельной короткой строкой — консоль ESP обрезает длинную строку, и хвост HMM2 PERF на плате терялся. Байты за 100 кадров:
         * E6 — картинки с SD прямо в RAM_G, dec — кадры, распакованные для процессора, sfx — страницы звуков, loops — буферы петель сейчас. */
        hmm_core->log("HMM2 IO frame=%lu E6=%lu dec=%lu sfx=%lu loops=%lu FT_ms=%lu",(unsigned long)hmm_frames,(unsigned long)(hmm_gfx_bytes-gfx_previous),
            (unsigned long)(hmm_decode_bytes-decode_previous),(unsigned long)(hmm_sfx_bytes-sfx_previous),(unsigned long)hmm_loop_bytes,(unsigned long)(raster_max*1024u/61875u));
        {extern void hmm_memory_line(unsigned);hmm_memory_line(hmm_frames);}
        interval=returned;slept_previous=hmm_slept_ms;gfx_previous=hmm_gfx_bytes;decode_previous=hmm_decode_bytes;sfx_previous=hmm_sfx_bytes;raster_max=0;engine_ms=prepare_ms=transfer_ms=swap_ms=link_bytes=0;sd_previous=sd;sd_wait_previous=sd_wait;sd_requests_previous=sd_requests;
    }
}
