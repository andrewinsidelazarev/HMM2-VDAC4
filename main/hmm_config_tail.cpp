/* Codex: тот же синтаксис конфигурации, но без машины регулярных выражений. */
namespace {
struct ConfigNumbers {
    std::string_view s; size_t pos=0;
    void spaces(){while(pos<s.size()&&s[pos]==' ')++pos;}
    bool ch(char c){spaces();if(pos==s.size()||s[pos]!=c)return false;++pos;return true;}
    bool num(int32_t &n,bool negative=false){
        spaces();auto start=pos;if(negative&&pos<s.size()&&s[pos]=='-')++pos;
        auto digit=pos;while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')++pos;
        return pos>digit&&convertToInt(s.substr(start,pos-start),n);
    }
    bool end(){spaces();return pos==s.size();}
};
}
fheroes2::Point TinyConfig::PointParams(const std::string &key,const fheroes2::Point &fallback)const {
    auto it=find(ModifyKey(key));if(it==end())return fallback;ConfigNumbers p{it->second};fheroes2::Point r;
    /* Codex: пробелы до '[' и после ']' исходный формат не допускает. */
    if(p.s.empty()||p.s.front()!='['||p.s.back()!=']')return fallback;
    return p.ch('[')&&p.num(r.x,true)&&p.ch(',')&&p.num(r.y,true)&&p.ch(']')&&p.end()?r:fallback;
}
fheroes2::ResolutionInfo TinyConfig::ResolutionParams(const std::string &key,const fheroes2::ResolutionInfo &fallback)const {
    auto it=find(ModifyKey(key));if(it==end())return fallback;ConfigNumbers p{it->second};int32_t w,h,a,b;
    if(!p.num(w)||!p.ch('x')||!p.num(h))return fallback;
    if(p.end())return {w,h};
    if(p.ch(':')){if(p.num(a)&&p.ch('x')&&p.num(b)&&p.end())return {w,h,a,b};return fallback;}
    if(p.ch('x')&&p.num(a)&&p.end()&&a>0&&w<=INT32_MAX/a&&h<=INT32_MAX/a)return {w,h,w*a,h*a};
    return fallback;
}
bool TinyConfig::Exists(const std::string &key)const{return find(ModifyKey(key))!=end();}
