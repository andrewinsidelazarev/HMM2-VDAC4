// Codex: минимальная проверка конструктора, STL и исключения до сборки всего движка.
#include <string>
#include <vector>
static std::string name("HMM2 runtime");
static int constructed=0,destroyed=0;
struct Lifetime {Lifetime(){++constructed;}~Lifetime(){++destroyed;}};
static Lifetime lifetime;
struct Base {virtual ~Base()=default;};
struct Derived:Base {int value=7;};
extern "C" int runtime_destructed(){return destroyed;}
extern "C" __attribute__((visibility("default"))) int app_entry(int, char **) {
    if(constructed!=1)return -1;
    Derived object;Base *base=&object;
    // Codex: барьер не даёт оптимизатору заменить проверку RTTI знанием локального типа.
    __asm__ volatile("":"+r"(base));
    auto*derived=dynamic_cast<Derived*>(base);if(!derived)return -2;
    std::vector<int> values(4,7);
    try { throw values.at(0)+derived->value-7; }
    catch(int v) { return v+name.size(); }
}
