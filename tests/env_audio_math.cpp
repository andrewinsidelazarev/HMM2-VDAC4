#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <utility>
#include <cstdlib>
#include "hmm_env_audio.inc"
static void check(bool ok){if(!ok)std::abort();}
int main(){
    const double maximum=std::sqrt(18*32*32);unsigned count=0,ties=0;
    for(unsigned n=0;n<18432;++n){
        volatile unsigned source=n;
        const auto before=uint8_t(std::lround(std::sqrt(double(source))*255/maximum));
        const auto after=hmm_env_distance(n,maximum);
        if(before!=after){std::printf("distance n=%u old=%u new=%u\n",n,unsigned(before),unsigned(after));return 1;}
        ++count;
    }
    for(int width:{1,2,3,5,36,72,108,144})for(int height:{1,2,3,5,36,72,108,144})
      for(int cx:{0,1,width/2,width-1})for(int cy:{0,1,height/2,height-1})for(int radius:{3,4}){
        std::vector<std::pair<int,int>> before,after;
        for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
            if(x+cx>=0&&y+cy>=0&&x+cx<width&&y+cy<height)before.emplace_back(x,y);
        std::stable_sort(before.begin(),before.end(),[](auto a,auto b){return a.first*a.first+a.second*a.second < b.first*b.first+b.second*b.second;});
        for(auto &p:hmm_env_positions){const int x=p[0],y=p[1];
            if(x>=-radius&&x<=radius&&y>=-radius&&y<=radius&&x+cx>=0&&y+cy>=0&&x+cx<width&&y+cy<height)after.emplace_back(x,y);}
        check(before==after);++ties;
    }
    std::printf("ENV exact: %u distances, %u clipped stable-order scans; floating half-ties retained\n",count,ties);
}
