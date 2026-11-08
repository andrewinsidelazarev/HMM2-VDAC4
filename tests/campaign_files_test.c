/* Codex: настоящий файловый слой; только удалённое чтение заменено файлом на ПК. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
uint32_t hmm_package_size;
int hmm_read_hold,hmm_window_owner;
volatile int hmm_running=1;
const zm_core_t *hmm_core;
static FILE *source;
void hmm_fail(const char *s){fprintf(stderr,"%s\n",s);exit(2);}
size_t hmm_remote_read(uint32_t at,void *p,size_t n){assert(n<=32768);assert(!fseek(source,(long)at,SEEK_SET));return fread(p,1,n,source);}
int hmm_fs_call(unsigned op,const char *name,const char *other,uint32_t at,void *data,unsigned n,uint32_t *size){return 1;}
#include "../main/hmm_files.c"
static unsigned found;
static void listing(const char *name,void *unused){assert(name&&*name&&!strchr(name,'/'));found++;}
int main(int argc,char **argv){
    assert(argc==2);source=fopen(argv[1],"rb");assert(source);fseek(source,0,SEEK_END);hmm_package_size=(uint32_t)ftell(source);
    if(!catalogue()){puts("INVALID");return 2;}
    unsigned names=0,maps=0;unsigned char got[4096],expected[4096];unsigned long long total=0;
    for(unsigned i=0;i<entry_count;i++){
        const struct entry *e=entries+i;names+=(unsigned)strlen(e->path)+1;
        char upper[128];for(unsigned j=0;j<=strlen(e->path);j++)upper[j]=(char)toupper((unsigned char)e->path[j]);
        assert(hmm_is_file(upper));hmm_file *f=hmm_fopen(upper,"rb");assert(f&&hmm_file_offset(f)==e->offset);
        for(uint32_t at=0;at<e->size;){
            unsigned n=e->size-at<sizeof(got)?e->size-at:sizeof(got);
            assert(hmm_fread(got,1,n,f)==n);
            unsigned done=0;
            if(e->prefix&&at<512){unsigned k=512-at<n?512-at:n;fseek(source,e->prefix+at,SEEK_SET);assert(fread(expected,1,k,source)==k);done=k;}
            if(done<n){fseek(source,e->offset+at+done,SEEK_SET);assert(fread(expected+done,1,n-done,source)==n-done);}
            assert(!memcmp(got,expected,n));at+=n;total+=n;
        }
        assert(!hmm_fread(got,1,1,f));assert(!hmm_fseek(f,0,SEEK_SET));assert(hmm_ftell(f)==0);assert(!hmm_fclose(f));
        maps+=!strncmp(e->path,"maps/",5);
    }
    hmm_list_dir("MAPS",listing,0);assert(found==maps);
    printf("FILES=%u MAPS=%u BYTES=%llu XTENSA_CATALOG=%u\n",entry_count,maps,total,entry_count*16+names);
    fclose(source);return 0;
}
