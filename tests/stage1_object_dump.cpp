/* Codex: полная сериализация таблиц объектов оригинала для проверки всех полей и порядка. */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
static FILE *output;
static void value(uint32_t v){do{unsigned b=v&127;v>>=7;if(v)b|=128;fputc(b,output);}while(v);}
static void part(const Maps::ObjectPartInfo &p){
    value(static_cast<uint32_t>(p.tileOffset.x)<<1 ^ static_cast<uint32_t>(p.tileOffset.x>>31));
    value(static_cast<uint32_t>(p.tileOffset.y)<<1 ^ static_cast<uint32_t>(p.tileOffset.y>>31));
    value(p.icnIndex);value(p.objectType);value(p.icnType);value(p.animationFrames);
}
int main(int argc,char**argv){
    if(argc!=2)return 1;output=fopen(argv[1],"wb");if(!output)return 2;
    unsigned objects=0,parts=0;
    for(unsigned g=0;g<static_cast<unsigned>(Maps::ObjectGroup::GROUP_COUNT);g++){
        const auto&group=Maps::getObjectsByGroup(static_cast<Maps::ObjectGroup>(g));value(static_cast<uint32_t>(group.size()));
        for(const auto&o:group){
            objects++;value(o.objectType);value(o.metadata[0]);value(o.metadata[1]);
            value(static_cast<uint32_t>(o.groundLevelParts.size()));value(static_cast<uint32_t>(o.topLevelParts.size()));
            for(const auto&p:o.groundLevelParts){part(p);value(p.layerType);parts++;}
            for(const auto&p:o.topLevelParts){part(p);parts++;}
        }
    }
    fclose(output);printf("objects=%u parts=%u\n",objects,parts);return 0;
}
