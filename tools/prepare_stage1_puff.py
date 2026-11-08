"""Codex: автономный puff из уже используемого zlib; до распаковки глобальные данные ещё недоступны."""
from prepare_stage1 import BASE

def prepare_puff():
    source=BASE/'runtime/zlib-1.3.2/contrib/puff'
    out=BASE/'module/puff';out.mkdir(parents=True,exist_ok=True)
    text=(source/'puff.c').read_text(encoding='utf-8')
    text=text.replace('#include <setjmp.h>', '''/* Codex: Xtensa использует встроенные сохранение и восстановление контекста,
 * чтобы не добавлять зависимости загрузочного участка от newlib. */
#ifdef HMM_PUFF_TARGET
#include <stdint.h>
typedef intptr_t jmp_buf[5];
#define setjmp(env) __builtin_setjmp(env)
#define longjmp(env, value) __builtin_longjmp(env, value)
#define HMM_BOOT __attribute__((section(".hmm_boot")))
#else
#include <setjmp.h>
#define HMM_BOOT
#endif
''')
    text=text.replace('static const short','static const HMM_BOOT short')
    text=text.replace('static int virgin','int virgin').replace('static short lencnt','short lencnt').replace('static short distcnt','short distcnt').replace('static struct huffman lencode','struct huffman lencode')
    (out/'puff.c').write_text('/* Codex: адаптация для начального восстановления данных ELF; исходный puff — zlib 1.3.2. */\n'+text,encoding='utf-8')
    (out/'puff.h').write_bytes((source/'puff.h').read_bytes())
    return out/'puff.c'
