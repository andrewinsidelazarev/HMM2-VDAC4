// Codex: байты фикстуры стенда должны совпадать с исходным PCG32, без замены его алгоритма.
#include "../third_party/openhmm2/src/engine/rand.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    for (const uint64_t seed : {0ULL, 1ULL, 42ULL, 0xffffffffULL}) {
        Rand::PCG32 original(seed);
        static_assert(sizeof(original) == 16);
        uint64_t bytes[2];
        std::memcpy(bytes, &original, sizeof(bytes));
        const uint64_t expected = (55 + seed) * 6364136223846793005ULL + 55;
        assert(bytes[0] == expected && bytes[1] == 54);
        Rand::PCG32 restored(0);
        std::memcpy(&restored, bytes, sizeof(bytes));
        for (unsigned i = 0; i < 10000; ++i)
            assert(original() == restored());
    }
    std::puts("PCG32 fixture: original constructor bytes and 40000 outputs exact");
}
