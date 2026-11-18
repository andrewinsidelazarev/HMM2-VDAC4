// Only linked into the original CPU renderer used to prepare language resources.
#include <cstdint>
void hmm_gpu_fade(uint8_t) {}
bool hmm_gpu_map_animation(bool) { return false; }
void hmm_host_capture_cached_cursor() {}
extern "C" uint32_t hmm_gpu_clip_buffer(unsigned,unsigned,unsigned,unsigned) { return 0; }
extern "C" void hmm_gpu_clip_end() {}
extern "C" void hmm_cache_trim() {}
