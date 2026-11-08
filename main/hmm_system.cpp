/* Codex: пути и каталоги через файловый транспорт Evo; std::filesystem не нужен.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "system.h"
#include "dir.h"
#include "hmm_platform.h"
#include <algorithm>
#include <cctype>
#include <utility>
namespace {
std::string normalize(std::string_view p) {
    std::string s(p); std::replace(s.begin(),s.end(),'\\','/');
    while(s.size()>1 && s.back()=='/') s.pop_back();
    return s;
}
std::string lower(std::string_view p) {
    std::string s(p); for(char &c:s)c=char(std::tolower(static_cast<unsigned char>(c))); return s;
}
bool match(const char *p,const char *s) {
    while(*p) { if(*p=='*') { ++p; do { if(match(p,s))return true; } while(*s++); return false; }
        if(!*s || (*p!='?' && std::tolower((unsigned char)*p)!=std::tolower((unsigned char)*s)))return false; ++p;++s; }
    return !*s;
}
struct Listing { std::string path,filter; ListFiles *out; bool exact; };
void entry(const char *name,void *user) {
    auto &l=*static_cast<Listing*>(user); std::string n=lower(name),f=lower(l.filter);
    if((l.exact && n==f) || (!l.exact && n.size()>=f.size() && n.compare(n.size()-f.size(),f.size(),f)==0))
        l.out->push_back(System::concatPath(l.path,name));
}
}
namespace System {
bool isHandheldDevice(){return false;}
bool isTouchInputAvailable(){return false;}
bool isVirtualKeyboardSupported(){return false;}
bool isShellLevelGlobbingSupported(){return false;}
bool MakeDirectory(std::string_view p){return hmm_mkdir(std::string(p).c_str())!=0;}
bool Unlink(std::string_view p){return hmm_unlink(std::string(p).c_str())!=0;}
std::string concatPath(std::string_view l,std::string_view r){auto s=normalize(l);if(!s.empty()&&!r.empty())s+='/';return s+normalize(r);}
void appendOSSpecificDirectories(std::vector<std::string>&){}
std::string GetConfigDirectory(std::string_view){return ".";}
std::string GetDataDirectory(std::string_view){return ".";}
std::string GetParentDirectory(std::string_view p){auto s=normalize(p);auto n=s.find_last_of('/');return n==s.npos?"":s.substr(0,n);}
std::string GetFileName(std::string_view p){auto s=normalize(p);auto n=s.find_last_of('/');return n==s.npos?s:s.substr(n+1);}
std::string GetStem(std::string_view p){auto s=GetFileName(p);auto n=s.find_last_of('.');return n==s.npos?s:s.substr(0,n);}
bool IsFile(std::string_view p){return hmm_is_file(std::string(p).c_str())!=0;}
bool IsDirectory(std::string_view p){return hmm_is_dir(std::string(p).c_str())!=0;}
bool GetCaseInsensitivePath(std::string_view p,std::string &out){out=normalize(p);return IsFile(out)||IsDirectory(out);}
std::string encLocalToUTF8(std::string_view s){return std::string(s);}
std::string encUTF8ToLocal(std::string_view s){return std::string(s);}
void globFiles(std::string_view p,std::vector<std::string>&out){
    ListFiles all; auto parent=GetParentDirectory(p); all.ReadDir(parent,"");auto pattern=GetFileName(p); auto before=out.size();
    for(auto &s:all)if(match(pattern.c_str(),GetFileName(s).c_str()))out.push_back(s);
    if(before==out.size())out.emplace_back(p);
}
tm GetTM(time_t t){tm result{};
    /* Codex: сохранения используют календарь без зависимости от часового пояса ОС. */
    int64_t days=t/86400,secs=t%86400; result.tm_hour=int(secs/3600);result.tm_min=int(secs/60%60);result.tm_sec=int(secs%60);
    result.tm_wday=int((days+4)%7);int y=1970;
    auto leap=[](int a){return a%4==0&&(a%100!=0||a%400==0);};
    while(days>=365+leap(y)){days-=365+leap(y);++y;}result.tm_year=y-1900;result.tm_yday=int(days);
    const int m[]={31,28,31,30,31,30,31,31,30,31,30,31};int month=0;
    while(month<11 && days>=m[month]+(month==1&&leap(y))){days-=m[month]+(month==1&&leap(y));++month;}
    result.tm_mon=month;result.tm_mday=int(days)+1;return result;
}
}
void ListFiles::Append(ListFiles &&files){splice(end(),files);}
void ListFiles::ReadDir(const std::string &path,const std::string &filter){Listing l{path,filter,this,false};hmm_list_dir(path.c_str(),entry,&l);}
void ListFiles::FindFileInDir(std::string_view path,std::string_view name){Listing l{std::string(path),std::string(name),this,true};hmm_list_dir(l.path.c_str(),entry,&l);}
bool ListFiles::IsEmpty(const std::string &path,const std::string &filter){ListFiles l;l.ReadDir(path,filter);return l.empty();}
