/* Codex: восстановить инициализаторы данных до первого обращения runtime C++.
 * Код загружает и перемещает штатный загрузчик VDAC4; здесь нет записи в .text.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stddef.h>
extern int hmm_module_main(int,char**);
extern unsigned char __hmm_data_begin[];
__attribute__((section(".hmm_pack"),used))
const volatile uint32_t hmm_pack_header[12]={0};
/* Codex: после unpack упакованные инициализаторы больше не нужны. Их PSRAM
 * используется только аудиокэшем; заголовок, bootstrap и перемещения не меняются. */
unsigned char *hmm_recycled_data(size_t bytes){
    const volatile uint32_t *h=hmm_pack_header;
    if(h[0]!=0x314b5048||bytes>h[2])return NULL;
    return (unsigned char*)((uintptr_t)h+h[11]);
}

static int unpack(void){
    const volatile uint32_t *h=hmm_pack_header;
    if(h[0]!=0x314b5048||(h[10]!=2&&h[10]!=3))return 0;
    const uint32_t metadata=h[10]==3?h[4]:0;
    if(h[1]<4||h[1]>h[8]||metadata>h[8]-h[1])return 0;
    unsigned char *dst=__hmm_data_begin;
    const unsigned char *p=(const unsigned char*)h+h[11],*limit;
    extern int puff(unsigned char*,unsigned long*,const unsigned char*,unsigned long*);
    unsigned long decoded=h[1]+metadata,encoded=h[2];
    if(puff(dst,&decoded,p,&encoded)||decoded!=h[1]+metadata||encoded!=h[2])return 0;
    p=metadata?dst+h[1]:p+encoded;
    /* Codex: указатели на код используют уже отображённый адрес входа ELF.
     * Указатели на данные и конец массива — базу единой .bss приложения. */
    extern int main(int,char**);
    uintptr_t text=(uintptr_t)main-(h[9]-h[5]);
    limit=p+h[4];uint32_t offset=0;
    for(uint32_t i=0;i<h[3];i++){
        uint32_t delta=0,shift=0,v;
        do{if(p==limit||shift>=32)return 0;v=*p++;delta|=(v&127)<<shift;shift+=7;}while(v&128);
        if(delta>h[1]-4-offset)return 0;
        offset+=delta;
        if(offset&3)return 0;
        uint32_t *where=(uint32_t*)(dst+offset),value=*where;
        if(value>=h[5]&&value<h[5]+h[6])*where=text+value-h[5];
        else if(value>=h[7]&&value<=h[7]+h[8])*where=(uintptr_t)dst+value-h[7];
        else return 0;
    }
    if(p!=limit)return 0;
    for(uint32_t i=0;i<metadata;++i)dst[h[1]+i]=0;
    return 1;
}
int main(int argc,char**argv){
    if(argc!=3||!unpack())return 0;
    return hmm_module_main(argc,argv);
}
