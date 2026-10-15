// MP4 vs MKV recording cost through the production RecordingPipeline (real NVENC and AAC; deterministic GPU frames and
// PCM; no WGC, WASAPI or audible output). Alternates formats so slow drift of the machine affects both equally.
//   ReDotMkvPerformance <fresh folder under build> [seconds per round = 20] [rounds per format = 3] [fps = 60]
#include "quiet_gpu.h"

#include <winrt/base.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <thread>

namespace fs = std::filesystem;

namespace {
class ToneAudio final : public SceneAudioProvider {
public:
    std::vector<std::wstring> stems() const override { return {L"Scene source A", L"Scene source B"}; }
    bool start(std::shared_ptr<RecordingIngress>, std::wstring&) override { return true; }
    void origin(std::chrono::steady_clock::time_point) override {}
    void render(uint64_t first, uint64_t frames, std::vector<int16_t>& mix, std::vector<std::vector<int16_t>>& stems) override {
        mix.assign(frames * 2, 0); stems.assign(2, std::vector<int16_t>(frames * 2, 0));
        for (uint64_t i = 0; i < frames; ++i) {
            const double a = std::sin((first + i) * 2 * 3.14159265358979 * 440.0 / 48000.0) * 0.25, b = std::sin((first + i) * 2 * 3.14159265358979 * 660.0 / 48000.0) * 0.25;
            stems[0][i * 2] = stems[0][i * 2 + 1] = int16_t(a * 32767); stems[1][i * 2] = stems[1][i * 2 + 1] = int16_t(b * 32767);
            mix[i * 2] = mix[i * 2 + 1] = int16_t((a + b) * 32767);
        }
    }
    int advanceMilliseconds() const override { return 0; }
    void stop() override {}
};
struct Notifications { bool finished{}, success{}; std::wstring text; };
LRESULT CALLBACK notify(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == kRecordingEventMessage) {
        std::unique_ptr<RecordingEvent> event(reinterpret_cast<RecordingEvent*>(l));
        auto* state = reinterpret_cast<Notifications*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state && event->kind == RecordingEventKind::Finished) { state->finished = true; state->success = event->success; state->text = event->text; }
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
double seconds(FILETIME f) { ULARGE_INTEGER v; v.LowPart = f.dwLowDateTime; v.HighPart = f.dwHighDateTime; return double(v.QuadPart) / 1e7; }
double processCpu() { FILETIME c, e, k, u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u); return seconds(k) + seconds(u); }

struct Sample { double cpuSeconds{}, wallSeconds{}, encodeAvgMs{}, encodeMaxMs{}, privateMiBPeak{}, megabytes{}, finalizeMs{}; uint64_t frames{}, slow{}, drops{}, repeated{}, audioDrops{}; bool ok{}; };
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : (v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2); }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const fs::path root = fs::absolute(argv[1]);
    const double perRound = argc > 2 ? _wtof(argv[2]) : 20.0;
    const int rounds = argc > 3 ? _wtoi(argv[3]) : 3;
    const unsigned fps = argc > 4 ? unsigned(_wtoi(argv[4])) : 60;
    const fs::path allowed = fs::current_path() / L"build";
    if (root.wstring().rfind(allowed.wstring() + L"\\", 0) != 0 || fs::exists(root)) return 2;
    fs::create_directories(root / L"local");
    SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"local").c_str());
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto gpu = makeQuietGpu();
    if (!gpu) { std::printf("no NVIDIA GPU\n"); return 3; }
    Notifications state;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = notify; cls.lpszClassName = L"ReDotMkvPerformance";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    constexpr unsigned kW = 1920, kH = 1080;
    std::vector<ComPtr<ID3D11Texture2D>> textures(10);
    for (size_t t = 0; t < textures.size(); ++t) {
        std::vector<uint32_t> pixels(kW * kH);
        uint32_t seed = uint32_t(t * 2654435761u + 1);
        for (unsigned y = 0; y < kH; ++y) for (unsigned x = 0; x < kW; ++x) {
            seed = seed * 1664525u + 1013904223u; // light noise keeps the bitrate realistic rather than trivially tiny
            const unsigned n = (seed >> 24) & 31;
            pixels[y * kW + x] = 0xff000000u | ((((x * 255 / kW) + t * 25 + n) & 255) << 16) | ((((y * 255 / kH) + t * 11 + n) & 255) << 8) | ((t * 20 + n) & 255);
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = kW; desc.Height = kH; desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), kW * 4, 0};
        winrt::check_hresult(gpu->device->CreateTexture2D(&desc, &data, &textures[t]));
    }
    std::vector<Sample> mp4, mkv;
    std::ofstream report(root / L"mkv-performance.txt");
    auto log = [&](const std::string& line) { std::printf("%s\n", line.c_str()); report << line << "\n"; report.flush(); };
    log("MP4 vs MKV through the production pipeline: " + std::to_string(kW) + "x" + std::to_string(kH) + " @ " + std::to_string(fps) + " fps, Mix + 2 scene stems, " + std::to_string(int(perRound)) + " s per round, " + std::to_string(rounds) + " rounds per format, alternating.");
    for (int round = 0; round < rounds * 2; ++round) {
        const bool matroska = (round % 2) == 1; // MP4 first, then alternate
        const fs::path folder = root / (std::wstring(matroska ? L"mkv-" : L"mp4-") + std::to_wstring(round / 2));
        fs::create_directories(folder);
        VideoConfig video; video.width = kW; video.height = kH; video.fps = fps; video.container = matroska ? ContainerFormat::Mkv : ContainerFormat::Mp4;
        RecordingAudioOptions audio; audio.gameMode = GameAudioMode::Off; audio.scene = std::make_shared<ToneAudio>();
        state = {};
        RecordingPipeline pipeline; std::wstring error;
        Sample sample;
        if (!pipeline.start(window, gpu, folder.wstring(), RecordingQuality::Balanced, audio, error, false, {}, video)) { log("start failed"); return 4; }
        auto ingress = pipeline.ingress();
        const double cpuBegin = processCpu();
        const auto begin = std::chrono::steady_clock::now();
        auto nextFrame = begin; uint64_t pushed = 0; PROCESS_MEMORY_COUNTERS_EX memory{};
        bool stopped = false; std::chrono::steady_clock::time_point stopAt{};
        while (!state.finished) {
            MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - begin).count();
            if (!stopped && elapsed >= perRound) { stopped = true; stopAt = now; sample.wallSeconds = elapsed; sample.cpuSeconds = processCpu() - cpuBegin; pipeline.requestStop(); }
            if (!stopped && now >= nextFrame) { ingress->push({textures[pushed++ % textures.size()], now, {}}); nextFrame += std::chrono::nanoseconds(1'000'000'000ull / fps); }
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            sample.privateMiBPeak = std::max(sample.privateMiBPeak, double(memory.PrivateUsage) / 1048576.0);
            std::this_thread::sleep_for(std::chrono::microseconds(1500));
            if (elapsed > perRound + 60) { log("timeout"); return 5; }
        }
        sample.finalizeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stopAt).count();
        pipeline.join();
        sample.ok = state.success;
        sample.frames = ingress->encodedFrames.load();
        sample.encodeAvgMs = sample.frames ? ingress->totalEncodeMicros.load() / double(sample.frames) / 1000.0 : 0;
        sample.encodeMaxMs = ingress->maxEncodeMicros.load() / 1000.0;
        sample.slow = ingress->slowEncodeFrames.load(); sample.drops = ingress->queueDrops.load(); sample.repeated = ingress->repeatedFrames.load(); sample.audioDrops = ingress->audioQueueDrops.load();
        for (auto& entry : fs::directory_iterator(folder)) if (entry.path().extension() == (matroska ? L".mkv" : L".mp4")) sample.megabytes = double(fs::file_size(entry.path())) / 1e6;
        char line[512];
        std::snprintf(line, sizeof(line), "%s round %d: ok=%d frames=%llu cpu=%.2fs/%.1fs (%.1f%% of one core) encode avg/max=%.3f/%.2f ms slow=%llu drops=%llu repeated=%llu audioDrops=%llu private-peak=%.0f MiB size=%.1f MB finalize=%.0f ms",
            matroska ? "MKV" : "MP4", round / 2 + 1, sample.ok, (unsigned long long)sample.frames, sample.cpuSeconds, sample.wallSeconds, sample.wallSeconds ? 100.0 * sample.cpuSeconds / sample.wallSeconds : 0.0,
            sample.encodeAvgMs, sample.encodeMaxMs, (unsigned long long)sample.slow, (unsigned long long)sample.drops, (unsigned long long)sample.repeated, (unsigned long long)sample.audioDrops, sample.privateMiBPeak, sample.megabytes, sample.finalizeMs);
        log(line);
        (matroska ? mkv : mp4).push_back(sample);
        if (!sample.ok) return 6;
    }
    auto column = [](const std::vector<Sample>& v, auto field) { std::vector<double> out; for (auto& s : v) out.push_back(double(s.*field)); return median(out); };
    char line[640];
    auto summary = [&](const char* name, const std::vector<Sample>& v) {
        std::snprintf(line, sizeof(line), "MEDIAN %s: cpu%%=%.1f encodeAvg=%.3f ms encodeMax=%.2f ms private-peak=%.0f MiB size=%.1f MB finalize=%.0f ms slow=%.0f drops=%.0f", name,
            100.0 * column(v, &Sample::cpuSeconds) / std::max(0.001, column(v, &Sample::wallSeconds)), column(v, &Sample::encodeAvgMs), column(v, &Sample::encodeMaxMs), column(v, &Sample::privateMiBPeak),
            column(v, &Sample::megabytes), column(v, &Sample::finalizeMs), column(v, &Sample::slow), column(v, &Sample::drops));
        log(line);
    };
    summary("MP4", mp4); summary("MKV", mkv);
    return 0;
}
