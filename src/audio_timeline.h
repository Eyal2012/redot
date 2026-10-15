#pragma once
#include "recording.h"
#include "timeline.h"
#include "logging.h"
#include "audio_capture_trace.h"
#include <algorithm>
#include <cmath>

class AudioTimeline {
public:
    static constexpr uint64_t BlockFrames = 1024;
    static constexpr uint64_t RingFrames = 48000 * 4;
    AudioTimeline() : game_(RingFrames * 2), microphone_(RingFrames * 2), gameTags_(RingFrames, -1), microphoneTags_(RingFrames, -1) {}
    void ingest(const RecordedAudio& packet, std::chrono::steady_clock::time_point origin, RecordingIngress& ingress, const RecordingAudioOptions& options) {
        const int offset = packet.source == AudioSourceKind::Game ? options.gameSyncMilliseconds : options.microphoneSyncMilliseconds;
        const int64_t first = (packet.continuousClock ?
            std::llround(std::chrono::duration<long double>(packet.clockAnchor-origin).count()*48000)+static_cast<int64_t>(packet.firstFrame48k) :
            std::chrono::duration_cast<std::chrono::nanoseconds>(packet.capturedAt-origin).count()*48000/1'000'000'000LL) + offset*48LL;
        const size_t frames = packet.stereo48k.size() / 2;
        if (diagnosticSetting(L"PANDACLIP_SYNC_TRACE")) {
            const size_t source = packet.source == AudioSourceKind::Game ? 0 : 1;
            for (size_t n = 0; n < frames; ++n) if (std::abs(packet.stereo48k[n * 2] * 32767) > 3000) {
                const int64_t at = first + static_cast<int64_t>(n);
                if (at > traceLastLoud_[source] + 7200) logLine(L"Sync trace mapped source %zu cue: pts=%.3f ms, adjustment=%d ms.", source, at / 48.0, offset);
                traceLastLoud_[source] = at;
            }
        }
        auto& lane = packet.source == AudioSourceKind::Game ? game_ : microphone_;
        auto& tags = packet.source == AudioSourceKind::Game ? gameTags_ : microphoneTags_;
        for (size_t i = 0; i < frames; ++i) {
            const int64_t absolute = first + static_cast<int64_t>(i);
            if (absolute < static_cast<int64_t>(nextOutput_)) { ingress.lateAudioFrames.fetch_add(1, std::memory_order_relaxed); continue; }
            if (absolute >= static_cast<int64_t>(nextOutput_ + RingFrames)) { ingress.audioQueueDrops.fetch_add(1, std::memory_order_relaxed); continue; }
            const size_t slot = static_cast<size_t>(absolute % RingFrames);
            if(tags[slot]==absolute)ingress.audioTimelineOverwrites.fetch_add(1,std::memory_order_relaxed);
            lane[slot * 2] = packet.stereo48k[i * 2]; lane[slot * 2 + 1] = packet.stereo48k[i * 2 + 1]; tags[slot] = absolute;
        }
    }
    void build(uint64_t first, uint64_t frames, const RecordingAudioOptions& options,
        std::vector<int16_t>& mixed, std::vector<int16_t>& gameOut, std::vector<int16_t>& microphoneOut, RecordingIngress* ingress=nullptr) {
        mixed.resize(frames * 2); gameOut.resize(frames * 2); microphoneOut.resize(frames * 2);
        std::vector<float> processed;
        if(ingress && ingress->audioTrace && ingress->audioTrace->active())processed.resize(frames*2);
        uint64_t missing=0,clipped=0;
        for (uint64_t i = 0; i < frames; ++i) {
            const uint64_t absolute = first + i; const size_t slot = absolute % RingFrames;
            const bool hasGame = gameTags_[slot] == static_cast<int64_t>(absolute);
            const bool hasMic = microphoneTags_[slot] == static_cast<int64_t>(absolute);
            if(!processed.empty()){processed[i*2]=hasGame?game_[slot*2]:0;processed[i*2+1]=hasGame?game_[slot*2+1]:0;}
            if(options.gameMode!=GameAudioMode::Off && !hasGame)++missing;
            if(options.microphoneEnabled && !hasMic)++missing;
            const float g = hasGame && !options.gameMuted ? game_[slot * 2] * options.gameVolume : 0.0f;
            const float gr = hasGame && !options.gameMuted ? game_[slot * 2 + 1] * options.gameVolume : 0.0f;
            const float m = hasMic && !options.microphoneMuted ? microphone_[slot * 2] * options.microphoneVolume : 0.0f;
            const float mr = hasMic && !options.microphoneMuted ? microphone_[slot * 2 + 1] * options.microphoneVolume : 0.0f;
            gameOut[i * 2] = pcm(g); gameOut[i * 2 + 1] = pcm(gr);
            microphoneOut[i * 2] = pcm(m); microphoneOut[i * 2 + 1] = pcm(mr);
            if(std::abs(g+m)>1 || std::abs(gr+mr)>1)++clipped;
            mixed[i * 2] = pcm(std::clamp(g + m, -1.0f, 1.0f));
            mixed[i * 2 + 1] = pcm(std::clamp(gr + mr, -1.0f, 1.0f));
            gameTags_[slot] = microphoneTags_[slot] = -1;
        }
        if(ingress){
            ingress->audioTimelineMissing.fetch_add(missing,std::memory_order_relaxed);
            ingress->audioMixClipped.fetch_add(clipped,std::memory_order_relaxed);
            if(ingress->audioTrace){if(!processed.empty())ingress->audioTrace->processed(first,processed);ingress->audioTrace->mix(first,mixed);}
        }
        nextOutput_ = first + frames;
    }
    // Scene lanes reuse the exact repaired integer timeline, without quantizing
    // intermediate PCM. Gain/mix and int16 conversion happen once afterward.
    void extract(uint64_t first,uint64_t frames,AudioSourceKind source,std::vector<float>& output,RecordingIngress& ingress) {
        auto& lane=source==AudioSourceKind::Game?game_:microphone_;
        auto& tags=source==AudioSourceKind::Game?gameTags_:microphoneTags_;
        output.resize(frames*2);uint64_t missing=0;
        for(uint64_t i=0;i<frames;++i){auto at=first+i;auto slot=at%RingFrames;bool present=tags[slot]==int64_t(at);
            output[i*2]=present?lane[slot*2]:0;output[i*2+1]=present?lane[slot*2+1]:0;tags[slot]=-1;if(!present)++missing;}
        nextOutput_=first+frames;ingress.audioTimelineMissing+=missing;
    }
private:
    static int16_t pcm(float value) { return static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f); }
    std::vector<float> game_, microphone_;
    std::vector<int64_t> gameTags_, microphoneTags_;
    uint64_t nextOutput_{};
    int64_t traceLastLoud_[2]{};
};
