#pragma once
#include <cmath>
#include <cstdint>

namespace kharvox::intro {


class MusicBeat {
    float low{}, energy{}, average{};
    unsigned window{};
    uint64_t frame{}, last{};
public:
    template<class OnBeat> void consume(const float* stereo,int samples,OnBeat onBeat){
        for(int i=0;i<samples;i+=2){
            low+=.01945f*((stereo[i]+stereo[i+1])*.5f-low);
            energy+=low*low;++frame;
            if(++window==480){
                const float rms=std::sqrt(energy/480.f);
                if(rms>.008f&&rms>average*1.55f&&(last==0||frame-last>=8640)){
                    last=frame;onBeat(frame);
                }
                average+=.08f*(rms-average);energy=0;window=0;
            }
        }
    }
};
}
