/* Codex: межпоточная публикация 32-битных флагов в стенде MSVC; Xtensa использует атомики GCC. */
#include <intrin.h>
#define __ATOMIC_RELEASE 0
#define __ATOMIC_ACQUIRE 0
#define __atomic_store_n(p,v,o) _InterlockedExchange((volatile long*)(p),(long)(v))
#define __atomic_exchange_n(p,v,o) _InterlockedExchange((volatile long*)(p),(long)(v))
#define __atomic_load_n(p,o) _InterlockedCompareExchange((volatile long*)(p),0,0)
