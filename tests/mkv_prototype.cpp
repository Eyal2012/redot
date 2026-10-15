// Standalone libwebm compatibility + crash-recovery prototype for ReDot MKV recording.
// Verification-only. Uses Media Foundation's H.264/AAC encoders (not NVENC) as a neutral packet source and the
// real MatroskaWriter. Only ever terminates the child process it created itself.
#include "mkv_writer.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

#define CHECK(expr) do { HRESULT _hr = (expr); if (FAILED(_hr)) { std::fprintf(stderr, "FAIL %s -> 0x%08X (line %d)\n", #expr, unsigned(_hr), __LINE__); std::exit(2); } } while (0)

namespace {
constexpr UINT kW = 1280, kH = 720, kFps = 30;

struct Packet { std::vector<uint8_t> bytes; int64_t pts{}; bool key{}; };

struct H264Source {
    ComPtr<IMFTransform> mft;
    std::vector<uint8_t> header;
    MFT_OUTPUT_STREAM_INFO info{};
    void open() {
        CHECK(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft)));
        ComPtr<IMFMediaType> out, in;
        CHECK(MFCreateMediaType(&out));
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, 4'000'000); out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, kW, kH); MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, kFps, 1);
        MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
        CHECK(mft->SetOutputType(0, out.Get(), 0));
        CHECK(MFCreateMediaType(&in));
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, kW, kH); MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, kFps, 1);
        MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        CHECK(mft->SetInputType(0, in.Get(), 0));
        ComPtr<ICodecAPI> api;
        if (SUCCEEDED(mft.As(&api))) {
            VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = VARIANT_TRUE; api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
            v.vt = VT_UI4; v.ulVal = kFps * 2; api->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);
        }
        ComPtr<IMFMediaType> current;
        if (SUCCEEDED(mft->GetOutputCurrentType(0, &current))) {
            UINT32 size = 0;
            if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size) {
                header.resize(size); current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), size, &size);
            }
        }
        CHECK(mft->GetOutputStreamInfo(0, &info));
        CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
    }
    void encode(uint64_t index, std::vector<Packet>& packets) {
        const DWORD bytes = kW * kH * 3 / 2;
        ComPtr<IMFMediaBuffer> buffer; CHECK(MFCreateMemoryBuffer(bytes, &buffer));
        BYTE* data = nullptr; CHECK(buffer->Lock(&data, nullptr, nullptr));
        for (UINT y = 0; y < kH; ++y) for (UINT x = 0; x < kW; ++x) data[y * kW + x] = BYTE(((x + index * 7) / 8 + y / 8) & 0xFF);
        // A white flash for 100 ms once per second makes video timing visible to the verifier.
        if (index % kFps < 3) std::memset(data + kW * (kH / 2 - 40), 235, kW * 80);
        std::memset(data + kW * kH, 128, kW * kH / 2);
        buffer->Unlock(); buffer->SetCurrentLength(bytes);
        ComPtr<IMFSample> sample; CHECK(MFCreateSample(&sample)); sample->AddBuffer(buffer.Get());
        sample->SetSampleTime(LONGLONG(index * 10'000'000ull / kFps)); sample->SetSampleDuration(10'000'000ll / kFps);
        CHECK(mft->ProcessInput(0, sample.Get(), 0));
        pull(packets);
    }
    void pull(std::vector<Packet>& packets) {
        for (;;) {
            MFT_OUTPUT_DATA_BUFFER out{}; ComPtr<IMFSample> own;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> b; CHECK(MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 1 << 20), &b));
                CHECK(MFCreateSample(&own)); own->AddBuffer(b.Get()); out.pSample = own.Get();
            }
            DWORD status = 0; HRESULT hr = mft->ProcessOutput(0, 1, &out, &status);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            CHECK(hr);
            ComPtr<IMFSample> s; if (own && out.pSample == own.Get()) s = own; else s.Attach(out.pSample);
            if (out.pEvents) out.pEvents->Release();
            if (!s) continue;
            ComPtr<IMFMediaBuffer> c; CHECK(s->ConvertToContiguousBuffer(&c));
            BYTE* d = nullptr; DWORD len = 0; CHECK(c->Lock(&d, nullptr, &len));
            Packet p; p.bytes.assign(d, d + len); c->Unlock();
            LONGLONG t = 0; s->GetSampleTime(&t); p.pts = t;
            UINT32 clean = 0; s->GetUINT32(MFSampleExtension_CleanPoint, &clean); p.key = clean != 0;
            packets.push_back(std::move(p));
        }
    }
};

struct AacSource {
    ComPtr<IMFTransform> mft; std::vector<uint8_t> userData; MFT_OUTPUT_STREAM_INFO info{};
    void open() {
        MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Audio, MFAudioFormat_AAC}; IMFActivate** act = nullptr; UINT32 n = 0;
        CHECK(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT, nullptr, &outInfo, &act, &n));
        CHECK(n ? S_OK : E_FAIL); CHECK(act[0]->ActivateObject(IID_PPV_ARGS(&mft)));
        for (UINT32 i = 0; i < n; ++i) act[i]->Release(); CoTaskMemFree(act);
        ComPtr<IMFMediaType> in, out; MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16); in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
        CHECK(mft->SetInputType(0, in.Get(), 0));
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16); out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1); out->SetUINT32(MF_MT_AVG_BITRATE, 192000);
        out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        CHECK(mft->SetOutputType(0, out.Get(), 0));
        ComPtr<IMFMediaType> cur; CHECK(mft->GetOutputCurrentType(0, &cur));
        UINT32 size = 0; if (SUCCEEDED(cur->GetBlobSize(MF_MT_USER_DATA, &size)) && size) { userData.resize(size); cur->GetBlob(MF_MT_USER_DATA, userData.data(), size, &size); }
        CHECK(mft->GetOutputStreamInfo(0, &info));
        CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)); CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
    }
    void encode(const std::vector<int16_t>& pcm, uint64_t firstFrame, std::vector<Packet>& packets) {
        const DWORD bytes = DWORD(pcm.size() * 2); ComPtr<IMFMediaBuffer> b; CHECK(MFCreateMemoryBuffer(bytes, &b));
        BYTE* d = nullptr; CHECK(b->Lock(&d, nullptr, nullptr)); std::memcpy(d, pcm.data(), bytes); b->Unlock(); b->SetCurrentLength(bytes);
        ComPtr<IMFSample> s; MFCreateSample(&s); s->AddBuffer(b.Get());
        s->SetSampleTime(LONGLONG(firstFrame * 10'000'000ull / 48000)); s->SetSampleDuration(LONGLONG(pcm.size() / 2 * 10'000'000ull / 48000));
        CHECK(mft->ProcessInput(0, s.Get(), 0)); pull(packets);
    }
    void pull(std::vector<Packet>& packets) {
        for (;;) {
            MFT_OUTPUT_DATA_BUFFER out{}; ComPtr<IMFSample> own;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> bb; MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 65536), &bb); MFCreateSample(&own); own->AddBuffer(bb.Get()); out.pSample = own.Get();
            }
            DWORD status = 0; HRESULT hr = mft->ProcessOutput(0, 1, &out, &status);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            CHECK(hr);
            ComPtr<IMFSample> s; if (own && out.pSample == own.Get()) s = own; else s.Attach(out.pSample);
            if (out.pEvents) out.pEvents->Release();
            if (!s) continue;
            ComPtr<IMFMediaBuffer> c; CHECK(s->ConvertToContiguousBuffer(&c));
            BYTE* d = nullptr; DWORD len = 0; CHECK(c->Lock(&d, nullptr, &len));
            Packet p; p.bytes.assign(d, d + len); c->Unlock(); LONGLONG t = 0; s->GetSampleTime(&t); p.pts = t; p.key = true;
            packets.push_back(std::move(p));
        }
    }
};

int writeMode(const std::wstring& path, double seconds, unsigned tracks) {
    CHECK(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    H264Source video; video.open();
    std::vector<AacSource> aac(tracks); for (auto& a : aac) a.open();
    MkvVideoSpec spec; spec.width = kW; spec.height = kH; spec.fps = kFps;
    std::wstring error;
    if (!mkvBuildAvcC(video.header, spec.avcC, error)) { std::fwprintf(stderr, L"avcC: %s\n", error.c_str()); return 3; }
    static const char* names[] = {"Mix", "Game", "Microphone", "Scene A", "Scene B"};
    std::vector<MkvAudioSpec> audio;
    for (unsigned i = 0; i < tracks; ++i) { MkvAudioSpec a; a.name = names[i % 5]; a.audioSpecificConfig = mkvAudioSpecificConfigFromMfUserData(aac[i].userData); a.isDefault = i == 0; audio.push_back(a); }
    MatroskaWriter writer;
    if (!writer.open(path, spec, audio, error)) { std::fwprintf(stderr, L"open: %s\n", error.c_str()); return 4; }
    std::printf("OPEN\n"); std::fflush(stdout);
    const auto begin = std::chrono::steady_clock::now();
    uint64_t audioFrame = 0; std::vector<Packet> packets;
    const uint64_t totalFrames = uint64_t(seconds * kFps);
    uint64_t videoCount = 0, audioCount = 0;
    for (uint64_t f = 0; f < totalFrames; ++f) {
        std::this_thread::sleep_until(begin + std::chrono::microseconds(f * 1'000'000ull / kFps));
        packets.clear(); video.encode(f, packets);
        for (auto& p : packets) { if (!writer.writeVideo(p.bytes.data(), p.bytes.size(), p.pts, p.key, error)) { std::fwprintf(stderr, L"video: %s\n", error.c_str()); return 5; } ++videoCount; }
        // 1600 PCM frames per 1/30 s keeps audio flowing in step with video.
        for (unsigned t = 0; t < tracks; ++t) {
            std::vector<int16_t> pcm(1600 * 2);
            for (size_t i = 0; i < 1600; ++i) { const double v = std::sin((audioFrame + i) * 2 * 3.14159265358979 * (440.0 * (t + 1)) / 48000.0) * 0.3 * 32767; pcm[i * 2] = pcm[i * 2 + 1] = int16_t(v); }
            std::vector<Packet> out; aac[t].encode(pcm, audioFrame, out);
            for (auto& p : out) { if (!writer.writeAudio(t, p.bytes.data(), p.bytes.size(), p.pts, error)) { std::fwprintf(stderr, L"audio: %s\n", error.c_str()); return 6; } ++audioCount; }
        }
        audioFrame += 1600;
        if (f % kFps == kFps - 1) { std::printf("PROGRESS %llu %llu %llu\n", (unsigned long long)(f + 1) / kFps, (unsigned long long)videoCount, (unsigned long long)audioCount); std::fflush(stdout); }
    }
    if (!writer.finalize(error)) { std::fwprintf(stderr, L"finalize: %s\n", error.c_str()); return 7; }
    std::printf("DONE %llu %llu flushes=%llu bytes=%llu\n", (unsigned long long)videoCount, (unsigned long long)audioCount, (unsigned long long)writer.flushes(), (unsigned long long)writer.bytesWritten());
    return 0;
}

void print(const MkvInspection& i) {
    std::printf("  docType=%s valid=%d finalized=%d cues=%d truncatedTail=%d clusters=%llu videoFrames=%llu keyframes=%llu audioFrames=%llu duration=%.3fs headerDuration=%.3fs\n",
        i.docType.c_str(), i.valid, i.finalized, i.hasCues, i.truncatedTail, (unsigned long long)i.clusters, (unsigned long long)i.videoFrames,
        (unsigned long long)i.videoKeyframes, (unsigned long long)i.audioFrames, i.durationSeconds, i.headerDurationSeconds);
    for (auto& t : i.tracks) std::printf("  track %llu type=%d codec=%s name=%s private=%zu %ux%u %.0fHz ch=%u\n", (unsigned long long)t.number, t.type, t.codecId.c_str(), t.name.c_str(), t.codecPrivate.size(), t.width, t.height, t.sampleRate, t.channels);
    for (auto& [n, st] : i.trackStats) std::printf("  track %llu frames=%llu first=%.3fs last=%.3fs\n", (unsigned long long)n, (unsigned long long)st.frames, st.firstSeconds, st.lastSeconds);
    if (!i.problem.empty()) std::wprintf(L"  problem: %s\n", i.problem.c_str());
}

// Decode through Windows' own Media Foundation MKV source: proves a stock Windows reader accepts the file.
bool mfDecode(const std::wstring& path, uint64_t& video, uint64_t& audio, unsigned& audioStreams, double& seconds) {
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) return false;
    audioStreams = 0;
    for (DWORD s = 0; s < 32; ++s) { ComPtr<IMFMediaType> t; if (FAILED(reader->GetNativeMediaType(s, 0, &t))) break; GUID m{}; t->GetGUID(MF_MT_MAJOR_TYPE, &m); if (m == MFMediaType_Audio) ++audioStreams; }
    video = audio = 0; seconds = 0;
    for (DWORD s = 0; s < 32; ++s) {
        ComPtr<IMFMediaType> t; if (FAILED(reader->GetNativeMediaType(s, 0, &t))) break;
        GUID m{}; t->GetGUID(MF_MT_MAJOR_TYPE, &m);
        ComPtr<IMFMediaType> want; MFCreateMediaType(&want); want->SetGUID(MF_MT_MAJOR_TYPE, m);
        want->SetGUID(MF_MT_SUBTYPE, m == MFMediaType_Video ? MFVideoFormat_NV12 : MFAudioFormat_PCM);
        if (FAILED(reader->SetCurrentMediaType(s, nullptr, want.Get()))) return false;
    }
    for (int guard = 0; guard < 200000; ++guard) {
        DWORD index = 0, flags = 0; LONGLONG ts = 0; ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &index, &flags, &ts, &sample))) return video > 0;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaType> cur; reader->GetCurrentMediaType(index, &cur); GUID m{}; cur->GetGUID(MF_MT_MAJOR_TYPE, &m);
        if (m == MFMediaType_Video) { ++video; seconds = std::max(seconds, ts / 1e7); } else ++audio;
    }
    return true;
}

int check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what); return ok ? 0 : 1; }

int verifyMode(const std::wstring& path, bool finalized) {
    MkvInspection i = mkvInspect(path); print(i);
    int failures = 0;
    failures += check(i.valid && i.docType == "matroska", "libwebm-parsable Matroska (DocType matroska)");
    failures += check(i.finalized == finalized, finalized ? "finalized (cues + duration)" : "unfinalized as expected");
    uint64_t v = 0, a = 0; unsigned streams = 0; double seconds = 0;
    const bool decoded = mfDecode(path, v, a, streams, seconds);
    std::printf("  MF decode: ok=%d videoSamples=%llu audioSamples=%llu audioStreams=%u lastVideo=%.3fs\n", decoded, (unsigned long long)v, (unsigned long long)a, streams, seconds);
    failures += check(decoded && v > 0, "Windows Media Foundation decodes the video track");
    return failures;
}

int crashMode(const std::wstring& exe, const fs::path& dir, unsigned tracks, double killAfterSeconds) {
    fs::create_directories(dir);
    const fs::path file = dir / L"crash.partial.mkv";
    std::error_code ec; fs::remove(file, ec);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; HANDLE readPipe{}, writePipe{};
    CreatePipe(&readPipe, &writePipe, &sa, 0); SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = writePipe; si.hStdError = GetStdHandle(STD_ERROR_HANDLE); si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" write \"" + file.wstring() + L"\" 60 " + std::to_wstring(tracks);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { std::printf("FAIL: could not start the owned writer child\n"); return 1; }
    CloseHandle(writePipe);
    std::string text; char buffer[256]; DWORD got = 0; double progress = 0; unsigned long long v = 0, a = 0;
    while (progress < killAfterSeconds && ReadFile(readPipe, buffer, sizeof(buffer) - 1, &got, nullptr) && got) {
        text.append(buffer, got); size_t at;
        while ((at = text.find('\n')) != std::string::npos) { std::string line = text.substr(0, at); text.erase(0, at + 1); double p; unsigned long long vv, aa; if (std::sscanf(line.c_str(), "PROGRESS %lf %llu %llu", &p, &vv, &aa) == 3) { progress = p; v = vv; a = aa; } }
    }
    // Terminate exactly the process this test created (handle from CreateProcess), mid-cluster by design.
    std::this_thread::sleep_for(std::chrono::milliseconds(370));
    TerminateProcess(pi.hProcess, 0xDEAD); WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(readPipe);
    std::printf("Killed owned writer after ~%.0f s (reported %llu video / %llu audio packets written)\n", progress, v, a);
    int failures = 0;
    failures += check(fs::exists(file), "partial file exists after kill");
    failures += check(mkvFileLooksOrphaned(file.wstring()), "orphan probe: no live writer holds the file");
    MkvInspection raw = mkvInspect(file.wstring()); print(raw);
    failures += check(raw.valid && !raw.finalized, "killed file parses and is recognised as unfinished");
    const double loss = progress - raw.durationSeconds;
    std::printf("  data lost vs. last progress report: %.2f s (written=%.2f s)\n", loss, raw.durationSeconds);
    failures += check(loss < 3.5, "footage loss after kill is bounded (< one cluster + in-flight frames)");
    uint64_t dv = 0, da = 0; unsigned ds = 0; double dsec = 0;
    const bool rawDecode = mfDecode(file.wstring(), dv, da, ds, dsec);
    std::printf("  MF decode of the raw killed file: ok=%d video=%llu audio=%llu audioStreams=%u\n", rawDecode, (unsigned long long)dv, (unsigned long long)da, ds);
    const fs::path recovered = dir / L"crash_recovered.mkv"; fs::remove(recovered, ec);
    MkvInspection result; std::wstring error;
    const bool ok = mkvRecover(file.wstring(), recovered.wstring(), result, error);
    if (!ok) std::wprintf(L"  recover error: %s\n", error.c_str());
    failures += check(ok, "mkvRecover produced a finalized copy");
    if (ok) {
        failures += verifyMode(recovered.wstring(), true);
        MkvInspection rec = mkvInspect(recovered.wstring());
        failures += check(rec.videoFrames == raw.videoFrames && rec.audioFrames >= raw.audioFrames - tracks * 4, "recovered copy keeps all complete frames");
        failures += check(rec.tracks.size() == tracks + 1, "recovered copy keeps every audio track");
    }
    failures += check(fs::exists(file), "original partial file was not deleted or modified by recovery");
    return failures;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc >= 5 && std::wstring(argv[1]) == L"write") { const int rc = writeMode(argv[2], _wtof(argv[3]), unsigned(_wtoi(argv[4]))); MFShutdown(); return rc; }
    if (argc >= 3 && std::wstring(argv[1]) == L"verify") { MFStartup(MF_VERSION); const int rc = verifyMode(argv[2], true); MFShutdown(); return rc ? 1 : 0; }
    if (argc >= 5 && std::wstring(argv[1]) == L"crash") { MFStartup(MF_VERSION); const int rc = crashMode(argv[0], argv[2], unsigned(_wtoi(argv[3])), _wtof(argv[4])); MFShutdown(); std::printf(rc ? "RESULT failures=%d\n" : "RESULT failures=0\n", rc); return rc ? 1 : 0; }
    std::printf("usage: write <file> <seconds> <audioTracks> | verify <file> | crash <dir> <audioTracks> <killAfterSeconds>\n");
    return 64;
}
