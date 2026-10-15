// Controlled MKV crash/recovery verification through the production RecordingPipeline (real NVENC, real AAC
// encoders, deterministic GPU frames and PCM; no WGC, no WASAPI, no audible output).
//
//   ReDotMkvCrashTest <fresh folder under build>          parent: runs every scenario below
//   ReDotMkvCrashTest --child <folder> <seconds> [fail]   owned recording child (spawned by the parent only)
//
// The parent terminates ONLY the child process it created (handle from CreateProcess) and never touches any
// other process. All files live in a fresh folder beneath this project's build directory.
#include "quiet_gpu.h"
#include "mkv_writer.h"

#include <winrt/base.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

class ToneAudio final : public SceneAudioProvider {
public:
    std::vector<std::wstring> stems() const override { return {L"Scene source A", L"Scene source B"}; }
    bool start(std::shared_ptr<RecordingIngress>, std::wstring&) override { return true; }
    void origin(std::chrono::steady_clock::time_point) override {}
    void render(uint64_t first, uint64_t frames, std::vector<int16_t>& mix, std::vector<std::vector<int16_t>>& stems) override {
        mix.assign(frames * 2, 0);
        stems.assign(2, std::vector<int16_t>(frames * 2, 0));
        for (uint64_t i = 0; i < frames; ++i) {
            const double a = std::sin((first + i) * 2 * 3.14159265358979 * 440.0 / 48000.0) * 0.25;
            const double b = std::sin((first + i) * 2 * 3.14159265358979 * 660.0 / 48000.0) * 0.25;
            stems[0][i * 2] = stems[0][i * 2 + 1] = int16_t(a * 32767);
            stems[1][i * 2] = stems[1][i * 2 + 1] = int16_t(b * 32767);
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

std::string ascii(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) out.push_back(c < 128 && c >= 32 ? char(c) : '?');
    return out;
}

int childMain(const fs::path& folder, double seconds, bool injectFailure) {
    fs::create_directories(folder / L"local");
    SetEnvironmentVariableW(L"LOCALAPPDATA", (folder / L"local").c_str());
    if (injectFailure) SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_FAIL_FRAME", L"90");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto gpu = makeQuietGpu();
    if (!gpu) { std::printf("NOGPU\n"); return 3; }
    Notifications state;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = notify; cls.lpszClassName = L"ReDotMkvCrashChild";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    constexpr unsigned kW = 640, kH = 360;
    std::vector<ComPtr<ID3D11Texture2D>> textures(12);
    for (size_t t = 0; t < textures.size(); ++t) {
        std::vector<uint32_t> pixels(kW * kH);
        for (unsigned y = 0; y < kH; ++y) for (unsigned x = 0; x < kW; ++x)
            pixels[y * kW + x] = 0xff000000u | (((x * 255 / kW + t * 21) & 255) << 16) | (((y * 255 / kH + t * 9) & 255) << 8) | ((t * 20) & 255);
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = kW; desc.Height = kH; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), kW * 4, 0};
        winrt::check_hresult(gpu->device->CreateTexture2D(&desc, &data, &textures[t]));
    }
    VideoConfig video; video.width = kW; video.height = kH; video.fps = 30; video.container = ContainerFormat::Mkv;
    RecordingAudioOptions audio; audio.gameMode = GameAudioMode::Off; audio.scene = std::make_shared<ToneAudio>();
    RecordingPipeline pipeline; std::wstring error;
    if (!pipeline.start(window, gpu, folder.wstring(), RecordingQuality::Balanced, audio, error, false, {}, video)) {
        std::printf("STARTFAIL %s\n", ascii(error).c_str()); return 4;
    }
    auto ingress = pipeline.ingress();
    const auto begin = std::chrono::steady_clock::now();
    uint64_t pushed = 0, lastReport = 0;
    for (;;) {
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (state.finished) break;
        if (elapsed >= seconds) { pipeline.requestStop(); }
        if (elapsed < seconds) ingress->push({textures[pushed++ % textures.size()], std::chrono::steady_clock::now(), {}});
        const uint64_t whole = uint64_t(elapsed);
        if (whole > lastReport) { lastReport = whole; std::printf("PROGRESS %llu %llu\n", (unsigned long long)whole, (unsigned long long)ingress->encodedFrames.load()); std::fflush(stdout); }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        if (elapsed > seconds + 30) { std::printf("TIMEOUT\n"); return 5; }
    }
    pipeline.join();
    std::printf("FINISHED success=%d text=%s\n", state.success ? 1 : 0, ascii(state.text).c_str());
    std::fflush(stdout);
    return state.success ? 0 : 6;
}

// ---- parent side ----------------------------------------------------------------------------------------

unsigned failures = 0;
void check(bool ok, const std::string& what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++failures; }

struct Child {
    PROCESS_INFORMATION process{};
    HANDLE pipe{};
    std::string buffer;
    bool start(const std::wstring& exe, const fs::path& folder, double seconds, bool fail) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; HANDLE writePipe{};
        CreatePipe(&pipe, &writePipe, &sa, 0); SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = writePipe; si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        std::wstring command = L"\"" + exe + L"\" --child \"" + folder.wstring() + L"\" " + std::to_wstring(seconds) + (fail ? L" fail" : L"");
        const bool ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &process);
        CloseHandle(writePipe);
        return ok;
    }
    // Reads lines until `done(line)` returns true or the child exits.
    template <typename Callback> bool readUntil(Callback&& done) {
        char chunk[512]; DWORD got = 0;
        for (;;) {
            size_t at;
            while ((at = buffer.find('\n')) != std::string::npos) { std::string line = buffer.substr(0, at); buffer.erase(0, at + 1); if (done(line)) return true; }
            if (!ReadFile(pipe, chunk, sizeof(chunk), &got, nullptr) || !got) return false;
            buffer.append(chunk, got);
        }
    }
    void kill() { TerminateProcess(process.hProcess, 0xDEAD); WaitForSingleObject(process.hProcess, 10000); }
    DWORD wait() { WaitForSingleObject(process.hProcess, 60000); DWORD code = 0; GetExitCodeProcess(process.hProcess, &code); return code; }
    ~Child() { if (process.hProcess) CloseHandle(process.hProcess); if (process.hThread) CloseHandle(process.hThread); if (pipe) CloseHandle(pipe); }
};

bool decodes(const std::wstring& path, uint64_t& video, uint64_t& audio, unsigned& audioStreams) {
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) return false;
    audioStreams = 0; video = audio = 0;
    for (DWORD s = 0; s < 32; ++s) {
        ComPtr<IMFMediaType> t; if (FAILED(reader->GetNativeMediaType(s, 0, &t))) break;
        GUID major{}; t->GetGUID(MF_MT_MAJOR_TYPE, &major);
        ComPtr<IMFMediaType> want; MFCreateMediaType(&want); want->SetGUID(MF_MT_MAJOR_TYPE, major);
        want->SetGUID(MF_MT_SUBTYPE, major == MFMediaType_Video ? MFVideoFormat_NV12 : MFAudioFormat_PCM);
        if (major == MFMediaType_Audio) ++audioStreams;
        if (FAILED(reader->SetCurrentMediaType(s, nullptr, want.Get()))) return false;
    }
    for (int guard = 0; guard < 100000; ++guard) {
        DWORD index = 0, flags = 0; LONGLONG ts = 0; ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &index, &flags, &ts, &sample))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaType> cur; reader->GetCurrentMediaType(index, &cur); GUID major{}; cur->GetGUID(MF_MT_MAJOR_TYPE, &major);
        (major == MFMediaType_Video ? video : audio) += 1;
    }
    return video > 0;
}

std::vector<fs::path> files(const fs::path& folder, const std::wstring& suffix) {
    std::vector<fs::path> result;
    for (auto& entry : fs::directory_iterator(folder)) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.size() >= suffix.size() && _wcsicmp(name.c_str() + name.size() - suffix.size(), suffix.c_str()) == 0) result.push_back(entry.path());
    }
    return result;
}

// Produces one genuine orphan (a hard-killed owned child recording) and stops: used by the quiet suite to prove the
// shipping app recovers it at startup. Nothing here recovers, renames or deletes anything.
int orphanOnlyMain(const fs::path& exe, const fs::path& folder) {
    const auto allowed = fs::current_path() / L"build";
    if (folder.wstring().rfind(allowed.wstring() + L"\\", 0) != 0 || fs::exists(folder)) { std::printf("Use a fresh folder inside the project build directory.\n"); return 2; }
    fs::create_directories(folder);
    Child child;
    if (!child.start(exe, folder, 60, false)) return 3;
    unsigned long long reached = 0;
    child.readUntil([&](const std::string& line) { unsigned long long s, f; if (std::sscanf(line.c_str(), "PROGRESS %llu %llu", &s, &f) == 2) { reached = s; return s >= 5; } return false; });
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    child.kill();
    const auto partial = files(folder, L".partial.mkv");
    std::printf("orphan created after ~%llu s: %zu partial file(s)\n", reached, partial.size());
    return partial.size() == 1 ? 0 : 4;
}

int parentMain(const fs::path& exe, const fs::path& root) {
    if (fs::exists(root)) { std::printf("Use a fresh folder.\n"); return 2; }
    const auto allowed = fs::current_path() / L"build";
    if (root.wstring().rfind(allowed.wstring() + L"\\", 0) != 0) { std::printf("Folder must be inside the project build directory.\n"); return 2; }
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    MFStartup(MF_VERSION);

    // ---- Scenario 1: hard-kill an active recording, then recover ---------------------------------------
    {
        const fs::path folder = root / L"killed"; fs::create_directories(folder);
        Child child;
        check(child.start(exe, folder, 60, false), "owned recording child started");
        unsigned long long seconds = 0, frames = 0;
        std::uintmax_t sizeA = 0, sizeB = 0; bool probed = false;
        child.readUntil([&](const std::string& line) {
            unsigned long long s, f;
            if (std::sscanf(line.c_str(), "PROGRESS %llu %llu", &s, &f) == 2) {
                seconds = s; frames = f;
                auto partial = files(folder, L".partial.mkv");
                if (s == 3 && !partial.empty()) sizeA = fs::file_size(partial.front());
                if (s == 5 && !partial.empty()) sizeB = fs::file_size(partial.front());
                if (s == 6 && !probed) {
                    probed = true;
                    // The recording is live: a recovery pass must leave its file completely alone.
                    const auto live = files(folder, L".partial.mkv");
                    const auto before = live.empty() ? 0 : fs::file_size(live.front());
                    auto items = mkvRecoverOrphans(folder.wstring());
                    check(live.size() == 1 && items.size() == 1 && items[0].left && !items[0].recoveredOk, "active recording is not mistaken for an abandoned file");
                    check(files(folder, L"_recovered.mkv").empty() && files(folder, L".recovered").empty() && fs::exists(live.front()) && fs::file_size(live.front()) >= before,
                        "active recording was neither copied, renamed nor truncated");
                }
                return s >= 8;
            }
            return false;
        });
        check(sizeA > 0 && sizeB > sizeA, "file grows incrementally while recording (bytes on disk at 3 s < at 5 s)");
        std::this_thread::sleep_for(std::chrono::milliseconds(230));
        child.kill();
        std::printf("killed owned child after ~%llu s (%llu frames reported encoded)\n", seconds, frames);

        const auto partial = files(folder, L".partial.mkv");
        check(partial.size() == 1 && files(folder, L".mkv").size() == 1, "after kill only the .partial.mkv exists (no false final file)");
        check(!partial.empty() && mkvFileLooksOrphaned(partial.front().wstring()), "killed file is recognised as abandoned");
        const auto originalSize = partial.empty() ? 0 : fs::file_size(partial.front());
        MkvInspection raw = partial.empty() ? MkvInspection{} : mkvInspect(partial.front().wstring());
        check(raw.valid && !raw.finalized, "killed file parses and is unfinished");
        check(raw.tracks.size() == 4, "killed file holds video + Mix + two scene stems");
        const double loss = double(seconds) - raw.durationSeconds;
        std::printf("   last reported second=%llu, footage on disk=%.2f s, loss<=%.2f s, video frames on disk=%llu\n", seconds, raw.durationSeconds, loss, (unsigned long long)raw.videoFrames);
        check(raw.durationSeconds > double(seconds) - 2.5, "at most about two seconds of footage lost to the kill");

        const auto items = mkvRecoverOrphans(folder.wstring());
        check(items.size() == 1 && items[0].recoveredOk, "orphan recovery produced a validated clip");
        const auto recovered = files(folder, L"_recovered.mkv");
        check(recovered.size() == 1 && files(folder, L".partial.mkv").empty() && files(folder, L".partial.mkv.recovered").size() == 1, "original preserved under .recovered; recovered clip is a separate file");
        if (!partial.empty() && !files(folder, L".partial.mkv.recovered").empty())
            check(fs::file_size(files(folder, L".partial.mkv.recovered").front()) == originalSize, "original bytes are intact after recovery");
        if (!recovered.empty()) {
            MkvInspection fixed = mkvInspect(recovered.front().wstring());
            check(fixed.valid && fixed.finalized && fixed.hasCues && fixed.videoFrames == raw.videoFrames && fixed.tracks.size() == 4, "recovered clip is finalized, indexed and keeps every complete video frame and track");
            uint64_t v = 0, a = 0; unsigned streams = 0;
            check(decodes(recovered.front().wstring(), v, a, streams) && v == raw.videoFrames && streams == 3, "recovered clip decodes through Windows Media Foundation with 3 audio streams");
            std::printf("   recovered: %llu video frames, %llu audio frames, %.2f s\n", (unsigned long long)fixed.videoFrames, (unsigned long long)fixed.audioFrames, fixed.durationSeconds);
        }
        check(mkvRecoverOrphans(folder.wstring()).empty(), "a second recovery pass is a no-op");
    }

    // ---- Scenario 2: recording failure keeps the footage and never reports Saved ---------------------------
    {
        const fs::path folder = root / L"failed"; fs::create_directories(folder);
        Child child;
        check(child.start(exe, folder, 30, true), "failure-injection child started");
        std::string finished;
        child.readUntil([&](const std::string& line) { if (line.rfind("FINISHED", 0) == 0) { finished = line; return true; } return false; });
        const DWORD code = child.wait();
        std::printf("   child reported: %s\n", finished.c_str());
        check(finished.find("success=0") != std::string::npos && code != 0, "injected write failure is reported as failed, not Saved");
        check(finished.find("Saved recording") == std::string::npos && files(folder, L".mkv").size() == 1 && files(folder, L".partial.mkv").size() == 1, "no false final .mkv; footage kept as .partial.mkv");
        check(finished.find("kept as") != std::string::npos, "failure message tells the user where the footage was kept");
        const auto items = mkvRecoverOrphans(folder.wstring());
        check(items.size() == 1 && items[0].recoveredOk, "footage from the failed recording is recoverable");
    }

    // ---- Scenario 3: unrecoverable / hostile partial files are never altered ----------------------------------
    {
        const fs::path folder = root / L"hostile"; fs::create_directories(folder);
        const fs::path junk = folder / L"ReDot_junk.partial.mkv", empty = folder / L"ReDot_empty.partial.mkv";
        { std::ofstream(junk, std::ios::binary) << "this is not a matroska file"; std::ofstream(empty, std::ios::binary); }
        const auto junkSize = fs::file_size(junk);
        const auto items = mkvRecoverOrphans(folder.wstring());
        bool allLeft = items.size() == 2; for (auto& item : items) allLeft = allLeft && item.left && !item.recoveredOk;
        check(allLeft && fs::exists(junk) && fs::file_size(junk) == junkSize && fs::exists(empty), "non-Matroska and empty partial files are left byte-for-byte untouched");
        check(files(folder, L"_recovered.mkv").empty(), "no clip is exposed for an unvalidated partial file");
    }

    MFShutdown();
    std::printf("%s mkv crash/recovery failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 5 && std::wstring(argv[1]) == L"--child") return childMain(argv[2], _wtof(argv[3]), std::wstring(argv[4]) == L"fail");
    if (argc == 4 && std::wstring(argv[1]) == L"--child") return childMain(argv[2], _wtof(argv[3]), false);
    if (argc == 3 && std::wstring(argv[1]) == L"--orphan-only") return orphanOnlyMain(argv[0], fs::absolute(argv[2]));
    if (argc == 2) return parentMain(argv[0], fs::absolute(argv[1]));
    return 2;
}
