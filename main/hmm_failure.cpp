/* Codex: ошибки транспорта доходят до штатного обработчика main OpenHMM2. */
extern "C" {
#include "hmm_core.h"
void hmm_video_check_core(void);
int hmm_audio_self(void);
}
#include <stdexcept>
#include <cstring>
#include <exception>
extern "C" int hmm_exception_pending(void){return std::uncaught_exceptions()!=0;}
extern "C" void hmm_window_restore_failed(void){
    if(!hmm_error[0])std::strcpy(hmm_error,"Window restore failed");
    hmm_session_request_stop();
    hmm_core->log("HMM2: window restore failed, session stopped");
}
extern "C" void hmm_fail(const char *message){
    /* Клод: ошибка задачи звука останавливает только звук. hmm_io_cancel здесь снял бы чтение игры, и та упала бы с чужой причиной
     * «short package read». Канал и окна задача к этому моменту отпустила сама (read_block, hmm_package_read). */
    if(hmm_audio_self()){hmm_core->log("HMM2: audio failure: %s",message);throw std::runtime_error(message);}
    /* Codex: FT может отказать раньше фонового poll после F12; сначала отличаем SLOT0 от неисправности. */
    hmm_video_check_core();
    if(hmm_running){std::strncpy(hmm_error,message,sizeof(hmm_error)-1);hmm_error[sizeof(hmm_error)-1]=0;}
    else message="Application stopped";
    // A fatal transport error ends the session before C++ unwinds windows.
    // Their destructors may request another frame; a failed FT must not turn
    // that restoration into a second exception and terminate the game task.
    hmm_session_request_stop();
    hmm_core->log("HMM2: failure: %s",message);
    throw std::runtime_error(message);
}

#ifdef __GNUC__
#include <cxxabi.h>
extern "C" const char *hmm_exception_type(void){const auto *type=__cxxabiv1::__cxa_current_exception_type();return type?type->name():"none";}
#endif
