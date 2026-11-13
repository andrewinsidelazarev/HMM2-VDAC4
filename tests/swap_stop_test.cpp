#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#define HMM_SIZE_RENDER
constexpr int ZM_OK=0;
bool hmm_running;
static uint32_t now,reads,stop_at,ready_at,fail_at;
static int read_ft(uint32_t at,uint32_t *value){assert(at==0x302054);++reads;now+=10;if(reads==stop_at)hmm_running=false;*value=reads>=ready_at?0:2;return reads==fail_at?1:0;}
static uint32_t clock_ms(){return now;}
static void delay_ms(uint32_t ms){now+=ms;}
static struct {int(*ft_read)(uint32_t,uint32_t*);uint32_t(*millis)();void(*delay)(uint32_t);} core{read_ft,clock_ms,delay_ms},*hmm_core=&core;
void hmm_fail(const char *s){throw std::runtime_error(s);}
#include "../main/hmm_swap_fence.inc"
static void reset(uint32_t clock=0){now=clock;reads=0;stop_at=ready_at=fail_at=~0u;hmm_running=swap_pending=true;}
static void failure(const char *expected){std::string message;try{wait_swap_locked();}catch(const std::runtime_error&e){message=e.what();}assert(message==expected&&reads<200);}
int main(){
    reset();stop_at=10;failure("Application stopped");assert(reads==10);
    reset();failure("FT812 DLSWAP timeout");
    reset(0xffffff00u);failure("FT812 DLSWAP timeout");
    reset();fail_at=4;failure("FT812 DLSWAP read failed");
    reset();ready_at=3;wait_swap_locked();assert(!swap_pending&&reads==3);wait_swap_locked();assert(reads==3);
    reset();hmm_running=false;failure("Application stopped");assert(!reads);
    puts("SWAP WAIT PASS: stop, timeout, clock wrap, read error, successful swap, confirmed reuse");
}
