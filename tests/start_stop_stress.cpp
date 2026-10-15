// Repeated start/stop of the production RecordingPipeline (alternating MKV/MP4, with and without an attached replay buffer
// and periodic replay exports) while tracking handles, threads and private memory. Synthetic GPU frames and PCM only.
//   ReDotStartStopStress <fresh folder under build> [cycles = 300] [seconds per cycle = 1.2]
// Output: <folder>/cycles.csv (one row per cycle) and a PASS/FAIL verdict. Files produced by this run live only in <folder>.
#include "quiet_gpu.h"
#include "mkv_writer.h"

#include <winrt/base.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
            const double a = std::sin((first + i) * 2 * 3.14159265358979 * 440.0 / 48000.0) * 0.2, b = std::sin((first + i) * 2 * 3.14159265358979 * 660.0 / 48000.0) * 0.2;
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
double slope(const std::vector<std::pair<double, double>>& p) {
    if (p.size() < 3) return 0;
    double mx = 0, my = 0; for (auto& v : p) { mx += v.first; my += v.second; } mx /= p.size(); my /= p.size();
    double num = 0, den = 0; for (auto& v : p) { num += (v.first - mx) * (v.second - my); den += (v.first - mx) * (v.first - mx); }
    return den ? num / den : 0;
}
bool playable(const fs::path& path) {
    if (path.extension() == L".mkv") { auto info = mkvInspect(path.wstring()); return info.valid && info.finalized; }
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) return false;
    DWORD flags = 0; ComPtr<IMFSample> sample;
    for (int i = 0; i < 20 && !sample; ++i) { if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) return false; }
    return bool(sample);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const fs::path root = fs::absolute(argv[1]);
    const int cycles = argc > 2 ? _wtoi(argv[2]) : 300;
    const double perCycle = argc > 3 ? _wtof(argv[3]) : 1.2;
    const fs::path allowed = fs::current_path() / L"build";
    if (root.wstring().rfind(allowed.wstring() + L"\\", 0) != 0 || fs::exists(root)) return 2;
    fs::create_directories(root / L"local");
    SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"local").c_str());
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    MFStartup(MF_VERSION);
    auto gpu = makeQuietGpu();
    if (!gpu) { std::printf("no NVIDIA GPU\n"); return 3; }
    Notifications state;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = notify; cls.lpszClassName = L"ReDotStartStopStress";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    constexpr unsigned kW = 1280, kH = 720;
    std::vector<ComPtr<ID3D11Texture2D>> textures(6);
    for (size_t t = 0; t < textures.size(); ++t) {
        std::vector<uint32_t> pixels(kW * kH);
        for (unsigned y = 0; y < kH; ++y) for (unsigned x = 0; x < kW; ++x) pixels[y * kW + x] = 0xff000000u | (((x * 255 / kW + t * 40) & 255) << 16) | (((y * 255 / kH + t * 17) & 255) << 8) | 0x40;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = kW; desc.Height = kH; desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), kW * 4, 0};
        winrt::check_hresult(gpu->device->CreateTexture2D(&desc, &data, &textures[t]));
    }
    std::ofstream csv(root / L"cycles.csv");
    csv << "cycle,container,replay,success,handles,threads,private_bytes,working_set,encoded_frames,start_ms,stop_ms,file_ok,export_ok\n";
    std::vector<std::pair<double, double>> handlePoints, threadPoints, privatePoints;
    unsigned failures = 0, startHandles = 0, startThreads = 0, endHandles = 0, endThreads = 0; uint64_t startPrivate = 0, endPrivate = 0;
    auto replay = std::make_shared<ReplayBuffer>();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const bool matroska = cycle % 2 == 0, withReplay = cycle % 4 < 2;
        const fs::path folder = root / (L"c" + std::to_wstring(cycle % 8)); // eight rotating folders, files removed after verification
        fs::create_directories(folder);
        VideoConfig video; video.width = kW; video.height = kH; video.fps = 60; video.container = matroska ? ContainerFormat::Mkv : ContainerFormat::Mp4;
        RecordingAudioOptions audio; audio.gameMode = GameAudioMode::Off; audio.scene = std::make_shared<ToneAudio>();
        state = {};
        RecordingPipeline pipeline; std::wstring error;
        replay->clearHistory(); replay->setEnabled(withReplay);
        const auto t0 = std::chrono::steady_clock::now();
        const bool started = pipeline.start(window, gpu, folder.wstring(), RecordingQuality::Balanced, audio, error, false, withReplay ? replay : std::shared_ptr<ReplayBuffer>{}, video);
        const double startMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!started) { std::printf("cycle %d start failed\n", cycle); ++failures; continue; }
        auto ingress = pipeline.ingress();
        const auto begin = std::chrono::steady_clock::now(); uint64_t pushed = 0; auto next = begin;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() < perCycle) {
            MSG m{}; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            if (std::chrono::steady_clock::now() >= next) { ingress->push({textures[pushed++ % textures.size()], std::chrono::steady_clock::now(), {}}); next += std::chrono::microseconds(16667); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bool exportOk = true;
        if (withReplay && cycle % 10 < 2) { replay->saveAsync(window, folder.wstring(), 5); }
        const auto stopBegin = std::chrono::steady_clock::now();
        pipeline.requestStop();
        const auto limit = stopBegin + std::chrono::seconds(30);
        while (!state.finished && std::chrono::steady_clock::now() < limit) { MSG m{}; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        const double stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stopBegin).count();
        pipeline.join();
        const auto exportLimit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (replay->pendingSaves() && std::chrono::steady_clock::now() < exportLimit) { MSG m{}; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        if (replay->pendingSaves()) exportOk = false;
        bool fileOk = true;
        for (auto& entry : fs::directory_iterator(folder)) {
            const auto ext = entry.path().extension();
            if (entry.path().filename().wstring().find(L".partial") != std::wstring::npos) { fileOk = false; continue; }
            if (ext == L".mkv" || ext == L".mp4") { if (!playable(entry.path())) fileOk = false; }
        }
        // Only files this cycle created inside this run's own folder are removed.
        for (auto& entry : fs::directory_iterator(folder)) { std::error_code ec; fs::remove(entry.path(), ec); }
        HANDLE self = GetCurrentProcess(); DWORD handles = 0; GetProcessHandleCount(self, &handles);
        PROCESS_MEMORY_COUNTERS_EX memory{}; GetProcessMemoryInfo(self, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        DWORD threads = 0; { DWORD pid = GetCurrentProcessId(); HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); if (snap != INVALID_HANDLE_VALUE) { THREADENTRY32 e{sizeof(e)}; if (Thread32First(snap, &e)) do { if (e.th32OwnerProcessID == pid) ++threads; } while (Thread32Next(snap, &e)); CloseHandle(snap); } }
        const bool ok = state.finished && state.success && fileOk && exportOk;
        if (!ok) { ++failures; std::printf("cycle %d FAILED finished=%d success=%d file=%d export=%d %ls\n", cycle, state.finished, state.success, fileOk, exportOk, state.text.c_str()); }
        csv << cycle << "," << (matroska ? "mkv" : "mp4") << "," << withReplay << "," << ok << "," << handles << "," << threads << "," << memory.PrivateUsage << "," << memory.WorkingSetSize << "," << ingress->encodedFrames.load() << "," << startMs << "," << stopMs << "," << fileOk << "," << exportOk << "\n";
        csv.flush();
        if (cycle == 9) { startHandles = handles; startThreads = threads; startPrivate = memory.PrivateUsage; } // after warm-up
        endHandles = handles; endThreads = threads; endPrivate = memory.PrivateUsage;
        if (cycle >= 10) { handlePoints.push_back({double(cycle), double(handles)}); threadPoints.push_back({double(cycle), double(threads)}); privatePoints.push_back({double(cycle), double(memory.PrivateUsage)}); }
    }
    const double hs = slope(handlePoints), ts = slope(threadPoints), ps = slope(privatePoints);
    std::printf("cycles=%d failures=%u | after warm-up (cycle 10..%d): handles %u -> %u (slope %.4f/cycle), threads %u -> %u (slope %.4f/cycle), private %.1f -> %.1f MiB (slope %.2f KiB/cycle)\n",
        cycles, failures, cycles - 1, startHandles, endHandles, hs, startThreads, endThreads, ts, startPrivate / 1048576.0, endPrivate / 1048576.0, ps / 1024.0);
    // Handles, threads and private bytes legitimately oscillate by a few percent per cycle (export workers, allocator pages). Compare the
    // mean of the first third against the mean of the last third of the post-warm-up cycles instead of fitting a line through that noise.
    auto thirds = [](const std::vector<std::pair<double, double>>& p) {
        const size_t n = p.size() / 3; double a = 0, b = 0;
        if (n == 0) return std::pair<double, double>{0, 0};
        for (size_t i = 0; i < n; ++i) { a += p[i].second; b += p[p.size() - 1 - i].second; }
        return std::pair<double, double>{a / n, b / n};
    };
    const auto handleThirds = thirds(handlePoints), threadThirds = thirds(threadPoints), privateThirds = thirds(privatePoints);
    std::printf("first-third vs last-third means: handles %.1f -> %.1f, threads %.1f -> %.1f, private %.1f -> %.1f MiB\n", handleThirds.first, handleThirds.second,
        threadThirds.first, threadThirds.second, privateThirds.first / 1048576.0, privateThirds.second / 1048576.0);
    const bool leak = cycles >= 40 && (handleThirds.second > handleThirds.first + 20 || threadThirds.second > threadThirds.first + 4 ||
        privateThirds.second > privateThirds.first + 16.0 * 1048576.0);
    (void)hs; (void)ts; (void)ps;
    std::printf("%s start/stop stress%s\n", (failures || leak) ? "FAIL" : "PASS", leak ? " (resource growth beyond bounds)" : "");
    MFShutdown();
    return (failures || leak) ? 1 : 0;
}
