#include "clip_track_names.h"
#include "replay.h"
#include "timeline.h"
#include <chrono>
#include "logging.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <filesystem>

using Microsoft::WRL::ComPtr;
namespace {
constexpr int64_t kHns = 10'000'000;

constexpr unsigned kMaxSaves = 2;
std::atomic_uint64_t g_clipSequence{0};
std::wstring hrString(HRESULT hr) {
    wchar_t text[16]{}; swprintf_s(text, L"%08X", static_cast<unsigned>(hr)); return text;
}
bool writePacket(IMFSinkWriter* writer, DWORD stream, const ReplayPacket& packet, int64_t offset, std::wstring& error) {
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.bytes.size()), &buffer);
    BYTE* destination = nullptr; DWORD maximum = 0;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&destination, &maximum, nullptr);
    if (SUCCEEDED(hr)) {
        if (maximum < packet.bytes.size()) hr = E_OUTOFMEMORY;
        else CopyMemory(destination, packet.bytes.data(), packet.bytes.size());
        buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(static_cast<DWORD>(packet.bytes.size()));
    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    const int64_t start = packet.pts - offset;
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(start);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(packet.duration);
    if (SUCCEEDED(hr) && packet.track == ReplayTrack::Video && packet.keyframe)
        hr = sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.Get());
    if (FAILED(hr)) { error = L"MP4 clip export failed while writing a compressed packet (HRESULT 0x" + hrString(hr) + L")."; return false; }
    return true;
}

bool exportMp4(const ReplaySnapshot& snapshot, const std::wstring& folder, std::wstring& finalPath, std::wstring& error) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) { error = L"Could not initialize COM for replay clip export (HRESULT 0x" + hrString(co) + L")."; return false; }
    struct ExportRuntime {
        bool comStarted{}, mfStarted{};
        ~ExportRuntime() { if (mfStarted) MFShutdown(); if (comStarted) CoUninitialize(); }
    } runtime{SUCCEEDED(co), false};
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { error = L"Media Foundation could not start for clip export (HRESULT 0x" + hrString(hr) + L")."; return false; }
    runtime.mfStarted = true;
    SYSTEMTIME now{}; GetLocalTime(&now);
    const uint64_t sequence = g_clipSequence.fetch_add(1, std::memory_order_relaxed);
    wchar_t stamp[96]{}; swprintf_s(stamp, L"ReDot_Clip_%04u%02u%02u_%02u%02u%02u_%03u_%06llu", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, sequence);
    std::error_code ec; std::filesystem::create_directories(folder, ec);
    if (ec) { error = L"Could not create clip output folder (error " + std::to_wstring(ec.value()) + L")."; return false; }
    finalPath = (std::filesystem::path(folder) / (std::wstring(stamp) + L".mp4")).wstring();
    for (unsigned suffix = 1; std::filesystem::exists(finalPath) || std::filesystem::exists(finalPath.substr(0, finalPath.size() - 4) + L".partial.mp4"); ++suffix)
        finalPath = (std::filesystem::path(folder) / (std::wstring(stamp) + L"_" + std::to_wstring(suffix) + L".mp4")).wstring();
    const std::wstring partial = finalPath.substr(0, finalPath.size() - 4) + L".partial.mp4";
    struct PartialFile {
        const std::wstring& path; bool owned{}, committed{};
        ~PartialFile() {
            if (owned && !committed) {
                std::error_code ec; std::filesystem::remove(path, ec);
                if (ec) logLine(L"Incomplete replay temporary file remains: %s (cleanup error %d).", path.c_str(), ec.value());
            }
        }
    } partialFile{partial};
    ComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(partial.c_str(), nullptr, nullptr, &writer);
    partialFile.owned = SUCCEEDED(hr);
    DWORD videoStream = 0;
    ComPtr<IMFMediaType> videoType;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&videoType);
    if (SUCCEEDED(hr)) hr = videoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = videoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = videoType->SetUINT32(MF_MT_AVG_BITRATE, snapshot.video.bitrateMbps * 1000000);
    if (SUCCEEDED(hr)) hr = videoType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(videoType.Get(), MF_MT_FRAME_SIZE, snapshot.video.width, snapshot.video.height);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(videoType.Get(), MF_MT_FRAME_RATE, snapshot.video.fps, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(videoType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = videoType->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, snapshot.h264SequenceHeader.data(), static_cast<UINT32>(snapshot.h264SequenceHeader.size()));
    if (SUCCEEDED(hr)) hr = writer->AddStream(videoType.Get(), &videoStream);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoStream, videoType.Get(), nullptr);

    std::vector<ReplayAudioStream> audio=snapshot.sceneAudio;
    if(snapshot.hasMicrophone)audio.insert(audio.begin(),{ReplayTrack::Microphone,L"Microphone"});
    if(snapshot.hasGame)audio.insert(audio.begin(),{ReplayTrack::Game,L"Game"});
    if(snapshot.hasMix)audio.insert(audio.begin(),{ReplayTrack::Mix,L"Mix"});
    std::map<ReplayTrack,DWORD> audioStreams;
    for (const auto& stream : audio) {
        if(FAILED(hr))break;
        ComPtr<IMFMediaType> audioType;
        hr = MFCreateMediaType(&audioType);
        if (SUCCEEDED(hr)) hr = audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
        if (SUCCEEDED(hr)) hr = audioType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        if (SUCCEEDED(hr) && !snapshot.aacUserData.empty()) hr = audioType->SetBlob(MF_MT_USER_DATA, snapshot.aacUserData.data(), static_cast<UINT32>(snapshot.aacUserData.size()));
        if (SUCCEEDED(hr)) hr = writer->AddStream(audioType.Get(), &audioStreams[stream.track]);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStreams[stream.track], audioType.Get(), nullptr);
    }
    if (SUCCEEDED(hr)) hr = writer->BeginWriting();
    std::vector<const ReplayPacket*> ordered;
    ordered.reserve(snapshot.packets.size());
    for (const auto& p : snapshot.packets) ordered.push_back(p.get());
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->dts < b->dts; });
    uint64_t writtenPackets = 0;
    for (const ReplayPacket* packet : ordered) {
        if (diagnosticSetting(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET") && ++writtenPackets >= diagnosticSetting(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET")) { hr = E_FAIL; error = L"Controlled replay write failure (test hook)."; break; }
        if (FAILED(hr)) break;
        DWORD stream = videoStream;
        if (packet->track != ReplayTrack::Video) {
            auto found=audioStreams.find(packet->track);if(found==audioStreams.end())continue;
            stream=found->second;
        }
        if (!writePacket(writer.Get(), stream, *packet, snapshot.offset, error)) { hr = E_FAIL; break; }
    }
    if (SUCCEEDED(hr)) Sleep(static_cast<DWORD>(std::min<uint64_t>(3000, diagnosticSetting(L"PANDACLIP_TEST_EXPORT_DELAY_MS"))));
    if (SUCCEEDED(hr) && diagnosticSetting(L"PANDACLIP_TEST_EXPORT_FAIL_FINALIZE")) { hr = E_FAIL; error = L"Controlled replay finalization failure (test hook)."; }
    if (SUCCEEDED(hr)) hr = writer->Finalize();
    writer.Reset();
    if (SUCCEEDED(hr)) {
        std::filesystem::rename(partial, finalPath, ec);
        if (ec) { error = L"Clip MP4 was finalized but could not be renamed (error " + std::to_wstring(ec.value()) + L")."; hr = E_FAIL; }
        else { partialFile.committed = true; std::map<unsigned,std::string> names;for(auto& stream:audio){auto label=stream.name;int n=WideCharToMultiByte(CP_UTF8,0,label.data(),int(label.size()),nullptr,0,nullptr,nullptr);std::string utf8(n,'\0');WideCharToMultiByte(CP_UTF8,0,label.data(),int(label.size()),utf8.data(),n,nullptr,nullptr);names[audioStreams[stream.track]+1]=utf8;}saveClipTrackNames(finalPath,names); }
    } else if (error.empty()) error = L"Could not finalize compressed clip MP4 (HRESULT 0x" + hrString(hr) + L").";
    return SUCCEEDED(hr);
}
}

ReplayBuffer::ReplayBuffer(uint64_t memoryLimitBytes) : memoryLimitBytes_(memoryLimitBytes) {}

uint64_t ReplayBuffer::configurationMemoryLocked() const noexcept {
    uint64_t bytes = sequenceHeader_.capacity() + aacUserData_.capacity();
    for (const auto& stream : sceneAudio_) bytes += sizeof(stream) + stream.name.capacity() * sizeof(wchar_t);
    return bytes;
}

void ReplayBuffer::reset(std::vector<uint8_t> header, std::vector<uint8_t> aacUserData, bool gameAudio, bool microphoneAudio, VideoConfig video, std::vector<ReplayAudioStream> sceneAudio, bool sceneMix) {
    std::lock_guard lock(mutex_);
    packets_.clear(); ringBytes_ = 0;
    sequenceHeader_ = std::move(header);
    aacUserData_ = std::move(aacUserData);
    memoryEvictions = 0;
    sceneAudio_=std::move(sceneAudio);
    video_ = video; hasMix_ = video.trackMix && (gameAudio || microphoneAudio || !sceneAudio_.empty() || sceneMix);
    hasGame_ = gameAudio && video.trackGame; hasMicrophone_ = microphoneAudio && video.trackMicrophone;
    visibleBytes_.store(configurationMemoryLocked() + retainedBytes_, std::memory_order_relaxed);
    visibleDurationMs_.store(0, std::memory_order_relaxed); packetCount_.store(0, std::memory_order_relaxed);
}

void ReplayBuffer::push(ReplayPacket packet) {
    if (packet.bytes.empty() || !enabled_.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex_);
    if (!enabled_.load(std::memory_order_relaxed)) return;
    auto item = std::make_shared<ReplayPacket>(std::move(packet));
#ifdef REDOT_REPLAY_TEST_ALLOC_FAILURE
    if (diagnosticSetting(L"PANDACLIP_TEST_REPLAY_INSERT_ALLOCATION")) throw std::bad_alloc();
#endif
    const uint64_t bytes = packetMemory(*item);
    const auto position = std::upper_bound(packets_.begin(), packets_.end(), item->dts,
        [](int64_t dts, const auto& existing) { return dts < existing->dts; });
    packets_.insert(position, std::move(item));
    // Commit accounting only after both packet and deque allocations succeeded.
    ringBytes_ += bytes;
    evictLocked();
}

void ReplayBuffer::setEnabled(bool enabled) noexcept {
    if (!enabled) {
        enabled_.store(false, std::memory_order_relaxed);
        std::lock_guard lock(mutex_); packets_.clear(); ringBytes_ = 0; evictLocked();
    } else enabled_.store(true, std::memory_order_relaxed);
}

bool ReplayBuffer::enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

void ReplayBuffer::evictLocked() {
    while (!packets_.empty()) {
        const int64_t newest = packets_.back()->pts;
        const bool tooOld = newest - packets_.front()->pts > static_cast<int64_t>(historySeconds_) * kHns;
        const bool tooLarge = ringBytes_ + retainedBytes_ + configurationMemoryLocked() > memoryLimitBytes_;
        if (!tooOld && !tooLarge) break;
        if (tooLarge) ++memoryEvictions;
        ringBytes_ -= packetMemory(*packets_.front()); packets_.pop_front();
    }
    // A snapshot must begin on an independently decodable IDR. Drop any leading dependent GOP.
    auto key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p->track == ReplayTrack::Video && p->keyframe; });
    if (key != packets_.begin() && key != packets_.end()) {
        const int64_t firstPts = (*key)->pts;
        while (!packets_.empty() && packets_.front()->pts < firstPts) { ringBytes_ -= packetMemory(*packets_.front()); packets_.pop_front(); }
    }
    visibleBytes_.store(ringBytes_ + retainedBytes_ + configurationMemoryLocked(), std::memory_order_relaxed);
    const int64_t span = packets_.empty() ? 0 : std::max<int64_t>(0, packets_.back()->pts - packets_.front()->pts);
    visibleDurationMs_.store(static_cast<uint64_t>(span * 1000 / kHns), std::memory_order_relaxed);
    packetCount_.store(packets_.size(), std::memory_order_relaxed);
}

bool ReplayBuffer::snapshot(unsigned requestedSeconds, ReplaySnapshot& result, std::wstring& error) {
    std::lock_guard lock(mutex_);
    auto firstVideo = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p->track == ReplayTrack::Video; });
    auto lastVideo = std::find_if(packets_.rbegin(), packets_.rend(), [](const auto& p) { return p->track == ReplayTrack::Video; });
    if (firstVideo == packets_.end() || lastVideo == packets_.rend()) { error = L"Replay buffer is warming up; no complete video frame is available yet."; return false; }
    const int64_t videoEnd = (*lastVideo)->pts + (*lastVideo)->duration;
    const int64_t target = videoEnd - static_cast<int64_t>(requestedSeconds) * kHns;
    auto key = packets_.end();
    for (auto it = packets_.begin(); it != packets_.end(); ++it) {
        if ((*it)->track == ReplayTrack::Video && (*it)->keyframe && (*it)->pts >= target) { key = it; break; }
    }
    if (key == packets_.end()) key = std::find_if(packets_.begin(), packets_.end(), [](const auto& p) { return p->track == ReplayTrack::Video && p->keyframe; });
    if (key == packets_.end()) { error = L"Replay buffer has not reached an H.264 keyframe yet."; return false; }
    result = {}; result.video = video_; result.hasMix = hasMix_; result.offset = (*key)->pts; result.hasGame = hasGame_; result.hasMicrophone = hasMicrophone_;
    result.h264SequenceHeader = sequenceHeader_;
    result.aacUserData = aacUserData_;
    result.sceneAudio=sceneAudio_;
    int64_t commonEnd = videoEnd;
    auto latestEnd = [&](ReplayTrack track) {
        for (auto it = packets_.rbegin(); it != packets_.rend(); ++it)
            if ((*it)->track == track) return (*it)->pts + (*it)->duration;
        return result.offset;
    };
    if (hasMix_ || hasGame_ || hasMicrophone_) {
        if (hasMix_) commonEnd = std::min(commonEnd, latestEnd(ReplayTrack::Mix));
        if (hasGame_) commonEnd = std::min(commonEnd, latestEnd(ReplayTrack::Game));
        if (hasMicrophone_) commonEnd = std::min(commonEnd, latestEnd(ReplayTrack::Microphone));
    }
    for(auto& stream:sceneAudio_)commonEnd=std::min(commonEnd,latestEnd(stream.track));
    if (commonEnd <= result.offset) { error = L"Replay AAC encoders are warming up; no synchronized audio/video interval is available yet."; return false; }
    int64_t exportedVideoEnd = result.offset;
    for (const auto& packet : packets_) {
        // Video starts at the selected IDR. Keep AAC access units intact and start each audio
        // track on its first full packet at/after the same IDR offset.
        if (packet->pts < result.offset || packet->pts >= commonEnd) continue;
        if (packet->track == ReplayTrack::Video) exportedVideoEnd = std::max(exportedVideoEnd, packet->pts + packet->duration);
        result.bytes += packetMemory(*packet); result.packets.push_back(packet);
    }
    result.bytes += result.h264SequenceHeader.capacity() + result.aacUserData.capacity() + result.packets.capacity() * sizeof(result.packets[0]);
    for (const auto& stream : result.sceneAudio) result.bytes += sizeof(stream) + stream.name.capacity() * sizeof(wchar_t);
    result.durationSeconds = static_cast<double>(exportedVideoEnd - result.offset) / kHns;
    if (result.packets.empty() || result.durationSeconds <= 0) { error = L"Replay snapshot does not contain a playable GOP."; return false; }
    retainedBytes_ += result.bytes;
    evictLocked();
    if (ringBytes_ + retainedBytes_ + configurationMemoryLocked() > memoryLimitBytes_) {
        retainedBytes_ -= result.bytes;
        result = {}; // Failed snapshots must not retain unaccounted packet references.
        evictLocked();
        error = L"The selected clip snapshot would exceed the replay memory limit. Lower clip duration or raise the memory cap.";
        return false;
    }
    return true;
}

bool ReplayBuffer::reserveSave() noexcept {
    unsigned count = pendingSaves_.load(std::memory_order_relaxed);
    while (count < kMaxSaves) {
        if (pendingSaves_.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel)) {
            return true;
        }
    }
    return false;
}

void ReplayBuffer::finishSave(uint64_t bytes) noexcept {
    { std::lock_guard lock(mutex_); retainedBytes_ = retainedBytes_ > bytes ? retainedBytes_ - bytes : 0; evictLocked(); }
    pendingSaves_.fetch_sub(1, std::memory_order_acq_rel);
}

uint64_t ReplayBuffer::memoryBytes() const noexcept { return visibleBytes_.load(std::memory_order_relaxed); }
double ReplayBuffer::availableSeconds() const noexcept { return visibleDurationMs_.load(std::memory_order_relaxed) / 1000.0; }
uint64_t ReplayBuffer::packetCount() const noexcept { return packetCount_.load(std::memory_order_relaxed); }
unsigned ReplayBuffer::pendingSaves() const noexcept { return pendingSaves_.load(std::memory_order_relaxed); }
uint64_t ReplayBuffer::memoryLimitBytes() const noexcept { std::lock_guard lock(mutex_); return memoryLimitBytes_; }

void ReplayBuffer::setMemoryLimitBytes(uint64_t bytes) noexcept {
    std::lock_guard lock(mutex_);
    if (pendingSaves_.load()) { logLine(L"Replay RAM-cap change postponed: exports still retain snapshots."); return; }
    memoryLimitBytes_ = std::clamp<uint64_t>(bytes, 128ull * 1024 * 1024, 4096ull * 1024 * 1024);
    evictLocked();
}

void ReplayBuffer::clearHistory() noexcept {
    std::lock_guard lock(mutex_); packets_.clear(); ringBytes_ = 0; evictLocked();
}

void ReplayBuffer::saveAsync(HWND window, const std::wstring& folder, unsigned seconds) {
    auto notify = [window](bool ok, std::wstring text) {
        auto* event = new ReplayEvent{ok, std::move(text)};
        if (!window || !PostMessageW(window, kReplayEventMessage, 0, reinterpret_cast<LPARAM>(event))) delete event;
    };
    if (!reserveSave()) { exportFailures.fetch_add(1); notify(false, L"Two clip exports are already pending. Wait for one to finish before saving again."); return; }
    auto self = shared_from_this();
    try {
        std::thread([self, seconds, window, folder] {
            struct Reservation {
                std::shared_ptr<ReplayBuffer> buffer; uint64_t bytes{};
                ~Reservation() { buffer->finishSave(bytes); }
            } reservation{self};
            ReplaySnapshot snapshot; std::wstring failure, path; bool ok = false;
            const auto started = std::chrono::steady_clock::now();
            try {
                if (self->snapshot(seconds, snapshot, failure)) {
                    reservation.bytes = snapshot.bytes;
                    logLine(L"Replay snapshot: common offset=%lld hns; duration=%.3f s; bytes=%llu.", snapshot.offset, snapshot.durationSeconds, snapshot.bytes);
                    ok = exportMp4(snapshot, folder, path, failure);
                }
            } catch (...) { failure = L"Replay export failed while allocating or writing its snapshot. Capture continues."; }
            const auto ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
            self->lastExportMilliseconds.store(ms);
            uint64_t prior = self->maxExportMilliseconds.load();
            while (ms > prior && !self->maxExportMilliseconds.compare_exchange_weak(prior, ms)) {}
            (ok ? self->exportsCompleted : self->exportFailures).fetch_add(1);
            logLine(L"Replay export completed: success=%d, elapsed=%llu ms.", ok, ms);
            if (ok) {
                wchar_t duration[64]{}; swprintf_s(duration, L" (%.1f seconds; keyframe-aligned)", snapshot.durationSeconds);
                failure = L"Saved clip: " + std::filesystem::path(path).filename().wstring() + duration;
                logLine(L"Replay clip saved: %s; actual duration=%.3f seconds, packets=%zu, bytes=%llu; common offset=%lld hns.", path.c_str(), snapshot.durationSeconds, snapshot.packets.size(), snapshot.bytes, snapshot.offset);
            } else logLine(L"Replay clip export failed: %s", failure.c_str());
            auto* event = new ReplayEvent{ok, std::move(failure), ok ? path : std::wstring{}};
            if (!window || !PostMessageW(window, kReplayEventMessage, 0, reinterpret_cast<LPARAM>(event))) delete event;
        }).detach();
    } catch (...) { finishSave(0); exportFailures.fetch_add(1); notify(false, L"Could not start the bounded clip export worker. Capture continues."); }
}

void ReplayBuffer::setHistorySeconds(unsigned seconds) { std::lock_guard lock(mutex_); historySeconds_ = std::clamp(seconds, 5u, 600u); evictLocked(); }
