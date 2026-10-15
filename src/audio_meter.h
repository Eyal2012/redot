#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
// Raw stereo PCM statistics, before recording gain/mute. One writer per source.
// The release envelope retains short real bursts between GUI polls; no oscillator
// or synthetic animation is involved. Readers never consume/reset the sample data.
struct AudioMeterSnapshot {
    float rms{}, peak{}, displayedRms{}, displayedPeak{};
    uint64_t frames{}, packets{}, timeMs{};
    double square{};
};
struct AudioMeter {
    std::atomic<float> rms{}, peak{}, rmsHold{}, peakHold{};
    std::atomic<double> square{};
    std::atomic<uint64_t> frames{}, packets{}, timeMs{}, sequence{};
    void publish(double sum, float maximum, uint64_t count, uint64_t now) noexcept {
        sequence.fetch_add(1,std::memory_order_acq_rel);
        const auto previous=timeMs.load(std::memory_order_relaxed);
        const float decay=previous?static_cast<float>(std::exp(-double(now-previous)/300.0)):0.f;
        const float value=count?static_cast<float>(std::sqrt(sum/(double(count)*2.0))):0.f;
        rms.store(value,std::memory_order_relaxed); peak.store(maximum,std::memory_order_relaxed);
        rmsHold.store(std::max(value,rmsHold.load(std::memory_order_relaxed)*decay),std::memory_order_relaxed);
        peakHold.store(std::max(maximum,peakHold.load(std::memory_order_relaxed)*decay),std::memory_order_relaxed);
        square.store(sum,std::memory_order_relaxed); frames.store(count,std::memory_order_relaxed);
        timeMs.store(now,std::memory_order_relaxed); packets.fetch_add(1,std::memory_order_relaxed);
        sequence.fetch_add(1,std::memory_order_release);
    }
    AudioMeterSnapshot snapshot(uint64_t now) const noexcept {
        AudioMeterSnapshot s;
        for(int n=0;n<3;++n){
            const auto before=sequence.load(std::memory_order_acquire);if(before&1)continue;
            s.rms=rms.load(std::memory_order_relaxed);s.peak=peak.load(std::memory_order_relaxed);
            s.frames=frames.load(std::memory_order_relaxed);s.square=square.load(std::memory_order_relaxed);
            s.packets=packets.load(std::memory_order_relaxed);s.timeMs=timeMs.load(std::memory_order_relaxed);
            const float decay=s.timeMs?static_cast<float>(std::exp(-double(now>=s.timeMs?now-s.timeMs:0)/300.0)):0.f;
            s.displayedRms=rmsHold.load(std::memory_order_relaxed)*decay;s.displayedPeak=peakHold.load(std::memory_order_relaxed)*decay;
            if(before==sequence.load(std::memory_order_acquire))return s;
        }
        return {}; // Bounded reader; try again next GUI tick during a collision.
    }
};
