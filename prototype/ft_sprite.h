// Codex: команды PALETTED8 и кэш RAM_G для двух списков; исходники игры здесь не меняются.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef HFT_HOST
#define HFT_API __declspec(dllexport)
#else
#define HFT_API
#endif
typedef struct {uint32_t source,palette;int32_t sw,sh,ix,iy,x,y,w,h,flip;} hft_sprite;
HFT_API uint32_t hft_emit(uint32_t *out,uint32_t capacity,const hft_sprite *s);
HFT_API uint32_t hft_compact(uint32_t *words,uint32_t count);
HFT_API int hft_precompose(uint8_t *history,int width,int height,const hft_sprite *s,
    const uint8_t *pixels,const uint8_t *mask,int copy,const uint8_t *transform,uint8_t *patch,uint8_t *patch_mask);
#define HFT_PAGE_BYTES 1024
/* Клод: RAM_G — 3 МиБ: hmm_video_open включает режим 3 МБ (LINK FT_CFG бит 0) и проверяет его по FT_STAT бит 27. */
#define HFT_PAGES 3072
/* Клод: мелкие текстуры (меньше 64 КиБ) сначала размещаются только ниже этой страницы; верх — большим картинкам (ft_sprite.c),
 * двум картинкам 640×480. */
#define HFT_SMALL_TOP (HFT_PAGES-640)
#define HFT_ENTRIES 1024
typedef struct {uint64_t key;uint32_t bytes,age;uint16_t page,pages;uint8_t pins,valid;} hft_entry;
// Клод: index — хеш ключей (открытая адресация, 0 — пусто, иначе номер записи + 1), вдвое больше записей. Перебор всех
// записей в PSRAM на каждый слой и его палитру занимал на ESP десятки миллисекунд кадра.
#define HFT_INDEX_BITS 11
#define HFT_INDEX (1<<HFT_INDEX_BITS)
/* Клод: free_low/free_high — свободных страниц ниже HFT_SMALL_TOP и выше; entry_hint — откуда искать свободную запись; clock — откуда
 * искать вытесняемую (ft_sprite.c). */
typedef struct {hft_entry entries[HFT_ENTRIES];uint8_t pages[HFT_PAGES];uint32_t age;int build_slot;unsigned last_entry,free_low,free_high,entry_hint,clock;uint16_t index[HFT_INDEX];} hft_cache;
HFT_API void hft_cache_init(hft_cache*c);
HFT_API void hft_cache_begin(hft_cache*c,int displayed_slot);
HFT_API int hft_cache_get(hft_cache*c,uint64_t key,uint32_t bytes,uint32_t *address,int *upload);
/* Клод: без побочных действий: уже в кэше (key, bytes) или есть сплошной свободный участок под bytes — без вытеснения. */
HFT_API int hft_cache_ready(const hft_cache*c,uint64_t key,uint32_t bytes);
#ifdef __cplusplus
}
#endif
