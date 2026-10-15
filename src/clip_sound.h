#pragma once
#include "replay.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// Subtle two-note confirmation chime for a successfully finalized replay clip. The WAV is synthesized once into
// static memory (no external file, so standalone deployments work) and played with PlaySound's asynchronous
// memory mode, which returns immediately and never touches the capture, encoder or export threads.
namespace ClipSound {
constexpr uint32_t SampleRate = 44100;
constexpr double DurationSeconds = 0.24;
constexpr double PeakAmplitude = 0.18; // about -15 dBFS: audible, never startling

inline std::vector<uint8_t> buildWav() {
    const uint32_t frames = static_cast<uint32_t>(SampleRate * DurationSeconds);
    std::vector<uint8_t> wav(44 + frames * 2);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&wav[at], &v, 2); };
    std::memcpy(&wav[0], "RIFF", 4); put32(4, 36 + frames * 2); std::memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, SampleRate); put32(28, SampleRate * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&wav[36], "data", 4); put32(40, frames * 2);
    const double pi = 3.14159265358979323846;
    for (uint32_t n = 0; n < frames; ++n) {
        const double t = n / double(SampleRate);
        double v = 0;
        // G5 then C6, the second starting 70 ms in; each has a 6 ms attack and an exponential decay.
        for (auto [start, hz] : {std::pair{0.0, 783.99}, std::pair{0.07, 1046.50}}) {
            if (t < start) continue;
            const double local = t - start;
            v += std::sin(2 * pi * hz * local) * std::exp(-local * 14.0) * std::min(1.0, local / 0.006);
        }
        const double fadeOut = std::min(1.0, (DurationSeconds - t) / 0.02); // never end on a click
        const int16_t s = static_cast<int16_t>(std::lround(std::clamp(v * 0.5 * PeakAmplitude * fadeOut, -1.0, 1.0) * 32767.0));
        put16(44 + n * 2, static_cast<uint16_t>(s));
    }
    return wav;
}

// Only a fully exported clip (success with a finalized path) earns the sound, and only when the preference is on.
inline bool due(bool enabled, const ReplayEvent& event) { return enabled && event.success && !event.finalizedPath.empty(); }

inline void play() {
    static const std::vector<uint8_t> wav = buildWav(); // must outlive the asynchronous playback
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
} // namespace ClipSound
