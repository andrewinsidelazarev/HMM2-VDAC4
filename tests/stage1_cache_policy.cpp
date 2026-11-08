// Codex: настоящий ZmCache прошивки в совместном стенде; исходники прошивки только читаются.
#include "zifi/zm_cache.hpp"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
namespace {
class Storage final:public zifi::ZmStorage {
public:
 std::map<std::string,std::vector<uint8_t>> files;size_t capacity;
 explicit Storage(size_t n):capacity(n){}
 bool read(const char*n,uint8_t*p,size_t k)override{auto i=files.find(n);if(i==files.end()||i->second.size()!=k)return false;std::memcpy(p,i->second.data(),k);return true;}
 bool write(const char*n,const uint8_t*p,size_t k)override{files[n].assign(p,p+k);return true;}
 bool remove(const char*n)override{return files.erase(n)!=0;}
 size_t capacityBytes()override{return capacity;}
 long fileSize(const char*n)override{auto i=files.find(n);return i==files.end()?kMissing:long(i->second.size());}
 bool list(void(*f)(const char*,void*),void*p)override{for(auto&i:files)f(i.first.c_str(),p);return true;}
};
std::unique_ptr<Storage> storage;std::unique_ptr<zifi::ZmCache> cache;
}
extern "C" {
__declspec(dllexport) void cache_reset(unsigned n){cache.reset();storage.reset(new Storage(n));cache.reset(new zifi::ZmCache(*storage));}
__declspec(dllexport) int cache_has(const char*n,unsigned size,unsigned stamp){return cache->has(n,size,stamp);}
__declspec(dllexport) int cache_fits(unsigned size,const char*busy){return cache->canFit(size,busy);}
__declspec(dllexport) int cache_store(const char*n,unsigned stamp,const uint8_t*p,unsigned size,const char*busy){return cache->store(n,stamp,p,size,busy);}
__declspec(dllexport) int cache_touch(const char*n){auto*p=cache->find(n);if(!p)return 0;std::vector<uint8_t>b(p->size);return cache->read(n,b.data(),unsigned(b.size()));}
__declspec(dllexport) int cache_contains(const char*n){return cache->find(n)!=nullptr;}
}
