/* Independent byte oracle for mixed seeks, interleaved large reads, EOF and
 * both cache boundaries. Uses the production file layer and real DAT. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t hmm_package_size;
int hmm_read_hold,hmm_window_owner;
volatile int hmm_running=1;
const zm_core_t *hmm_core;
static FILE *source;
static unsigned checks;
void hmm_fail(const char *s){fprintf(stderr,"%s\n",s);exit(2);}
size_t hmm_remote_read(uint32_t at,void *p,size_t n){assert(n<=32768&&at<=hmm_package_size&&n<=hmm_package_size-at);assert(!fseek(source,(long)at,SEEK_SET));return fread(p,1,n,source);}
int hmm_fs_call(unsigned op,const char *name,const char *other,uint32_t at,void *data,unsigned n,uint32_t *size){return 1;}
#include "../main/hmm_files.c"
static unsigned char got[70002],expected[70000];
static void verify(uint32_t at,unsigned n){
 memset(got,0xa5,sizeof(got));
 const size_t actual=read_package(at,got+1,n);
 size_t want=at>hmm_package_size?0:hmm_package_size-at;if(want>n)want=n;
 assert(actual==want&&got[0]==0xa5&&got[n+1]==0xa5);
 if(want){assert(!fseek(source,(long)at,SEEK_SET));assert(fread(expected,1,want,source)==want);assert(!memcmp(got+1,expected,want));}
 ++checks;
}
int main(int argc,char **argv){
 assert(argc==2);source=fopen(argv[1],"rb");assert(source);fseek(source,0,SEEK_END);hmm_package_size=(uint32_t)ftell(source);
 const unsigned lengths[]={0,1,17,2047,2048,2049,8191,8192,32767,32768,32769,65551,70000};
 const uint32_t offsets[]={0,31,2047,2048,32767,32768,65535,65536,199485952,hmm_package_size-17,hmm_package_size,hmm_package_size+1};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i)for(unsigned j=0;j<sizeof(lengths)/sizeof(*lengths);++j){verify(offsets[i],lengths[j]);verify(70170624+17,lengths[j]);}
 uint32_t rng=0x812125;for(unsigned i=0;i<2000;++i){rng=rng*1664525u+1013904223u;const uint32_t at=rng%hmm_package_size;rng=rng*1664525u+1013904223u;verify(at,rng%70001);}
 fclose(source);printf("FILE SPANS EXACT %u cases; mixed seeks, interleaving,2KiB/32KiB boundaries,EOF,canaries\n",checks);return 0;
}
