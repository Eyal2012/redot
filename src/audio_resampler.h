#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Streaming version of the existing linear SRC. Carry fractional position and
// the last input frame across packets; only interpolation's one-frame lookahead
// is retained. No smoothing, suppression or independent packet rounding.
class AudioResampler {
    double phase_{};
    std::array<float,2> previous_{};
    bool previousValid_{};
    uint64_t outputFrames_{};
    double lastStep_{1};
public:
    uint64_t outputFrames() const { return outputFrames_; }
    std::vector<float> process(std::span<const float> stereo, double inputRate) {
        std::vector<float> output;
        const auto frames=stereo.size()/2;
        if(!frames || stereo.size()%2 || !std::isfinite(inputRate) || inputRate<8000 || inputRate>384000)return output;
        const double step=inputRate/48000.0;
        lastStep_=step;
        output.reserve((static_cast<size_t>(frames/step)+2)*2);
        while(phase_ < static_cast<double>(frames)) {
            auto a=static_cast<int64_t>(std::floor(phase_));
            double frac=phase_-a;
            if(frac<1e-6)frac=0;
            else if(1-frac<1e-6){++a;frac=0;}
            if(a>=static_cast<int64_t>(frames) || (frac>0 && a+1>=static_cast<int64_t>(frames)))break;
            if(a<0 && (!previousValid_ || a < -1))break;
            for(unsigned channel=0;channel<2;++channel) {
                const float left=a<0?previous_[channel]:stereo[static_cast<size_t>(a)*2+channel];
                const float right=frac==0?left:stereo[static_cast<size_t>(a+1)*2+channel];
                output.push_back(left+(right-left)*static_cast<float>(frac));
            }
            phase_+=step;
        }
        phase_-=frames;
        if(std::abs(phase_)<1e-6)phase_=0;
        previous_={stereo[stereo.size()-2],stereo.back()};previousValid_=true;
        outputFrames_+=output.size()/2;
        return output;
    }
    std::vector<float> drain() {
        std::vector<float> output;
        // Only the final fractional lookahead is held at the last true sample.
        while(previousValid_ && phase_<-1e-6){output.insert(output.end(),previous_.begin(),previous_.end());phase_+=lastStep_;}
        outputFrames_+=output.size()/2;
        return output;
    }
};
