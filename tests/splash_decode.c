/* Codex: исполняем неизменённый ft_image.c BL616 и сохраняем настоящий выход RGB565. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ft_copro.h"

static void emit(void *u, const uint8_t *p, size_t n) { if (fwrite(p,1,n,(FILE *)u)!=n) abort(); }
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 3;
    fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);
    uint8_t *data=calloc(1,n+4);if(fread(data,1,n,f)!=n)return 4;fclose(f);
    void *state=calloc(1,FTC_IMG_BYTES),*inflate=calloc(1,FTC_INFLATE_BYTES);
    unsigned opts=atoi(argv[3]),chunk=atoi(argv[4]);size_t at=0,used=0;
    f=fopen(argv[2],"wb");if(!f)return 5;
    ftc_img_begin(state,opts,inflate);int status=FTC_IMG_NEED_INPUT;
    while(status==FTC_IMG_NEED_INPUT) {
        size_t k=n-at;if(k>chunk)k=chunk;
        status=ftc_img_run(state,data+at,k,&used,emit,f);at+=used;
        if(!used&&at>=n&&status==FTC_IMG_NEED_INPUT)break;
    }
    size_t size=ftell(f);fclose(f);ftc_img_info_t info;ftc_img_get_info(state,&info);
    printf("{\"status\":%d,\"bytes\":%zu,\"consumed\":%zu,\"fmt\":%u,\"width\":%u,\"height\":%u,\"stride\":%u,\"opts\":%u,\"chunk\":%u}\n",status,size,at,info.fmt,info.w,info.h,info.stride,opts,chunk);
    free(data);free(state);free(inflate);
    return status==FTC_IMG_DONE&&size==1024*768*2&&info.fmt==7&&info.w==1024&&info.h==768&&info.stride==2048?0:6;
}
