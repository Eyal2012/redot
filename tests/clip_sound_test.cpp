// Quiet checks for the replay-clip confirmation sound: embedded waveform shape and the "when to play" rule.
// Never opens an audio device or plays anything.
#include "clip_sound.h"
#include <iostream>
#include <algorithm>

static int failures = 0;
static void check(bool ok, const char* what) { std::cout << (ok ? "PASS " : "FAIL ") << what << "\n"; if (!ok) ++failures; }

int main() {
    const auto wav = ClipSound::buildWav();
    check(wav.size() > 44 && std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0 && std::memcmp(wav.data() + 36, "data", 4) == 0, "WAV header is well formed");
    uint32_t dataBytes{}, rate{}; std::memcpy(&dataBytes, wav.data() + 40, 4); std::memcpy(&rate, wav.data() + 24, 4);
    check(dataBytes + 44 == wav.size() && rate == 44100, "WAV sizes and sample rate are consistent");
    const double seconds = dataBytes / 2.0 / rate;
    check(seconds >= 0.15 && seconds <= 0.30, "duration is 150-300 ms");
    const auto* pcm = reinterpret_cast<const int16_t*>(wav.data() + 44);
    const size_t count = dataBytes / 2; int peak{};
    for (size_t i = 0; i < count; ++i) peak = std::max(peak, std::abs(int(pcm[i])));
    check(peak > 1500 && peak < 0.25 * 32767, "peak level is audible but subtle (-18..-12 dBFS)");
    check(std::abs(pcm[0]) < 100 && std::abs(pcm[count - 1]) < 100, "starts and ends without a click");
    ReplayEvent ok{true, L"Saved clip: a.mp4", L"C:\clips\a.mp4"}, failed{false, L"Replay export failed"}, noPath{true, L"x"};
    check(ClipSound::due(true, ok), "successful finalized clip plays when enabled");
    check(!ClipSound::due(false, ok), "disabled preference stays silent");
    check(!ClipSound::due(true, failed), "failed save stays silent");
    check(!ClipSound::due(true, noPath), "event without finalized path stays silent");
    std::cout << "RESULT failures=" << failures << "\n";
    return failures ? 1 : 0;
}
