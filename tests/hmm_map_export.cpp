// Native-only inventory of actual decoded bundled-map text, not binary guesses.
#if defined(HMM2_HOST) && !defined(HMM2_PACKED)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
extern "C" void hmm_export_map_text(const char *kind,const char *text){
    static FILE *file;
    if(!text||!*text)return;
    if(!file){const char *path=std::getenv("HMM2_MAP_TEXT_EXPORT");if(!path)return;file=std::fopen(path,"wb");if(!file)std::abort();}
    const uint32_t lengths[]={uint32_t(std::strlen(kind)),uint32_t(std::strlen(text))};
    if(std::fwrite(lengths,sizeof(lengths),1,file)!=1||std::fwrite(kind,1,lengths[0],file)!=lengths[0]||std::fwrite(text,1,lengths[1],file)!=lengths[1])std::abort();
    std::fflush(file);
}
#endif
