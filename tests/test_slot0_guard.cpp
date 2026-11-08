// Codex: положительное M_SEL/slot0, отсутствие ложных выходов и новый сеанс.
#include "../patches/slot0_autounload/after/include/zifi/ft_detach_guard.hpp"
#include <cassert>
#include <cstdio>
int main(){
    zifi::FtDetachGuard g;constexpr uint32_t id=0x56444334,ft=0x0201012b,ts=0x01010120;
    assert(!g.due(100));g.opened();assert(g.due(100));assert(!g.due(199));assert(g.due(200));
    assert(!g.observe(id,ts,0)); // Codex: приложение ещё не видело выбранный FT.
    assert(!g.observe(id,ft,1<<12));
    assert(!g.observe(0,ts,0)); // Codex: ошибка LINK не означает выход.
    assert(!g.observe(id,0x01020120,0)); // Codex: неверный протокол.
    assert(!g.observe(id,ft,0)); // Codex: дождаться подтверждённого slot0.
    assert(!g.observe(id,ts,1<<12)); // Codex: обычный core_back до Stop SPG.
    assert(g.observe(id,ts,0));g.reset();assert(!g.due(1000));
    g.opened();assert(g.due(1100));assert(!g.observe(id,ts,0));
    assert(!g.observe(id,ft,1<<12));assert(g.observe(id,ts,0));
    g.reset();g.opened();assert(g.due(0xfffffff0u));assert(!g.due(20));assert(g.due(100));
    g.reset();g.opened(true);assert(g.observe(id,ts,0));
    std::puts("slot0 guard: all cases passed");
}
