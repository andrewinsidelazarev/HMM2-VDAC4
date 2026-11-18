#pragma once
#if defined(HMM2_HOST) && !defined(HMM2_PACKED)
extern "C" void hmm_export_map_text(const char *kind,const char *text);
#endif
