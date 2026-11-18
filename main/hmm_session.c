/* Codex: общий для ELF и совместного стенда порядок остановки сеанса. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include "hmm_audio.h"
#include "hmm_clock.h"
#include <string.h>

void hmm_session_reset(void){
    hmm_clock_reset();
    hmm_error[0]=0;hmm_frames=0;hmm_state=0;hmm_done=0;hmm_running=1;
}
void hmm_session_request_stop(void){
    __atomic_store_n(&hmm_running,0,__ATOMIC_RELEASE);
    hmm_io_cancel();
}
void hmm_session_task(void *arg){
    (void)arg;int result=0;
    while(hmm_running && !__atomic_load_n(&hmm_state,__ATOMIC_ACQUIRE))hmm_core->delay(1);
    if(hmm_running){
        char name[]="HMM2.ELF";char *args[]={name,0};
        hmm_core->log("HMM2: вход в main OpenHMM2 1.1.17");
        result=hmm_game_main(1,args);
        hmm_core->log("HMM2: main завершён, код=%d",result);
    }
    if(result && hmm_running && !hmm_error[0])strcpy(hmm_error,"Game main failed");
    hmm_audio_shutdown();
    hmm_session_request_stop();
    /* Codex: игра больше не пишет RAM_G/DL; завершение публикуется после освобождения видео. */
    hmm_video_close();
    hmm_runtime_close();
    __atomic_store_n(&hmm_state,hmm_error[0]?255:3,__ATOMIC_RELEASE);
    __atomic_store_n(&hmm_done,1,__ATOMIC_RELEASE);
}
void hmm_session_stop(void){
    hmm_session_request_stop();
    uint32_t start=hmm_core->millis();
    /* stop() runs on the firmware LINK loop. Keep that loop responsive: the
     * host owns application tasks and forcibly removes any unfinished task
     * after this callback returns. Session cleanup is cooperative, not required
     * to wait for an unresponsive transport during Tang reconfiguration. */
    while(!__atomic_load_n(&hmm_done,__ATOMIC_ACQUIRE) &&
          (uint32_t)(hmm_core->millis()-start)<500)hmm_core->delay(1);
    if(!hmm_done)hmm_core->log("HMM2: stop: worker did not finish; firmware will stop the task");
}
