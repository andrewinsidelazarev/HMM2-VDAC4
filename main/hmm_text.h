/* Codex: форматирование строк без локалей, iostream и файловой части libstdc++.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <string>
#include <string_view>
#include <type_traits>
#include <cstdio>
#include <cstdint>
namespace hmmfmt {
struct Base { unsigned n; };
struct Width { unsigned n; };
struct Fill { char c; };
constexpr Base hex{16}, dec{10};
constexpr char endl='\n';
inline Width setw(unsigned n) { return {n}; }
inline Fill setfill(char c) { return {c}; }
}
class HmmText {
    std::string text;
    unsigned base=10, width=0;
    char fill=' ';
    void append(std::string_view s) {
        if (width>s.size()) text.append(width-s.size(),fill);
        text.append(s.data(),s.size()); width=0;
    }
public:
    HmmText &operator<<(std::string_view s) { append(s); return *this; }
    HmmText &operator<<(const std::string &s) { append(s); return *this; }
    HmmText &operator<<(const char *s) { append(s?s:"(null)"); return *this; }
    HmmText &operator<<(char c) { append({&c,1}); return *this; }
    HmmText &operator<<(unsigned char c) { return *this<<static_cast<char>(c); }
    HmmText &operator<<(signed char c) { return *this<<static_cast<char>(c); }
    HmmText &operator<<(hmmfmt::Base b) { base=b.n; return *this; }
    HmmText &operator<<(hmmfmt::Width w) { width=w.n; return *this; }
    HmmText &operator<<(hmmfmt::Fill f) { fill=f.c; return *this; }
    template<class T, std::enable_if_t<std::is_integral_v<T>,int> =0>
    HmmText &operator<<(T n) {
        char buf[66]; char *end=buf+sizeof(buf), *p=end;
        bool neg=std::is_signed_v<T> && n<0 && base==10;
        uint64_t value=neg?uint64_t(0)-uint64_t(n):uint64_t(n);
        if (base==16 && sizeof(T)<8) value &= (uint64_t(1)<<(sizeof(T)*8))-1;
        do { *--p="0123456789abcdef"[value%base]; value/=base; } while(value);
        if(neg) *--p='-'; append({p,size_t(end-p)}); return *this;
    }
    HmmText &operator<<(double n) { char s[64]; std::snprintf(s,sizeof(s),"%.6g",n); append(s); return *this; }
    HmmText &operator<<(const void *p) { auto b=base; base=16; *this<<"0x"<<uintptr_t(p); base=b; return *this; }
    std::string str() const { return text; }
};
