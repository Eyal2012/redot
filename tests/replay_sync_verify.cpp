#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>

using Microsoft::WRL::ComPtr;
namespace {
struct Track {
    DWORD index{};
    GUID major{};
    GUID subtype{};
    UINT32 width{}, height{}, fpsN{}, fpsD{}, sampleRate{}, channels{};
};
struct Event { LONGLONG begin{}, end{}; };
std::wstring codec(const GUID& g) {
    if (g == MFAudioFormat_AAC) return L"AAC";
    if (g == MFVideoFormat_H264) return L"H264";
    return L"other";
}
// MP4 timestamps are exact to the 100 ns tick. Matroska stores whole milliseconds, so each packet is rounded
// independently (never accumulated); MKV clips get a 1.5 ms per-timestamp allowance instead of 200 ns.
LONGLONG gTimestampTolerance = 2;
bool nearEnd(LONGLONG a, LONGLONG b, LONGLONG tolerance = gTimestampTolerance) { return std::llabs(a - b) <= tolerance; }

bool configureReader(IMFSourceReader* reader, const Track& track) {
    if (FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) ||
        FAILED(reader->SetStreamSelection(track.index, TRUE))) return false;
    ComPtr<IMFMediaType> output;
    HRESULT hr = MFCreateMediaType(&output);
    if (track.major == MFMediaType_Audio) {
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
    } else {
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, track.width, track.height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, track.fpsN, track.fpsD);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    }
    return SUCCEEDED(hr) && SUCCEEDED(reader->SetCurrentMediaType(track.index, nullptr, output.Get()));
}

bool decodeAudio(const wchar_t* path, const Track& track, std::vector<Event>& beeps, const std::filesystem::path& dump = {}) {
    std::ofstream pcmFile;
    if (!dump.empty()) { pcmFile.open(dump / (L"track-" + std::to_wstring(track.index) + L".s16"), std::ios::binary); if (!pcmFile) return false; }
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path, nullptr, &reader);
    if (FAILED(hr) || !configureReader(reader.Get(), track)) return false;
    uint64_t frames = 0, blocks = 0, nonzeroSamples = 0; double sumSquares = 0; int peak = 0; LONGLONG first = 0, last = 0, lastDuration = 0;
    LONGLONG expectedNext = -1; bool monotonic = true, exact = true, active = false;
    Event current{};
    for (;;) {
        DWORD stream = 0, flags = 0; LONGLONG timestamp = 0; ComPtr<IMFSample> sample;
        hr = reader->ReadSample(track.index, 0, &stream, &flags, &timestamp, &sample);
        if (FAILED(hr)) return false;
        if (sample) {
            LONGLONG time = timestamp, duration = 0;
            sample->GetSampleTime(&time); sample->GetSampleDuration(&duration);
            if (blocks && time <= last) monotonic = false;
            if (!blocks) first = time;
            if (expectedNext >= 0 && !nearEnd(time, expectedNext)) exact = false;
            last = time; lastDuration = duration; ++blocks;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
            BYTE* data = nullptr; DWORD maximum = 0, length = 0;
            if (FAILED(buffer->Lock(&data, &maximum, &length))) return false;
            const size_t sampleFrames = length / 4;
            const auto* pcm = reinterpret_cast<const int16_t*>(data);
            if (pcmFile.is_open()) pcmFile.write(reinterpret_cast<const char*>(data), length);
            for (size_t i = 0; i < sampleFrames * 2; ++i) {
                const int amplitude = std::abs(static_cast<int>(pcm[i])); peak = std::max(peak, amplitude);
                if (amplitude) ++nonzeroSamples;
                sumSquares += static_cast<double>(pcm[i]) * pcm[i];
            }
            // One-millisecond RMS windows, rather than dating a beep to the start
            // of an entire 21.3 ms AAC block. Merge windows across sample buffers.
            for (size_t offset = 0; offset < sampleFrames; offset += 48) {
                const size_t count = std::min<size_t>(48, sampleFrames - offset);
                double energy = 0.0;
                for (size_t i = offset * 2; i < (offset + count) * 2; ++i) energy += static_cast<double>(pcm[i]) * pcm[i];
                const bool beep = std::sqrt(energy / (count * 2)) > 1600;
                const LONGLONG begin = time + static_cast<LONGLONG>(offset * 10000000ULL / 48000);
                const LONGLONG end = time + static_cast<LONGLONG>((offset + count) * 10000000ULL / 48000);
                if (beep) {
                    if (!active) { current = {begin, end}; active = true; } else current.end = end;
                } else if (active && begin - current.end >= 1500000) { beeps.push_back(current); active = false; }
            }
            buffer->Unlock();
            frames += sampleFrames;
            expectedNext = time + static_cast<LONGLONG>(sampleFrames * 10'000'000ULL / 48000);

        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    if (active) beeps.push_back(current);
    const double seconds = blocks ? (last - first + lastDuration) / 10'000'000.0 : 0.0;
    std::wcout << L"AAC track " << track.index << L": " << track.sampleRate << L" Hz, " << track.channels
        << L" channels; decoded " << frames << L" PCM frames over " << seconds
        << L"s; monotonic=" << monotonic << L" contiguous=" << exact << L"\n";
    std::wcout << L"Audio activity track " << track.index << L": peak=" << peak << L"/32768; RMS="
        << (frames ? std::sqrt(sumSquares / (frames * 2.0)) / 32768.0 : 0.0) << L"; nonzeroSamples=" << nonzeroSamples
        << L"; digitalSilence=" << (nonzeroSamples == 0) << L" (decoding/activity does not prove audible microphone speech)\n";
    return blocks && frames && monotonic && exact && (!pcmFile.is_open() || bool(pcmFile));
}

bool decodeVideo(const wchar_t* path, const Track& track, std::vector<Event>& flashes, const std::filesystem::path& dump = {}) {
    if (!track.fpsN || !track.fpsD) return false;
    // MF can infer 10000000/83333 from a 120 FPS file's first duration.
    // Validate every actual timestamp against the nearest supported CFR rate.
    const double reportedFps = double(track.fpsN) / track.fpsD;
    unsigned clockFps = static_cast<unsigned>(std::lround(reportedFps));
    if ((clockFps != 30 && clockFps != 60 && clockFps != 120) || std::abs(reportedFps - clockFps) > 0.001) return false;
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path, nullptr, &reader);
    if (FAILED(hr) || !configureReader(reader.Get(), track)) return false;
    uint64_t frames = 0; LONGLONG first = 0, last = 0, lastDuration = 0;
    bool monotonic = true, exact = true, inFlash = false; Event current{};
    double traceStart = 0, traceEnd = 0;
    { wchar_t text[64]{}; if (GetEnvironmentVariableW(L"REDOT_VERIFY_LUMA_TRACE", text, 64)) swscanf_s(text, L"%lf-%lf", &traceStart, &traceEnd); }unsigned minCenter=255,maxCenter=0,maxPlane=0;
    for (;;) {
        DWORD stream = 0, flags = 0; LONGLONG timestamp = 0; ComPtr<IMFSample> sample;
        hr = reader->ReadSample(track.index, 0, &stream, &flags, &timestamp, &sample);
        if (FAILED(hr)) return false;
        if (sample) {
            LONGLONG time = timestamp, duration = 0; sample->GetSampleTime(&time); sample->GetSampleDuration(&duration);
            const LONGLONG expected = static_cast<LONGLONG>(frames * 10'000'000ULL / clockFps);
            if (frames && time <= last) monotonic = false;
            if (std::llabs(time - expected) > gTimestampTolerance) exact = false;
            if (!frames) first = time;
            last = time; lastDuration = duration; ++frames;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
            BYTE* data = nullptr; DWORD maximum = 0, length = 0;
            if (FAILED(buffer->Lock(&data, &maximum, &length))) return false;
            const size_t pixel = static_cast<size_t>(track.height / 2) * track.width + track.width / 2;
            const bool flash = pixel < length && data[pixel] >= 220;
            // Diagnostic only: REDOT_VERIFY_LUMA_TRACE="start-end" (seconds) prints the center luma of every decoded frame in that window.
            if (traceEnd > 0 && pixel < length) { const double at = time / 1e7; if (at >= traceStart && at <= traceEnd) std::wcout << L"luma t=" << at << L" y=" << unsigned(data[pixel]) << L"\n"; }
            if(pixel<length){auto value=unsigned(data[pixel]);minCenter=std::min(minCenter,value);
                if(frames==1||value>maxCenter){maxCenter=value;if(!dump.empty()&&length>=track.width*track.height){std::ofstream image(dump/L"video-brightest-center.pgm",std::ios::binary);image<<"P5\n"<<track.width<<" "<<track.height<<"\n255\n";image.write(reinterpret_cast<const char*>(data),track.width*track.height);}}
                maxPlane=std::max(maxPlane,unsigned(*std::max_element(data,data+std::min<DWORD>(length,track.width*track.height))));}
            buffer->Unlock();
            if (flash) {
                if (!inFlash) { current = {time, time + duration}; inFlash = true; }
                else current.end = time + duration;
            } else if (inFlash) { flashes.push_back(current); inFlash = false; }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    if (inFlash) flashes.push_back(current);
    const double seconds = frames ? (last - first + lastDuration) / 10'000'000.0 : 0.0;
    std::wcout << L"H.264 track " << track.index << L": " << track.width << L"x" << track.height << L" @ "
        << track.fpsN << L"/" << track.fpsD << L"; decoded " << frames << L" frames over " << seconds
        << L"s; first=" << first << L"; monotonic=" << monotonic << L" exactRate=" << exact << L"\n";
    std::wcout<<L"Decoded luma center range="<<minCenter<<L".."<<maxCenter<<L" plane maximum="<<maxPlane<<L"; cue threshold=220 (single-point fixture detection, not scene image quality)\n";
    return frames && track.width >= 320 && track.height >= 180 && monotonic && exact;
}
}

int wmain(int argc, wchar_t** argv) {
    const bool requireEvents = argc == 3 && std::wstring(argv[1]) == L"--require-events";
    const bool dumpAudio = argc == 4 && std::wstring(argv[1]) == L"--dump-audio";
    const bool dumpVideo = argc == 4 && std::wstring(argv[1]) == L"--dump-video";
    if (argc >= 2) {
        const std::filesystem::path clip(argv[argc - 1]);
        if (_wcsicmp(clip.extension().c_str(), L".mkv") == 0) gTimestampTolerance = 15'000;
    }
    if ((!requireEvents && !dumpAudio && !dumpVideo && argc != 2) || (requireEvents && argc != 3)) {
        std::wcerr << L"Usage: ReDotReplaySyncVerify [--require-events] <clip.mp4|clip.mkv>\n       ReDotReplaySyncVerify --dump-audio <fresh build folder> <clip.mp4>\n"; return 2;
    }
    std::filesystem::path dump;
    if (dumpAudio || dumpVideo) {
        dump = std::filesystem::absolute(argv[2]).lexically_normal();
        const auto allowed = std::filesystem::current_path() / L"build";
        if (dump.wstring().rfind(allowed.wstring() + L"\\", 0) != 0 || std::filesystem::exists(dump)) return 2;
        std::filesystem::create_directories(dump);
    }
    const wchar_t* clipPath = argv[(dumpAudio || dumpVideo) ? 3 : requireEvents ? 2 : 1];
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 3;
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { CoUninitialize(); return 3; }
    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromURL(clipPath, nullptr, &reader);
    if (FAILED(hr)) { MFShutdown(); CoUninitialize(); return 4; }
    // MediaSession/Qt's native player uses presentation-descriptor ordinals;
    // the source reader may expose a different order. Never assume equivalence.
    ComPtr<IMFMediaSource> source;
    const auto sourceHr = reader->GetServiceForStream(MF_SOURCE_READER_MEDIASOURCE, GUID_NULL, IID_PPV_ARGS(&source));
    if (SUCCEEDED(sourceHr)) {
        ComPtr<IMFPresentationDescriptor> pd;
        if (SUCCEEDED(source->CreatePresentationDescriptor(&pd))) {
            DWORD count{}; pd->GetStreamDescriptorCount(&count);
            for (DWORD i=0; i<count; ++i) {
                ComPtr<IMFStreamDescriptor> sd; BOOL selected{};
                if (FAILED(pd->GetStreamDescriptorByIndex(i,&selected,&sd))) continue;
                DWORD id{}; sd->GetStreamIdentifier(&id);
                ComPtr<IMFMediaTypeHandler> handler; GUID major{};
                if (SUCCEEDED(sd->GetMediaTypeHandler(&handler))) handler->GetMajorType(&major);
                std::wcout << L"nativePD index=" << i << L" id=" << id << L" " << (major==MFMediaType_Audio?L"audio":L"video") << L" selected=" << selected << L"\n";
            }
        }
    } else std::wcout << L"nativePD unavailable HRESULT=" << std::hex << sourceHr << std::dec << L"\n";
    source.Reset(); // Release native source before reader/MF shutdown.
    std::vector<Track> tracks;
    for (DWORD i = 0; i < 32; ++i) {
        ComPtr<IMFMediaType> type;
        hr = reader->GetNativeMediaType(i, 0, &type);
        if (hr == MF_E_INVALIDSTREAMNUMBER) break;
        if (FAILED(hr)) continue;
        Track t{}; t.index = i;
        type->GetGUID(MF_MT_MAJOR_TYPE, &t.major); type->GetGUID(MF_MT_SUBTYPE, &t.subtype);
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &t.width, &t.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &t.fpsN, &t.fpsD);
        type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &t.sampleRate);
        type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &t.channels);
        std::wcout << L"stream=" << i << L" " << (t.major == MFMediaType_Video ? L"video " : L"audio ")
            << codec(t.subtype) << L"\n";
        tracks.push_back(t);
    }
    reader.Reset();
    std::vector<Event> flashes, beeps;
    DWORD timedReference = static_cast<DWORD>(-1);
    bool ok = true; unsigned audioCount = 0, videoCount = 0;
    for (const Track& track : tracks) {
        if (track.major == MFMediaType_Video) {
            ++videoCount; ok = decodeVideo(clipPath, track, flashes, dumpVideo ? dump : std::filesystem::path{}) && ok;
        } else if (track.major == MFMediaType_Audio) {
            ++audioCount; std::vector<Event> trackBeeps;
            const bool decoded = decodeAudio(clipPath, track, trackBeeps, dumpAudio ? dump : std::filesystem::path{});
            for (size_t i = 0; i < trackBeeps.size(); ++i) std::wcout << L"audioCue track=" << track.index << L" event=" << i + 1 << L" pts=" << trackBeeps[i].begin / 10000.0 << L"ms end=" << trackBeeps[i].end / 10000.0 << L"ms\n";
            if (trackBeeps.size() > beeps.size()) { beeps = std::move(trackBeeps); timedReference = track.index; }
            ok = decoded && track.subtype == MFAudioFormat_AAC && track.sampleRate == 48000 && track.channels == 2 && ok;
        }
    }
    std::wcout << L"tracks: video=" << videoCount << L" audio=" << audioCount << L"\n";
    // Retain independent detections for sessions with intentional source gaps
    // or live muting. Index-pairing below is only a convenience for complete
    // cue sequences; long-session analysis must match by time, per track.
    for(size_t i=0;i<flashes.size();++i)std::wcout<<L"videoCue event="<<i+1<<L" pts="<<flashes[i].begin/10000.0<<L"ms end="<<flashes[i].end/10000.0<<L"ms\n";
    std::wcout << L"Timed audio reference reader index=" << timedReference << L" (track with most detected cues; reader indexes are not MP4 track IDs)\n";
    if (!flashes.empty() || !beeps.empty()) {
        const size_t pairs = std::min(flashes.size(), beeps.size());
        std::vector<double> offsets;
        for (size_t i = 0; i < pairs; ++i) {
            const double ms = static_cast<double>(beeps[i].begin - flashes[i].begin) / 10'000.0;
            offsets.push_back(ms);
            std::wcout << L"event " << i + 1 << L": flash=" << flashes[i].begin / 10'000.0
                << L"ms beep=" << beeps[i].begin / 10'000.0 << L"ms audio-minus-video=" << ms << L"ms\n";
        }
        if (!offsets.empty()) {
            const auto [lo, hi] = std::minmax_element(offsets.begin(), offsets.end());
            std::wcout << L"detected flash events=" << flashes.size() << L" beep events=" << beeps.size()
                << L" paired=" << pairs << L" measured offset range=" << *lo << L".." << *hi
                << L"ms (spread=" << *hi - *lo << L"ms)\n";
        }
    }
    const bool eventsFound = flashes.size() >= 2 && beeps.size() >= 2 && flashes.size() == beeps.size();
    if (flashes.size() >= 2 && flashes.size() == beeps.size()) {
        const double seconds = (flashes.back().begin - flashes.front().begin) / 10000000.0;
        const double change = ((beeps.back().begin - flashes.back().begin) - (beeps.front().begin - flashes.front().begin)) / 10000.0;
        std::wcout << L"Endpoint offset change=" << change << L"ms over " << seconds << L"s (not proof of accumulating drift; inspect intermediate events/source scheduling)\n";
    }
    std::wcout << L"decodeResult=" << (ok ? L"PASS" : L"FAIL") << L" timedEvents=" << (eventsFound ? L"FOUND" : L"NOT FOUND") << L"\n";
    MFShutdown(); CoUninitialize();
    return ok && (!requireEvents || eventsFound) ? 0 : 5;
}
