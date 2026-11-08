#pragma once
#include "hmm_platform.h"
#include <cstdio>
#include <cstdint>

inline const char *hmm_move_reason;
inline unsigned hmm_move_input_kind;
inline int hmm_move_input_code;
inline uint32_t hmm_move_input_when;
inline void hmm_move_input_note(unsigned kind,int code){
    hmm_move_input_kind=kind;hmm_move_input_code=code;hmm_move_input_when=uint32_t(hmm_millis());
}
struct HmmMoveCause {
    const char *previous;
    explicit HmmMoveCause(const char *reason):previous(hmm_move_reason){hmm_move_reason=reason;}
    ~HmmMoveCause(){hmm_move_reason=previous;}
};
inline void hmm_move_trace(const char *why,int tile,int target,int green,unsigned steps,unsigned points){
    char message[144];
    std::snprintf(message,sizeof(message),"HMM2 MOVE why=%s tile=%d dst=%d green=%d steps=%u pts=%u",why,tile,target,green,steps,points);hmm_log(message);
    if(why[0]=='i'&&why[1]=='n'&&why[2]=='p'){
        const uint32_t age=hmm_move_input_kind?uint32_t(hmm_millis())-hmm_move_input_when:0;
        std::snprintf(message,sizeof(message),"HMM2 MKEY type=%u code=%d age=%u",hmm_move_input_kind,hmm_move_input_code,unsigned(age));hmm_log(message);
    }
}
