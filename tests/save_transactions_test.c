/* Actual file layer, in-memory E5 operations, injected status errors and resets.
 * No real player files or SD device are opened. */
#include "hmm_core.h"
#include "hmm_platform.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t hmm_package_size=1;
int hmm_read_hold,hmm_window_owner;
volatile int hmm_running=1;
const zm_core_t *hmm_core;
#define FILES 200
#define CAP 32768
static struct {int used;char name[13];unsigned size;unsigned char bytes[CAP];} disk[FILES];
static unsigned calls,inject_at,inject_mode,partial_index,stat_status;
static int triggered;
static FILE *record;
static jmp_buf power_cut;
static unsigned char old[128],other[211],replacement[19007],readback[CAP];
void hmm_fail(const char *s){fprintf(stderr,"Unexpected fatal: %s\n",s);abort();}
size_t hmm_remote_read(uint32_t at,void *data,size_t n){(void)at;(void)data;(void)n;abort();}
static int lookup(const char *name){for(unsigned i=0;i<FILES;i++)if(disk[i].used&&!strcmp(name,disk[i].name))return (int)i;return -1;}
static int create(const char *name){int i=lookup(name);if(i<0){for(i=0;i<FILES&&disk[i].used;i++);assert(i<FILES);disk[i].used=1;strcpy(disk[i].name,name);}disk[i].size=0;return i;}
int hmm_fs_call(unsigned op,const char *name,const char *dest,uint32_t at,void *data,unsigned n,uint32_t *size){
    assert(strlen(name)<=12&&(!dest||strlen(dest)<=12)&&n<=7680);
    if(op==0&&stat_status&&!strcmp(name,"HMM2.IDX")){unsigned status=stat_status;stat_status=0;return (int)status;}
    const int fire=calls++==inject_at;int i=lookup(name),result=0;
    if(fire){triggered=1;if(inject_mode==1)return 0x14;if(inject_mode==3)longjmp(power_cut,1);}
    if(op==0){if(i<0)result=0x19;else if(size)*size=disk[i].size;}
    else if(op==1){if(i<0||at>disk[i].size||n>disk[i].size-at)result=0x14;else memcpy(data,disk[i].bytes+at,n);}
    else if(op==2){create(name);}
    else if(op==3){
        assert(i>=0&&at+n<=CAP);
        unsigned take=n;if((fire&&inject_mode==2)||(partial_index&&!strcmp(name,"HMM2.ITM")&&at==7680))take=n/2;
        memcpy(disk[i].bytes+at,data,take);if(at+take>disk[i].size)disk[i].size=at+take;
        if(take!=n)result=0x14;
    }
    else if(op==4){if(i<0)result=0x19;else if(lookup(dest)>=0)result=0x14;else strcpy(disk[i].name,dest);}
    else if(op==5){if(i<0)result=0x19;else disk[i].used=0;}
    else assert(op==6);
    if(fire&&inject_mode==2)return result?result:0x14;
    if(fire&&inject_mode==4)longjmp(power_cut,1);
    if(record){
        fprintf(record,"OP %u %s %s %u %u %d %u ",op,name,dest?dest:"-",at,n,result,size&&result==0?*size:0);
        if(op==1||op==3)for(unsigned j=0;j<n;j++)fprintf(record,"%02x",((unsigned char*)data)[j]);
        fputc('\n',record);
    }
    return result;
}
#include "../main/hmm_files.c"
static void restart(void){
    static struct entry sentinel;entries=&sentinel;entry_count=0;
    saves_ready=0;memset(saves,0,sizeof(saves));memset(&save_pending,0,sizeof(save_pending));
    calls=0;inject_at=~0u;inject_mode=partial_index=stat_status=triggered=0;
}
static void put(const char *name,const void *bytes,unsigned n){int i=create(name);assert(n<=CAP);memcpy(disk[i].bytes,bytes,n);disk[i].size=n;}
static void fixture(void){
    memset(disk,0,sizeof(disk));restart();
    char index[8192]={0};strcpy(index,"old.sav");strcpy(index+128,"other.sav");
    put("HMM2.IDX",index,sizeof(index));put("H2S00000.SAV",old,sizeof(old));put("H2S00001.SAV",other,sizeof(other));
}
static unsigned read_save(const char *name){hmm_file *f=hmm_fopen(name,"rb");if(!f)return 0;unsigned n=(unsigned)hmm_fread(readback,1,CAP,f);assert(!hmm_fclose(f));return n;}
static void check_survivors(int new_name,int committed){
    restart();unsigned n=read_save("files/save/old.sav");
    assert((n==sizeof(old)&&!memcmp(readback,old,n))||(!new_name&&n==sizeof(replacement)&&!memcmp(readback,replacement,n)));
    if(committed&&!new_name)assert(n==sizeof(replacement));
    n=read_save("files/save/other.sav");assert(n==sizeof(other)&&!memcmp(readback,other,n));
    if(new_name){n=read_save("files/save/new.sav");assert(!n||(n==sizeof(replacement)&&!memcmp(readback,replacement,n)));if(committed)assert(n==sizeof(replacement));}
    int i=lookup("HMM2.IDX");assert(i>=0&&disk[i].size==8192);
}
static int save(const char *path,int commit){
    hmm_file *f=hmm_fopen(path,"wb");if(!f)return 0;
    int ok=hmm_fwrite(replacement,1,sizeof(replacement),f)==sizeof(replacement);
    /* The streamed zlib header is patched after writing the body. */
    if(ok){ok=!hmm_fseek(f,0,0)&&hmm_fwrite(replacement,1,16,f)==16;}
    if(hmm_fclose(f))ok=0;
    return ok&&commit&&hmm_save_commit();
}
static void test_recovery(void){
    for(unsigned phase=0;phase<3;phase++)for(unsigned kind=1;kind<=4;kind++)for(unsigned fault=0;fault<12;fault++){
        fixture();put("H2B00000.SAV",old,sizeof(old));
        if(phase==0)disk[lookup("H2S00000.SAV")].used=0;
        else put("H2S00000.SAV",replacement,sizeof(replacement));
        if(phase==2){int i=lookup("HMM2.IDX");put("HMM2.IBK",disk[i].bytes,8192);disk[i].bytes[0]='x';}
        inject_at=fault;inject_mode=kind;
        if(!setjmp(power_cut)){hmm_file *f=hmm_fopen("files/save/old.sav","rb");if(f)hmm_fclose(f);}
        check_survivors(0,0);unsigned n=read_save("files/save/old.sav");assert(n==sizeof(old)&&!memcmp(readback,old,n));
    }
}
int main(int argc,char **argv){
    memset(old,0x51,sizeof(old));memset(other,0x72,sizeof(other));
    for(unsigned i=0;i<sizeof(replacement);i++)replacement[i]=(unsigned char)(i*73+(i>>8));
    fixture();assert(save("files/save/old.sav",1));check_survivors(0,1);
    fixture();assert(save("files/save/new.sav",1));check_survivors(1,1);
    fixture();assert(!save("files/save/old.sav",0));check_survivors(0,0);assert(read_save("files/save/old.sav")==sizeof(old));
    fixture();assert(!save("files/save/new.sav",0));check_survivors(1,0);assert(!hmm_is_file("files/save/new.sav"));
    fixture();partial_index=1;assert(!save("files/save/new.sav",1));check_survivors(1,0);
    /* All status failures must fail closed, not just one errno value. */
    for(unsigned status=1;status<256;status++)if(status!=0x19){
        fixture();stat_status=status;assert(!save("files/save/new.sav",1));check_survivors(1,0);
    }
    fixture();disk[lookup("HMM2.IDX")].used=0;
    assert(save("files/save/new.sav",1));assert(!strcmp(saves[2],"new.sav"));
    assert(disk[lookup("H2S00000.SAV")].size==sizeof(old)&&!memcmp(disk[lookup("H2S00000.SAV")].bytes,old,sizeof(old)));
    assert(disk[lookup("H2S00001.SAV")].size==sizeof(other));
    fixture();int idx=lookup("HMM2.IDX");for(unsigned i=2;i<64;i++)snprintf((char*)disk[idx].bytes+i*128,128,"full%u.sav",i);
    assert(save("files/save/old.sav",1));check_survivors(0,1);assert(!save("files/save/new.sav",1));
    unsigned total=0;
    for(unsigned new_name=0;new_name<2;new_name++){
        fixture();assert(save(new_name?"files/save/new.sav":"files/save/old.sav",1));unsigned length=calls;
        for(unsigned kind=1;kind<=4;kind++)for(unsigned fault=0;fault<length;fault++){
            fixture();inject_at=fault;inject_mode=kind;volatile int committed=0;
            if(!setjmp(power_cut))committed=save(new_name?"files/save/new.sav":"files/save/old.sav",1);
            assert(triggered);check_survivors(new_name,committed);++total;
        }
    }
    test_recovery();
    unsigned deletes=0;
    fixture();assert(hmm_unlink("files/save/old.sav"));unsigned delete_calls=calls;
    for(unsigned kind=1;kind<=4;kind++)for(unsigned fault=0;fault<delete_calls;fault++){
        fixture();inject_at=fault;inject_mode=kind;
        if(!setjmp(power_cut))hmm_unlink("files/save/old.sav");
        restart();unsigned n=read_save("files/save/other.sav");assert(n==sizeof(other)&&!memcmp(readback,other,n));
        n=read_save("files/save/old.sav");assert(!n||(n==sizeof(old)&&!memcmp(readback,old,n)));
        /* Retrying deletion also clears a stale mapping after an earlier file
         * deletion succeeded but publishing its index update failed. */
        assert(hmm_unlink("files/save/old.sav")||!hmm_is_file("files/save/old.sav"));
        assert(!hmm_is_file("files/save/old.sav"));++deletes;
    }
    if(argc>1){
        record=fopen(argv[1],"wb");assert(record);
        for(unsigned fresh=0;fresh<2;fresh++){
            fixture();fprintf(record,"CASE %u\n",fresh);
            assert(save(fresh?"files/save/new.sav":"files/save/old.sav",1));
            for(unsigned i=0;i<FILES;i++)if(disk[i].used){
                fprintf(record,"FILE %s %u ",disk[i].name,disk[i].size);
                for(unsigned j=0;j<disk[i].size;j++)fprintf(record,"%02x",disk[i].bytes[j]);fputc('\n',record);
            }
        }
        fclose(record);record=0;
    }
    printf("SAVE TRANSACTIONS PASS: %u write/publication faults, 144 recovery faults, %u delete faults; old/new/abort/partial IDX/missing IDX/64 slots/seek header exact\n",total,deletes);
    return 0;
}
