/* Optional Russian catalogue and prebuilt FT812 resources. GPL-2.0-or-later. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { HMM_RU_GROUP=0x1000u, HMM_RU_SOURCE=0x80000000u, HMM_RU_INFLATE=0x40000000u };
int hmm_ru_prepare(void);
int hmm_ru_active(void);
const char *hmm_ru_lookup(const char *text);
/* Copied by the caller before another authored-map lookup; UI pointers stay stable. */
const char *hmm_ru_map_lookup(const char *text);
int hmm_ru_map_answer(const char *original,const char *answer,int truncated);
extern unsigned hmm_locale_io_busy;
uint32_t hmm_ru_count(unsigned group);
int hmm_ru_frame(unsigned group,unsigned index);
void hmm_ru_record(unsigned group,unsigned index,void *record);
void hmm_ru_read(uint32_t offset,void *data,size_t size);
void hmm_ru_graphics(uint32_t offset,uint32_t address,uint32_t size);
#ifdef __cplusplus
}
struct HmmLocaleReadGuard {
    HmmLocaleReadGuard(){++hmm_locale_io_busy;}
    ~HmmLocaleReadGuard(){--hmm_locale_io_busy;}
    HmmLocaleReadGuard(const HmmLocaleReadGuard&)=delete;
    HmmLocaleReadGuard& operator=(const HmmLocaleReadGuard&)=delete;
};
#endif
