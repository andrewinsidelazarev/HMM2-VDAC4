/* Codex: календарь игры задаётся RTC Ево, монотонные задержки остаются отдельными. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
void hmm_clock_reset(void);
int hmm_clock_sync(const uint8_t *rtc,size_t n);
uint64_t hmm_wall_seconds(void);
time_t hmm_time(time_t *out);
#ifdef __cplusplus
}
#endif
