/* Codex: граница платформы; одинаковые операции используются ELF и стендом ПК.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hmm_file hmm_file;
uint64_t hmm_millis(void);
void hmm_delay(uint32_t ms);
extern uint32_t hmm_slept_ms,hmm_gfx_bytes,hmm_decode_bytes,hmm_sfx_bytes,hmm_loop_bytes;
void hmm_log(const char *text);
int hmm_video_open(void);
void hmm_video_frame(const uint8_t *pixels, const uint8_t *rgb, int width, int height);
void hmm_video_close(void);
void hmm_gpu_init(void);
void hmm_gpu_frame(const uint8_t *pixels,const uint8_t *rgb,int width,int height);
void hmm_gpu_stats(uint32_t *bytes,uint32_t *prepare,uint32_t *transfer);
/* Клод: участок, где задача курсора не трогает сопроцессор и DL; вложенность разрешена. */
/* Клод: начало восстановленной памяти упакованных данных ELF занято звуком (OGG и окна звуков); дальше — базовые окна кэша DAT. */
#define HMM_RECYCLED_AUDIO_BYTES (144u*1024u)
void hmm_gpu_busy_enter(void);
void hmm_gpu_busy_leave(void);
/* Клод: мелкое чтение из HMM2.DAT через кэш окон (hmm_files.c): каждый кусок идёт с карты один раз. */
size_t hmm_package_read(uint32_t offset,void *dst,size_t count);
void hmm_input_init(void);
uint32_t hmm_input_polled(void);
void hmm_loading_mouse(int *x,int *y);
int hmm_mouse_next(int *dx,int *dy,int *buttons,int *x,int *y,uint32_t *when);
void hmm_mouse_position(int *x,int *y);
void hmm_input_frame(void);
void hmm_input_stats(uint32_t *out,int reset);
hmm_file *hmm_fopen(const char *path, const char *mode);
int hmm_fclose(hmm_file *file);
uint32_t hmm_file_offset(hmm_file *file);
size_t hmm_fread(void *dst, size_t size, size_t count, hmm_file *file);
size_t hmm_fwrite(const void *src, size_t size, size_t count, hmm_file *file);
long hmm_ftell(hmm_file *file);
int hmm_fseek(hmm_file *file, long offset, int whence);
int hmm_is_file(const char *path);
int hmm_is_dir(const char *path);
int hmm_mkdir(const char *path);
int hmm_unlink(const char *path);
typedef void (*hmm_dir_callback)(const char *name, void *user);
void hmm_list_dir(const char *path, hmm_dir_callback callback, void *user);
/* Codex: очередь хранит формат SDL_Event только для штатного обработчика OpenHMM2. */
int hmm_poll_event(void *event);
int hmm_modifiers(void);
int hmm_game_main(int argc, char **argv);
#ifdef __cplusplus
}
#endif
