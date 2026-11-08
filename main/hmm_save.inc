/* Codex: формат SAV остаётся исходным; сжатие потоком убирает копии всего мира из PSRAM.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HMM2_PACKED
#include <zlib.h>
#include <cstring>
extern "C" void hmm_save_plan(z_stream *);
extern "C" void hmm_gpu_trim_unused();
namespace {
class HmmZipWriter final : public OStreamBase {
    StreamFile &file;z_stream z{};size_t header;uint8_t input[1024],output[1024];unsigned used=0;bool initialized=false;
    bool flush(int mode){
        z.next_in=input;z.avail_in=used;
        int status=Z_OK;
        do {
            z.next_out=output;z.avail_out=sizeof(output);status=deflate(&z,mode);
            if(status!=Z_OK&&status!=Z_STREAM_END){setFail();return false;}
            file.putRaw(output,sizeof(output)-z.avail_out);
            if(file.fail()){setFail();return false;}
        }while(z.avail_in||!z.avail_out||(mode==Z_FINISH&&status!=Z_STREAM_END));
        used=0;return true;
    }
    void put8(uint8_t v)override {if(fail())return;input[used++]=v;if(used==sizeof(input))flush(Z_NO_FLUSH);}
public:
    explicit HmmZipWriter(StreamFile &f):file(f),header(f.tell()){
        // Codex: граница сохранения отдаёт свободный запас кадров, сохраняя живые снимки и кэши мира.
        hmm_gpu_trim_unused();hmm_save_plan(&z);
        setBigendian(true);file<<uint32_t(0)<<uint32_t(0)<<uint16_t(0)<<uint16_t(0);
        initialized=deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,12,5,Z_DEFAULT_STRATEGY)==Z_OK;
        if(!initialized||file.fail())setFail();
    }
    ~HmmZipWriter()override{if(initialized)deflateEnd(&z);}
    void putBE16(uint16_t v)override{put8(v>>8);put8(v);}
    void putLE16(uint16_t v)override{put8(v);put8(v>>8);}
    void putBE32(uint32_t v)override{putBE16(v>>16);putBE16(v);}
    void putLE32(uint32_t v)override{putLE16(v);putLE16(v>>16);}
    void putRaw(const void *p,size_t n)override{const auto*b=static_cast<const uint8_t*>(p);while(n--&&!fail())put8(*b++);}
    bool finish(){
        if(fail()||!flush(Z_FINISH))return false;
        const size_t end=file.tell();file.seek(header);file<<uint32_t(z.total_in)<<uint32_t(z.total_out);file.seek(end);
        return !file.fail();
    }
};
class HmmZipReader final : public IStreamBase {
    StreamFile &file;z_stream z{};uint8_t input[1024],output[1024];
    uint32_t rawSize=0,zipSize=0,remaining=0,consumed=0;size_t start=0;unsigned pos=0,count=0;bool initialized=false,ended=false;
    bool fill(){
        hmm_loading_tick();
        pos=count=0;if(ended||fail())return false;
        while(!count&&!ended){
            if(!z.avail_in&&remaining){
                auto bytes=file.getRaw(std::min<size_t>(remaining,sizeof(input)));
                if(bytes.empty()||file.fail()){setFail();return false;}
                memcpy(input,bytes.data(),bytes.size());remaining-=bytes.size();z.next_in=input;z.avail_in=bytes.size();
            }
            z.next_out=output;z.avail_out=sizeof(output);const int result=inflate(&z,Z_NO_FLUSH);count=sizeof(output)-z.avail_out;
            if(result==Z_STREAM_END){ended=true;if(remaining||z.avail_in||z.total_out!=rawSize)setFail();}
            else if(result!=Z_OK||(!count&&!z.avail_in&&!remaining)){setFail();return false;}
            if(z.total_out>rawSize){setFail();return false;}
        }
        return count&&!fail();
    }
    uint8_t get8()override{
        if(pos==count&&!fill()){setFail();return 0;}++consumed;return output[pos++];
    }
public:
    explicit HmmZipReader(StreamFile &f):file(f){
        setBigendian(true);rawSize=file.get32();zipSize=file.get32();const auto version=file.get16();file.skip(2);start=file.tell();
        if(file.fail()||!zipSize||!rawSize||version){setFail();return;}
        initialized=inflateInit(&z)==Z_OK;if(!initialized){setFail();return;}remaining=zipSize;
        // Клод: прежде весь файл распаковывался для проверки и затем ещё раз: загрузка шла дважды, и по чтению с карты, и по распаковке.
        // Теперь проверяется длина; контрольную сумму zlib и размер проверяет единственный проход (fill и finish). Ошибка в файле уходит в меню тем же путём.
        if(file.size()-start<zipSize)setFail();
    }
    ~HmmZipReader()override{if(initialized)inflateEnd(&z);}
    void skip(size_t n)override{while(n--&&!fail())get8();}
    // Клод: чтение мира — почти всё по 2 и 4 байта: прямо из буфера, без виртуального get8 на байт (в трассе загрузки 37 % команд ядра игры).
    uint16_t getBE16()override{
        if(count-pos>=2){const uint16_t v=uint16_t(output[pos])<<8|output[pos+1];pos+=2;consumed+=2;return v;}
        const auto a=get8();return uint16_t(a)<<8|get8();}
    uint16_t getLE16()override{const auto a=get8();return a|uint16_t(get8())<<8;}
    uint32_t getBE32()override{
        if(count-pos>=4){const uint32_t v=uint32_t(output[pos])<<24|uint32_t(output[pos+1])<<16|uint32_t(output[pos+2])<<8|output[pos+3];pos+=4;consumed+=4;return v;}
        const auto a=getBE16();return uint32_t(a)<<16|getBE16();}
    uint32_t getLE32()override{const auto a=getLE16();return a|uint32_t(getLE16())<<16;}
    std::vector<uint8_t> getRaw(size_t n)override{
        if(!n)n=rawSize-consumed;if(n>rawSize-consumed){setFail();return {};}
        std::vector<uint8_t> bytes(n);for(auto &v:bytes)v=get8();return bytes;
    }
    bool finish(){if(consumed!=rawSize||pos!=count)return false;if(!ended)fill();return ended&&!fail()&&pos==count;}
};
}
#endif
