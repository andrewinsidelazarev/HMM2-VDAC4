/* Codex: внутренние функции приложения; внешняя граница остаётся zm_app.h. */
#pragma once
#include "zm_app.h"
#include <stdint.h>
#include <stddef.h>
extern const zm_core_t *hmm_core;
extern volatile int hmm_running,hmm_done,hmm_state;
extern uint32_t hmm_package_size,hmm_frames;
extern char hmm_error[96];
void hmm_fail(const char *message);
int hmm_exception_pending(void);
int hmm_io_init(void);
void hmm_io_cancel(void);
void hmm_session_reset(void);
void hmm_session_request_stop(void);
void hmm_session_task(void *arg);
void hmm_session_stop(void);
void hmm_video_poll(uint32_t now);
void hmm_runtime_close(void);
int hmm_command(uint8_t cmd,const uint8_t *p,uint16_t n);
size_t hmm_remote_read(uint32_t offset,void *data,size_t size);
void hmm_remote_graphics(uint32_t offset,uint32_t address,uint32_t size);
void hmm_sd_stats(uint32_t *bytes,uint32_t *wait,uint32_t *requests);
int hmm_fs_call(unsigned op,const char *name,const char *other,uint32_t offset,void *data,unsigned length,uint32_t *size);
size_t hmm_keys_take(uint8_t *out,size_t max,int *joy,int *stale);
int hmm_mouse_take(int *dx,int *dy);
