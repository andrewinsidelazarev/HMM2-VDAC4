// Codex: исходная выдача полноэкранного снимка, границы арены и сохранность закреплённых данных.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <random>
constexpr size_t limit=128*1024,extra_limit=256*1024,page_size=256,base_pages=limit/page_size,menu_pages=~size_t(0);
constexpr size_t sprite_pages=(limit+extra_limit)/page_size;
uint8_t used[sprite_pages+240*1024/page_size];uint8_t *world_arena,*extra,*background;
constexpr unsigned background_limit=240*1024;
unsigned background_bytes,background_generation,lease_bytes,lease_generation;uint8_t *lease;
bool background_migrate;
std::unique_ptr<uint8_t[]> arena;
void *hmm_audio_idle_region(unsigned *bytes,unsigned *generation){*bytes=lease_bytes;*generation=lease_generation;return lease;}
size_t scratch_end;unsigned depth=1,epoch=17;
struct StoredSprite {uint8_t *pixels;size_t first,pages;unsigned pin;uint8_t *mask=nullptr;unsigned stamp=0;void release(){std::memset(used+first,0,pages);pixels=mask=nullptr;pages=0;}};
std::vector<StoredSprite> sprites;
template<class F> void each_sprite(F f){for(auto &s:sprites)f(&s);}
struct Region {size_t first,pages;bool busy=false;unsigned stamp=0;};Region regions[16];
void release_region(Region &r){std::memset(used+r.first,0,r.pages);r.pages=0;}
#include "memory_scratch_impl.inc"
int main(){
    std::vector<uint8_t> memory(limit+extra_limit,0x3c);world_arena=memory.data();extra=world_arena+limit;
    sprites={{world_arena,0,4,epoch},{world_arena+1000*page_size,1000,4,epoch}};
    for(auto &s:sprites)std::memset(used+s.first,1,s.pages);
    regions[0]={1200,7};std::memset(used+1200,1,7);
    assert(!hmm_scratch_take(307208));assert(sprites[1].pixels&&regions[0].pages&&!scratch_end);
    sprites[1].pin=epoch-1;regions[0].busy=true;
    assert(!hmm_scratch_take(307208)&&sprites[1].pixels&&!scratch_end);regions[0].busy=false;
    std::vector<uint8_t> audio(240*1024,0x19);background=audio.data();background_bytes=unsigned(audio.size());
    regions[1]={sprite_pages,7,true};std::memset(used+sprite_pages,1,7);
    auto *p=static_cast<uint8_t*>(hmm_scratch_take(307208));
    assert(p&&!(uintptr_t(p)&7)&&p>=world_arena&&p+307208<=world_arena+memory.size());
    assert(sprites[0].pixels&&!sprites[1].pixels&&!regions[0].pages);
    assert(regions[1].pages==7&&free_run(16,true)==sprite_pages+7);
    std::memset(p,0x7a,307208);assert(!hmm_scratch_take(307208));
    size_t at=free_run(16);assert(at!=~size_t(0)&&at+16<=scratch_end);std::memset(used+at,1,16);
    std::memset(world_arena+at*page_size,0x42,16*page_size);
    for(unsigned i=0;i<307208;++i)assert(p[i]==0x7a);
    for(unsigned i=0;i<4*page_size;++i)assert(world_arena[i]==0x3c);
    for(auto v:audio)assert(v==0x19);
    hmm_scratch_give();assert(!scratch_end&&free_run(400)!=~size_t(0));
    assert(!hmm_scratch_take(memory.size()));
    std::memset(used,1,sizeof(used));std::memset(used+500,0,30);
    assert(free_run(20)==500);world_arena=extra=nullptr;assert(free_run(20)==~size_t(0));
    // Codex: поздняя остановка петель переносит готовый фон, не читая его повторно.
    world_arena=memory.data();extra=world_arena+limit;std::memset(used,0,sizeof(used));
    for(auto &r:regions)r={};background=nullptr;background_bytes=0;
    regions[0]={17,9};regions[1]={40,4,true};std::memset(used+17,1,9);std::memset(used+40,1,4);
    std::memset(world_arena+17*page_size,0x56,9*page_size);
    std::memset(world_arena+40*page_size,0x79,4*page_size);
    lease=audio.data();lease_bytes=unsigned(audio.size());lease_generation=1;
    assert(refresh_background()&&regions[0].first>=sprite_pages&&regions[0].pages==9);
    assert(regions[1].first==40&&regions[1].pages==4);
    for(unsigned i=0;i<9*page_size;++i)assert(page_address(regions[0].first)[i]==0x56);
    for(unsigned i=17;i<26;++i)assert(!used[i]);
    // Codex: завершение занятой области должно повторить перенос без смены поколения.
    assert(background_migrate);regions[1].busy=false;assert(refresh_background()&&!background_migrate&&regions[1].first>=sprite_pages);
    for(unsigned i=0;i<4*page_size;++i)assert(page_address(regions[1].first)[i]==0x79);
    for(unsigned i=40;i<44;++i)assert(!used[i]);
    regions[2]={60,3,true};std::memset(used+60,1,3);
    lease=nullptr;++lease_generation;assert(!refresh_background()&&!regions[0].pages&&!regions[1].pages&&regions[2].pages==3);
    // Codex: сумма свободных страниц достаточна, но прежний поиск не видит цельного блока.
    std::memset(used,0,sizeof(used));for(auto &r:regions)r={};sprites.clear();
    for(unsigned i=0;i<4;++i){const size_t at=i*8,pages=i==2?4:i==3?3:2;auto *data=world_arena+at*page_size;
        sprites.push_back({data,at,pages,i==3?epoch:epoch-1,data+pages*page_size/2});
        std::memset(used+at,1,pages);std::memset(data,0x30+i,pages*page_size);
    }
    regions[0]={32,4,true};regions[1]={40,3};
    for(unsigned i=0;i<2;++i){auto &r=regions[i];std::memset(used+r.first,1,r.pages);std::memset(page_address(r.first),0x60+i,r.pages*page_size);}
    assert(free_run(1500)==~size_t(0));assert(cache_run(1500)==~size_t(0));
    assert(sprites[3].first==24&&regions[0].first==32);
    sprites[3].pin=epoch-1;regions[0].busy=false;
    assert(cache_run(1500)==18);
    for(unsigned i=0;i<sprites.size();++i){auto &s=sprites[i];
        assert(s.mask==s.pixels+s.pages*page_size/2);
        for(size_t n=0;n<s.pages*page_size;++n)assert(s.pixels[n]==0x30+i);
    }
    for(unsigned i=0;i<2;++i)for(size_t n=0;n<regions[i].pages*page_size;++n)assert(page_address(regions[i].first)[n]==0x60+i);
    for(unsigned i=0;i<sprite_pages;++i)assert(bool(used[i])==(i<18));
    // Codex: случайные размеры и закрепления проверяют перекрывающийся memmove
    // и сохранность каждого байта после нескольких последовательных переносов.
    std::minstd_rand random(82);
    for(unsigned trial=0;trial<1000;++trial){
        std::memset(used,0,sizeof(used));for(auto &r:regions)r={};sprites.clear();
        size_t at=0,occupied=0;std::vector<size_t> original;
        for(unsigned i=0;i<50;++i){at+=random()%8;const size_t pages=1+random()%20;
            auto *data=world_arena+at*page_size;const unsigned pin=random()%5?epoch-1:epoch;
            sprites.push_back({data,at,pages,pin,data+pages*page_size/2});original.push_back(at);
            std::memset(used+at,1,pages);std::memset(data,i+1,pages*page_size);occupied+=pages;at+=pages;
        }
        const size_t wanted=sprite_pages-occupied;cache_run(wanted);
        for(unsigned pass=0;pass<2;++pass){
            std::vector<unsigned char> expected(sprite_pages);
            for(unsigned i=0;i<sprites.size();++i){auto &s=sprites[i];
                if(s.pin==epoch)assert(s.first==original[i]);
                assert(s.pixels==page_address(s.first)&&s.mask==s.pixels+s.pages*page_size/2);
                for(size_t n=0;n<s.pages*page_size;++n)assert(s.pixels[n]==i+1);
                for(size_t n=s.first;n<s.first+s.pages;++n){assert(!expected[n]);expected[n]=1;}
            }
            for(unsigned i=0;i<sprite_pages;++i)assert(expected[i]==used[i]);
            for(auto &s:sprites)s.pin=epoch-1;
            assert(cache_run(wanted)==occupied);
        }
    }
    // Codex: полный основной банк не мешает заёму. Старый фон окна освобождается
    // раньше фаз боя; закреплённые спрайты и занятые фоны остаются на месте.
    std::memset(used,0,sizeof(used));for(auto &r:regions)r={};sprites.clear();
    background=lease=audio.data();background_bytes=lease_bytes=unsigned(audio.size());
    background_generation=lease_generation;
    sprites.push_back({world_arena,0,sprite_pages,epoch,nullptr,30});std::memset(used,1,sprite_pages);
    const size_t loan=sprite_run(60);assert(loan==sprite_pages);
    sprites.push_back({page_address(loan),loan,60,epoch-1,page_address(loan)+64,20});std::memset(used+loan,1,60);
    std::memset(page_address(loan),0x39,60*page_size);
    regions[0]={loan+60,background_bytes/page_size-60,false,10};std::memset(used+loan+60,1,regions[0].pages);
    assert(sprite_run(200)==loan+60&&!regions[0].pages&&sprites[1].pixels);
    regions[0]={loan+60,background_bytes/page_size-60,true,10};std::memset(used+loan+60,1,regions[0].pages);
    sprites[1].pin=epoch;assert(sprite_run(61)==~size_t(0));
    // Codex: снимок основной арены не затрагивает закреплённый заёмный спрайт.
    sprites[0].pin=epoch-1;assert(hmm_scratch_take(307208));assert(sprites[1].pixels);
    for(size_t i=0;i<60*page_size;++i)assert(sprites[1].pixels[i]==0x39);
    hmm_scratch_give();regions[0].busy=false;
    // Codex: отзыв того же адреса с новым поколением также сбрасывает указатели.
    ++lease_generation;assert(refresh_background()&&!sprites[1].pixels&&!regions[0].pages);
    for(size_t i=sprite_pages;i<sizeof(used);++i)assert(!used[i]);
    std::puts("Scratch: pinned protection, bounds, restoration, 1000 compacted layouts, contents and masks OK");
}
