/* Codex: чтение пакета и сохранения через действующий файловый слой; носитель стенда — только tmp. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
uint32_t hmm_package_size;
static FILE*pack;static unsigned listed;
void hmm_fail(const char*s){puts(s);abort();}
size_t hmm_remote_read(uint32_t at,void*p,size_t n){assert(n<=32768);assert(!fseek(pack,(long)at,SEEK_SET));return fread(p,1,n,pack);}
int hmm_fs_call(unsigned op,const char*name,const char*other,uint32_t at,void*data,unsigned n,uint32_t*size){
    assert(strlen(name)<=12);assert(n<=7680);if(op==5)return remove(name);if(op==4)return rename(name,other);
    FILE*f=fopen(name,op==2?"wb":op==3?"r+b":"rb");if(!f)return errno==ENOENT?0x19:1;fseek(f,0,SEEK_END);if(size)*size=(uint32_t)ftell(f);fseek(f,(long)at,SEEK_SET);int r=0;
    if(op==1)r=fread(data,1,n,f)!=n;if(op==3)r=fwrite(data,1,n,f)!=n;fclose(f);return r;
}
static void listing(const char*name,void*u){(void)u;assert(!strcmp(name,"King_of_Death_day_1.sav"));++listed;}
int main(int argc,char**argv){
    (void)argv;pack=fopen("E:/zx/cache/tmp/hmm2_esp/stage1/data/HMM2.DAT","rb");assert(pack);fseek(pack,0,SEEK_END);hmm_package_size=(uint32_t)ftell(pack);
    assert(hmm_is_file("DATA/HEROES2.AGG"));assert(hmm_is_file("maps/GateHell.mp2"));
    hmm_file*f=hmm_fopen("maps/KINGDTH.MX2","rb");assert(f);unsigned char magic[4];assert(hmm_fread(magic,1,4,f)==4);assert(magic[0]==0x5c&&!magic[1]);assert(hmm_fseek(f,-4,2)==0);assert(hmm_fread(magic,1,4,f)==4);assert(!hmm_fread(magic,1,4,f));hmm_fclose(f);
    unsigned char bytes[2637],got[2637];for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)(i*37);
    const char*path="files/save/King_of_Death_day_1.sav";
    if(argc==1){f=hmm_fopen(path,"wb");assert(f);assert(hmm_fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));assert(!hmm_fclose(f));assert(hmm_save_commit());}
    f=hmm_fopen(path,"rb");assert(f);assert(hmm_fread(got,1,sizeof(got),f)==sizeof(got));assert(!memcmp(got,bytes,sizeof(got)));hmm_fclose(f);
    hmm_list_dir("files/save",listing,0);assert(listed==1);
    if(argc>1){assert(hmm_unlink(path));assert(!hmm_is_file(path));listed=0;hmm_list_dir("files/save",listing,0);assert(!listed);}
    fclose(pack);puts(argc>1?"FILES OK restart/list/read/delete":"FILES OK package/save/read");return 0;
}
