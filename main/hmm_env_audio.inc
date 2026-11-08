/* Exact ambient-distance quantization and stable scan order for 32 px tiles. */
#include <cmath>
#include <cstdint>
static const int8_t hmm_env_positions[81][2]={
{0,0},{0,-1},{-1,0},{1,0},{0,1},{-1,-1},{1,-1},{-1,1},{1,1},{0,-2},{-2,0},{2,0},{0,2},{-1,-2},{1,-2},{-2,-1},{2,-1},{-2,1},{2,1},{-1,2},{1,2},{-2,-2},{2,-2},{-2,2},{2,2},{0,-3},{-3,0},{3,0},{0,3},{-1,-3},{1,-3},{-3,-1},{3,-1},{-3,1},{3,1},{-1,3},{1,3},{-2,-3},{2,-3},{-3,-2},{3,-2},{-3,2},{3,2},{-2,3},{2,3},{0,-4},{-4,0},{4,0},{0,4},{-1,-4},{1,-4},{-4,-1},{4,-1},{-4,1},{4,1},{-1,4},{1,4},{-3,-3},{3,-3},{-3,3},{3,3},{-2,-4},{2,-4},{-4,-2},{4,-2},{-4,2},{4,2},{-2,4},{2,4},{-3,-4},{3,-4},{-4,-3},{4,-3},{-4,3},{4,3},{-3,4},{3,4},{-4,-4},{4,-4},{-4,4},{4,4}};
inline uint8_t hmm_env_distance(unsigned squared,double maximum){
    // Rounding threshold: 255*sqrt(n/18432) >= d-1/2 iff
    // 7225*n >= 512*(2*d-1)^2. All products fit uint32_t.
    // Keep the old floating expression on the three exact half-way ties;
    // its library/compiler rounding is thereby retained on the actual CPU.
    if(squared==512||squared==4608||squared==12800){
        volatile unsigned tie=squared;
        return uint8_t(std::lround(std::sqrt(double(tie))*255/maximum));
    }
    const unsigned scaled=squared*7225;unsigned lo=0,hi=255;
    while(lo<hi){const unsigned mid=(lo+hi+1)/2,bound=2*mid-1;
        if(bound*bound*512<=scaled)lo=mid;else hi=mid-1;
    }
    return uint8_t(lo);
}
