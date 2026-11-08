// Rendering runs synchronously on the game task. No core0 graphics worker,
// queues, wakeups or background RAM_G uploads are created.
namespace {
using RenderJob=void(*)(void*);
void render_worker_start(){}
void render_wait(){}
bool render_submit(RenderJob fn,void *arg){fn(arg);return false;}
void render_worker_stop(){}
struct RenderGuard {};
#ifdef HMM2_HOST
#define HMM_RENDER_NOINLINE __declspec(noinline)
#else
#define HMM_RENDER_NOINLINE __attribute__((noinline))
#endif
}