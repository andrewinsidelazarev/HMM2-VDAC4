// Codex: временный массив ИИ имеет точный размер мира и страницы по 64 double.
// Значения и индексы исходного расчёта не меняются; непрерывные 93312 байт не нужны.
#include <cstddef>
#include <memory>
#include <vector>
class HmmThreatArray {
    size_t length;
    std::vector<std::unique_ptr<double[]>> pages;
public:
    explicit HmmThreatArray(size_t n):length(n),pages((n+63)/64){for(auto &p:pages)p.reset(new double[64]{});}
    size_t size()const{return length;}
    double &operator[](size_t i){return pages[i/64][i%64];}
    const double &operator[](size_t i)const{return pages[i/64][i%64];}
};
