/* Codex: штатный OGG VDAC4, исходные SFX и независимый вывод PCM. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void hmm_audio_tick(void);
void hmm_audio_poll(void);
void hmm_audio_manager_tick(void);
void hmm_audio_shutdown(void);
int hmm_audio_busy(void);
/* Клод: вызов идёт из задачи звука. */
int hmm_audio_self(void);
#ifdef __cplusplus
}
#endif
