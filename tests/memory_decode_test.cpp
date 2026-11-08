// Codex: каждый сжатый спрайт DAT распаковывается с настоящими callbacks менеджера.
// Независимый проход zlib с обычной кучей даёт побайтный эталон.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <zlib.h>
#include "memory_decode_impl.inc"
static unsigned state_bytes,windows,save_peak;
static voidpf tracked(voidpf p,uInt n,uInt width){
    const unsigned bytes=n*width;
    if(bytes==decode_window)++windows;else state_bytes=std::max(state_bytes,bytes);
    void *result=decode_alloc(p,n,width);
    assert(result==decode_region||result==decode_region+decode_window);
    return result;
}
int main(int argc,char **argv){
    assert(argc==3);FILE *data=std::fopen(argv[1],"rb"),*index=std::fopen(argv[2],"rb");assert(data&&index);
    std::vector<uint8_t> region(decode_bytes),input,expected,actual,compressed;
    decode_region=region.data();unsigned records=0,record[3];
    while(std::fread(record,sizeof(record),1,index)==1){
        input.resize(record[1]);expected.resize(record[2]);actual.resize(record[2]);
        assert(!std::fseek(data,long(record[0]),SEEK_SET));assert(std::fread(input.data(),1,input.size(),data)==input.size());
        uLongf size=static_cast<uLongf>(expected.size());assert(uncompress(expected.data(),&size,input.data(),static_cast<uLong>(input.size()))==Z_OK&&size==record[2]);
        z_stream stream{};stream.zalloc=tracked;stream.zfree=decode_free;assert(inflateInit(&stream)==Z_OK);
        stream.next_in=input.data();stream.avail_in=static_cast<uInt>(input.size());int result=Z_OK;
        while(result==Z_OK){stream.next_out=actual.data()+stream.total_out;stream.avail_out=static_cast<uInt>(std::min<size_t>(1024,actual.size()-stream.total_out));result=inflate(&stream,Z_NO_FLUSH);}
        assert(result==Z_STREAM_END&&stream.total_out==expected.size()&&actual==expected);assert(inflateEnd(&stream)==Z_OK);++records;
        // Codex: тот же блок последовательно служит сохранению и следующей распаковке.
        // Обычный читатель zlib проверяет каждый байт потока с окном 4 КиБ.
        compressed.resize(compressBound(static_cast<uLong>(expected.size())));
        z_stream writer{};hmm_save_plan(&writer);assert(deflateInit2(&writer,Z_DEFAULT_COMPRESSION,Z_DEFLATED,12,5,Z_DEFAULT_STRATEGY)==Z_OK);
        save_peak=std::max(save_peak,save_used);assert(save_used<=decode_bytes);
        writer.next_in=expected.data();writer.avail_in=static_cast<uInt>(expected.size());
        writer.next_out=compressed.data();writer.avail_out=static_cast<uInt>(compressed.size());
        assert(deflate(&writer,Z_FINISH)==Z_STREAM_END);const uLong bytes=writer.total_out;assert(deflateEnd(&writer)==Z_OK);
        size=static_cast<uLongf>(actual.size());assert(uncompress(actual.data(),&size,compressed.data(),bytes)==Z_OK&&size==expected.size()&&actual==expected);
    }
    std::fclose(data);std::fclose(index);
    std::printf("decode/save pool: %u roundtrips exact, state=%u window=%u requests=%u save_peak=%u\n",records,state_bytes,decode_window,windows,save_peak);
}
