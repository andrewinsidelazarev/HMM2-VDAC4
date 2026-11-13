/* Recoverable save publication over the existing E5 file operations.
 * The old file is never truncated. A backup means publication did not finish:
 * recover it before another read/write. TMP is never a committed save.
 * HMM2.IDX retains the original 64 x 128-byte on-disk format.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#define SAVE_NOT_FOUND 0x19
static char saves[64][128];
static int saves_ready;
static struct {hmm_file *file;unsigned slot;int ready;char name[128];} save_pending;

static int disk_stat(const char *name,uint32_t *size){return hmm_fs_call(0,name,0,0,0,0,size);}
static int disk_sync(const char *name){return !hmm_fs_call(6,name,0,0,0,0,0);}
static void save_filename(char name[13],unsigned slot,char kind){snprintf(name,13,"H2%c%05u.SAV",kind,slot);}
static int disk_recover(const char *name,const char *backup){
    int status=disk_stat(backup,0);
    if(status==SAVE_NOT_FOUND)return 1;
    if(status)return 0;
    status=disk_stat(name,0);
    if(status&&status!=SAVE_NOT_FOUND)return 0;
    if(!status&&hmm_fs_call(5,name,0,0,0,0,0))return 0;
    return !hmm_fs_call(4,backup,name,0,0,0,0)&&disk_sync(name);
}
static int disk_replace(const char *name,const char *temp,const char *backup){
    if(!disk_recover(name,backup))return 0;
    const int status=disk_stat(name,0);
    if(status&&status!=SAVE_NOT_FOUND)return 0;
    if(!status&&hmm_fs_call(4,name,backup,0,0,0,0))return 0;
    if(hmm_fs_call(4,temp,name,0,0,0,0)||!disk_sync(name))return 0;
    /* Commit only after the complete new file has been synchronized. */
    return status||!hmm_fs_call(5,backup,0,0,0,0,0);
}
static int save_recover(unsigned slot,char name[13]){
    char backup[13];save_filename(name,slot,'S');save_filename(backup,slot,'B');
    return disk_recover(name,backup);
}
static int save_index(void){
    if(saves_ready)return 1;
    if(!disk_recover("HMM2.IDX","HMM2.IBK"))return 0;
    uint32_t size=0;const int status=disk_stat("HMM2.IDX",&size);
    if(status==SAVE_NOT_FOUND){memset(saves,0,sizeof(saves));saves_ready=1;return 1;}
    if(status||size!=sizeof(saves))return 0;
    for(unsigned at=0;at<sizeof(saves);){
        unsigned n=sizeof(saves)-at;if(n>DISK_CHUNK)n=DISK_CHUNK;
        if(hmm_fs_call(1,"HMM2.IDX",0,at,(char*)saves+at,n,0))return 0;
        at+=n;
    }
    for(unsigned i=0;i<64;i++)if(!memchr(saves[i],0,128)||strchr(saves[i],'/')||strchr(saves[i],'\\'))return 0;
    saves_ready=1;return 1;
}
static int write_index(void){
    int ok=!hmm_fs_call(2,"HMM2.ITM",0,0,0,0,0);
    for(unsigned at=0;ok&&at<sizeof(saves);){
        unsigned n=sizeof(saves)-at;if(n>DISK_CHUNK)n=DISK_CHUNK;
        ok=!hmm_fs_call(3,"HMM2.ITM",0,at,(char*)saves+at,n,0);at+=n;
    }
    if(!disk_sync("HMM2.ITM"))ok=0;
    if(ok)ok=disk_replace("HMM2.IDX","HMM2.ITM","HMM2.IBK");
    if(!ok)saves_ready=0;
    return ok;
}
static int save_lookup(const char *path,char logical[128]){
    char normalized[128];normalize(path,normalized);
    if(strlen(path)>=128||strncmp(normalized,"files/save/",11)||!normalized[11]||strchr(normalized+11,'/')||!save_index())return -1;
    const char *leaf=strrchr(path,'/');strcpy(logical,leaf?leaf+1:path);
    for(unsigned i=0;i<64;i++){char lower[128];normalize(saves[i],lower);if(!strcmp(lower,normalized+11))return (int)i;}
    return 64;
}
static int disk_name(const char *path,char name[13],int create){
    char normalized[128];normalize(path,normalized);
    if(strncmp(normalized,"files/save/",11))return save_name(path,name);
    char logical[128];const int slot=save_lookup(path,logical);(void)create;
    return slot>=0&&slot<64&&save_recover((unsigned)slot,name);
}
static int save_begin(const char *path,hmm_file *file){
    if(save_pending.file)return 0;
    save_pending.ready=0;
    char logical[128];int slot=save_lookup(path,logical);if(slot<0)return 0;
    if(slot==64){
        for(slot=0;slot<64;slot++)if(!saves[slot][0]){
            char name[13];save_filename(name,(unsigned)slot,'S');int status=disk_stat(name,0);
            if(status&&status!=SAVE_NOT_FOUND)return 0;
            if(!status)continue;
            save_filename(name,(unsigned)slot,'B');status=disk_stat(name,0);
            if(status&&status!=SAVE_NOT_FOUND)return 0;
            if(status==SAVE_NOT_FOUND)break;
        }
        if(slot==64)return 0;
    }
    char name[13];if(!save_recover((unsigned)slot,name))return 0;
    save_filename(file->name,(unsigned)slot,'T');
    if(hmm_fs_call(2,file->name,0,0,0,0,0))return 0;
    file->writable=1;save_pending.file=file;save_pending.slot=(unsigned)slot;
    strcpy(save_pending.name,logical);return 1;
}
int hmm_save_commit(void){
    if(save_pending.file||!save_pending.ready)return 0;
    save_pending.ready=0;
    if(!save_index())return 0;
    char name[13],temp[13],backup[13];const unsigned i=save_pending.slot;
    save_filename(name,i,'S');save_filename(temp,i,'T');save_filename(backup,i,'B');
    if(!disk_replace(name,temp,backup))return 0;
    if(saves[i][0])return 1;
    strcpy(saves[i],save_pending.name);return write_index();
}
