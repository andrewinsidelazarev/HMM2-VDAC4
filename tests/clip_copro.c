/* Клод: кадры video/<имя>.smk через настоящий код сопроцессора BL616 (ft_copro.c, ft_image.c):
 * та же последовательность, что у hmm_gpu_clip_decode: кадр в FIFO целиком, затем команды.
 *   clip_copro клип.smk каталог_вывода номер_кадра...
 * Выбранные кадры сохраняются в RGB565, у остальных проверяется только завершение без ошибки. */
#define main host_test_main
#include "host_test.c"
#undef main
#define CLIP_FIFO_AT 0x2000u
#define CLIP_FIFO_SIZE (256u * 1024u)
#define CLIP_TARGET 0x80000u

static int ring(const uint32_t *w, uint32_t n)
{
    uint32_t wp = peek32(FTC_REG_CMD_WRITE);
    for (uint32_t i = 0; i < n; i++)
        poke32(FTC_RAM_CMD + ((wp + 4 * i) & 0xFFF), w[i]);
    poke32(FTC_REG_CMD_WRITE, (wp + 4 * n) & 0xFFF);
    return 0;
}

static int settle(long limit)
{
    while (peek32(FTC_REG_CMD_READ) != peek32(FTC_REG_CMD_WRITE) || ftc_poll(&cop)) {
        ftc_poll(&cop);
        frame_tick();
        if (peek32(FTC_REG_CMD_READ) == 0xFFF)
            return 1;
        if (--limit < 0)
            return 2;
    }
    return 0;
}

static int decode(const uint8_t *data, uint32_t size)
{
    const uint32_t fifo[] = {0xFFFFFF39u, CLIP_FIFO_AT, CLIP_FIFO_SIZE};
    ring(fifo, 3);
    int r = settle(100000);
    if (r)
        return r;
    /* Кадр целиком до команд: E6 не идёт во время декодирования (FT_STAT 25 на плате). */
    memcpy(mem + CLIP_FIFO_AT, data, size);
    poke32(0x309018u, size);
    const uint32_t frame[] = {0xFFFFFF40u, 0xFFFFFF41u, CLIP_TARGET, CLIP_FIFO_AT + CLIP_FIFO_SIZE, 0xFFFFFF00u};
    ring(frame, 5);
    return settle(50000000);
}

int main(int argc, char **argv)
{
    size_t n;
    uint8_t *clip = load(argv[1], &n);
    const uint32_t *h = (const uint32_t *)clip;
    if (h[0] != 0x31564D48u)
        return 2;
    const uint32_t w = h[1], ht = h[2], count = h[3];
    const uint32_t *table = h + 6;
    chip_reset();
    unsigned ok = 0, unique = 0;
    uint32_t last = ~0u;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t offset = table[i * 2], size = table[i * 2 + 1];
        int wanted = 0;
        for (int a = 3; a < argc; a++)
            wanted |= (uint32_t)atoi(argv[a]) == i;
        if (offset != last) {
            int r = decode(clip + offset, size);
            if (r) {
                printf("FAIL frame=%u result=%d fault=%d\n", i, r, cop.fault);
                return 1;
            }
            last = offset;
            ++unique;
        }
        ++ok;
        if (wanted) {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame_%04u.565", argv[2], i);
            FILE *f = fopen(path, "wb");
            if (!f)
                return 2;
            fwrite(mem + CLIP_TARGET, 2, (size_t)w * ht, f);
            fclose(f);
        }
    }
    printf("OK frames=%u unique=%u w=%u h=%u\n", ok, unique, w, ht);
    return 0;
}
