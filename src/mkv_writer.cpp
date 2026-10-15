#include "mkv_writer.h"
#include "clip_track_names.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#include "mkvmuxer/mkvmuxer.h"
#include "mkvparser/mkvparser.h"

namespace {

std::wstring win32Text(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = count && text ? std::wstring(text, count) : L"Windows error";
    if (text) LocalFree(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result + L" (" + std::to_wstring(code) + L")";
}

// Buffered, seekable writer. The buffer only batches libwebm's many tiny element writes;
// MatroskaWriter drains it after every frame so a killed process loses at most the frame
// being written, never an unbounded backlog.
class FileWriter final : public mkvmuxer::IMkvWriter {
public:
    ~FileWriter() override { closeHandle(); }
    bool open(const std::wstring& path, std::wstring& error) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            handle_ = nullptr;
            error = L"Could not create the Matroska file: " + win32Text(GetLastError());
            return false;
        }
        buffer_.reserve(kBufferBytes);
        return true;
    }
    int32_t Write(const void* data, uint32_t length) override {
        if (failed_ || !handle_) return -1;
        if (!buffer_.empty() && position_ != bufferStart_ + static_cast<int64_t>(buffer_.size())) {
            if (!drain()) return -1;
        }
        if (buffer_.empty()) bufferStart_ = position_;
        const auto* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + length);
        position_ += length;
        size_ = std::max(size_, position_);
        if (buffer_.size() >= kBufferBytes && !drain()) return -1;
        return 0;
    }
    int64_t Position() const override { return position_; }
    int32_t Position(int64_t position) override { if (position < 0) return -1; position_ = position; return 0; }
    bool Seekable() const override { return true; }
    void ElementStartNotify(uint64_t, int64_t) override {}

    bool drain() {
        if (buffer_.empty()) return !failed_;
        if (failed_ || !handle_) return false;
        LARGE_INTEGER at{}; at.QuadPart = bufferStart_;
        DWORD written = 0;
        if (!SetFilePointerEx(handle_, at, nullptr, FILE_BEGIN) ||
            !WriteFile(handle_, buffer_.data(), static_cast<DWORD>(buffer_.size()), &written, nullptr) ||
            written != buffer_.size()) {
            failed_ = true; lastError_ = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            return false;
        }
        bytesWritten_ += written;
        buffer_.clear();
        return true;
    }
    bool durableFlush() { return handle_ && FlushFileBuffers(handle_); }
    // Releases the handle without writing buffered bytes (abandon path drains first).
    void closeHandle() { if (handle_) { CloseHandle(handle_); handle_ = nullptr; } }
    bool failed() const { return failed_; }
    DWORD lastError() const { return lastError_; }
    uint64_t bytesWritten() const { return bytesWritten_; }
    HANDLE handle() const { return handle_; }

private:
    static constexpr size_t kBufferBytes = 256 * 1024;
    HANDLE handle_{};
    std::vector<uint8_t> buffer_;
    int64_t bufferStart_{}, position_{}, size_{};
    bool failed_{};
    DWORD lastError_{};
    uint64_t bytesWritten_{};
};

class FileReader final : public mkvparser::IMkvReader {
public:
    ~FileReader() override { if (handle_) CloseHandle(handle_); }
    bool open(const std::wstring& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) { handle_ = nullptr; return false; }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle_, &size)) return false;
        size_ = size.QuadPart;
        return true;
    }
    int Read(long long pos, long length, unsigned char* buffer) override {
        if (pos < 0 || length < 0 || pos + length > size_) return -1;
        LARGE_INTEGER at{}; at.QuadPart = pos;
        DWORD read = 0;
        if (!SetFilePointerEx(handle_, at, nullptr, FILE_BEGIN) || !ReadFile(handle_, buffer, length, &read, nullptr) ||
            read != static_cast<DWORD>(length)) return -1;
        return 0;
    }
    int Length(long long* total, long long* available) override {
        if (total) *total = size_;
        if (available) *available = size_;
        return 0;
    }
    long long size() const { return size_; }

private:
    HANDLE handle_{};
    long long size_{};
};

constexpr uint64_t kTrackVideo = 1;

} // namespace

// ---------------------------------------------------------------------------
// Annex-B helpers

namespace {
struct Nal { const uint8_t* data; size_t size; };
std::vector<Nal> splitAnnexB(const uint8_t* bytes, size_t size) {
    std::vector<Nal> nals;
    size_t i = 0;
    auto startCode = [&](size_t at, size_t& length) {
        if (at + 3 <= size && bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 1) { length = 3; return true; }
        if (at + 4 <= size && bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 0 && bytes[at + 3] == 1) { length = 4; return true; }
        return false;
    };
    size_t codeLength = 0;
    while (i < size && !startCode(i, codeLength)) ++i;
    while (i < size) {
        startCode(i, codeLength);
        const size_t begin = i + codeLength;
        size_t end = begin;
        size_t next = 0;
        while (end < size && !startCode(end, next)) ++end;
        size_t trimmed = end;
        while (trimmed > begin && bytes[trimmed - 1] == 0 && end < size) --trimmed; // trailing_zero_8bits before next start code
        if (trimmed > begin) nals.push_back({bytes + begin, trimmed - begin});
        i = end;
    }
    return nals;
}
} // namespace

bool mkvBuildAvcC(const std::vector<uint8_t>& parameterSets, std::vector<uint8_t>& avcC, std::wstring& error) {
    std::vector<Nal> sps, pps;
    for (const Nal& nal : splitAnnexB(parameterSets.data(), parameterSets.size())) {
        const int type = nal.data[0] & 0x1F;
        if (type == 7) sps.push_back(nal);
        else if (type == 8) pps.push_back(nal);
    }
    if (sps.empty() || pps.empty() || sps[0].size < 4 || sps.size() > 31 || pps.size() > 255) {
        error = L"The H.264 sequence header did not contain usable SPS/PPS parameter sets for Matroska.";
        return false;
    }
    avcC.clear();
    avcC.push_back(1);
    avcC.push_back(sps[0].data[1]); avcC.push_back(sps[0].data[2]); avcC.push_back(sps[0].data[3]);
    avcC.push_back(0xFC | 3); // 4-byte NAL length prefixes
    avcC.push_back(0xE0 | static_cast<uint8_t>(sps.size()));
    for (const Nal& nal : sps) { avcC.push_back(uint8_t(nal.size >> 8)); avcC.push_back(uint8_t(nal.size)); avcC.insert(avcC.end(), nal.data, nal.data + nal.size); }
    avcC.push_back(static_cast<uint8_t>(pps.size()));
    for (const Nal& nal : pps) { avcC.push_back(uint8_t(nal.size >> 8)); avcC.push_back(uint8_t(nal.size)); avcC.insert(avcC.end(), nal.data, nal.data + nal.size); }
    const int profile = sps[0].data[1];
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134) {
        avcC.push_back(0xFC | 1); // chroma_format_idc = 4:2:0 (NVENC ARGB input is always 4:2:0 8-bit)
        avcC.push_back(0xF8);     // bit_depth_luma_minus8 = 0
        avcC.push_back(0xF8);     // bit_depth_chroma_minus8 = 0
        avcC.push_back(0);        // no SPS extensions
    }
    return true;
}

void mkvAnnexBToLengthPrefixed(const uint8_t* annexB, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    for (const Nal& nal : splitAnnexB(annexB, size)) {
        if ((nal.data[0] & 0x1F) == 9) continue; // access unit delimiter
        out.push_back(uint8_t(nal.size >> 24)); out.push_back(uint8_t(nal.size >> 16));
        out.push_back(uint8_t(nal.size >> 8)); out.push_back(uint8_t(nal.size));
        out.insert(out.end(), nal.data, nal.data + nal.size);
    }
}

std::vector<uint8_t> mkvAudioSpecificConfigFromMfUserData(const std::vector<uint8_t>& userData) {
    // HEAACWAVEINFO = WAVEFORMATEX-free 12-byte header (payload type, profile/level, struct type,
    // reserved) followed by the AudioSpecificConfig.
    constexpr size_t kHeader = 12;
    if (userData.size() <= kHeader) return {};
    return std::vector<uint8_t>(userData.begin() + kHeader, userData.end());
}

// ---------------------------------------------------------------------------
// Writer

struct MatroskaWriter::Impl {
    FileWriter file;
    mkvmuxer::Segment segment;
    std::vector<uint64_t> audioTrackNumbers;
    std::vector<uint8_t> scratch;
    bool open{};
    bool finalized{};
    std::atomic_uint64_t flushCount{0};
    unsigned flushIntervalMs{1000};
    std::thread flusher;
    std::mutex flushMutex;
    std::condition_variable flushWake;
    bool stopFlusher{};
    std::atomic_bool dirty{false};

    // libwebm rejects any frame older than the previous one across ALL tracks, but AAC packets lag video
    // by the encoder's priming delay. Hold packets in per-track FIFOs and release them in timestamp order:
    // as soon as every track has data (the minimum is then certain) or once a packet is kWindowNs behind
    // the newest one (a stalled track must not stall the file or grow memory without bound).
    struct Pending { int64_t ns{}; bool key{}; std::vector<uint8_t> data; };
    static constexpr int64_t kWindowNs = 300'000'000;
    std::vector<uint64_t> trackNumbers; // index 0 = video
    std::vector<std::deque<Pending>> queues;
    int64_t newestNs{}, lastEmittedNs{};
    std::atomic_uint64_t clamped{0};
    uint64_t queuedBytes{}, maxQueuedBytes{};

    bool emit(size_t index, Pending& item, std::wstring& error) {
        if (item.ns < lastEmittedNs) { item.ns = lastEmittedNs; clamped.fetch_add(1); }
        lastEmittedNs = item.ns;
        if (!segment.AddFrame(item.data.data(), item.data.size(), trackNumbers[index], uint64_t(item.ns), item.key) || !file.drain()) {
            error = file.failed() ? L"Matroska write failed: " + win32Text(file.lastError()) : L"libwebm rejected a frame.";
            return false;
        }
        dirty = true;
        return true;
    }
    bool pump(bool flushAll, std::wstring& error) {
        for (;;) {
            size_t best = SIZE_MAX; bool allHaveData = true;
            for (size_t i = 0; i < queues.size(); ++i) {
                if (queues[i].empty()) { allHaveData = false; continue; }
                if (best == SIZE_MAX || queues[i].front().ns < queues[best].front().ns) best = i;
            }
            if (best == SIZE_MAX) return true;
            if (!flushAll && !allHaveData && queues[best].front().ns >= newestNs - kWindowNs) return true;
            Pending item = std::move(queues[best].front());
            queues[best].pop_front();
            queuedBytes -= item.data.size();
            if (!emit(best, item, error)) return false;
        }
    }
    bool enqueue(size_t index, const uint8_t* data, size_t size, int64_t ns, bool key, std::wstring& error) {
        Pending item; item.ns = ns; item.key = key; item.data.assign(data, data + size);
        queuedBytes += size; maxQueuedBytes = std::max(maxQueuedBytes, queuedBytes);
        newestNs = std::max(newestNs, ns);
        queues[index].push_back(std::move(item));
        return pump(false, error);
    }

    void startFlusher() {
        if (!flushIntervalMs) return;
        flusher = std::thread([this] {
            std::unique_lock lock(flushMutex);
            while (!flushWake.wait_for(lock, std::chrono::milliseconds(flushIntervalMs), [this] { return stopFlusher; })) {
                if (dirty.exchange(false)) {
                    // FlushFileBuffers on another thread is safe for a synchronous handle; it only
                    // asks Windows to push the bytes already handed to WriteFile.
                    if (file.durableFlush()) flushCount.fetch_add(1);
                }
            }
        });
    }
    void stopFlusherThread() {
        if (!flusher.joinable()) return;
        { std::lock_guard lock(flushMutex); stopFlusher = true; }
        flushWake.notify_all();
        flusher.join();
    }
};

MatroskaWriter::MatroskaWriter() : impl_(std::make_unique<Impl>()) {}
MatroskaWriter::~MatroskaWriter() { abandon(); }

bool MatroskaWriter::isOpen() const noexcept { return impl_ && impl_->open; }
uint64_t MatroskaWriter::bytesWritten() const noexcept { return impl_ ? impl_->file.bytesWritten() : 0; }
uint64_t MatroskaWriter::flushes() const noexcept { return impl_ ? impl_->flushCount.load() : 0; }
uint64_t MatroskaWriter::clampedTimestamps() const noexcept { return impl_ ? impl_->clamped.load() : 0; }
uint64_t MatroskaWriter::maxQueuedBytes() const noexcept { return impl_ ? impl_->maxQueuedBytes : 0; }
void MatroskaWriter::setFlushIntervalMilliseconds(unsigned value) noexcept { if (impl_) impl_->flushIntervalMs = value; }

bool MatroskaWriter::open(const std::wstring& path, const MkvVideoSpec& video, const std::vector<MkvAudioSpec>& audio,
    std::wstring& error) {
    if (impl_->open) { error = L"The Matroska writer is already open."; return false; }
    if (!video.width || !video.height || video.avcC.empty()) { error = L"Matroska video configuration is incomplete."; return false; }
    if (!impl_->file.open(path, error)) return false;
    auto& segment = impl_->segment;
    auto fail = [&](const wchar_t* text) {
        error = text; impl_->file.closeHandle(); return false;
    };
    if (!segment.Init(&impl_->file)) return fail(L"libwebm could not start the Matroska segment.");
    segment.set_mode(mkvmuxer::Segment::kFile);
    segment.OutputCues(true);
    segment.set_max_cluster_duration(2'000'000'000ull); // ~one clusters per IDR interval; bounds unfinished tail
    segment.set_max_cluster_size(8ull * 1024 * 1024);
    segment.GetSegmentInfo()->set_writing_app("ReDot");
    segment.GetSegmentInfo()->set_muxing_app("ReDot (libwebm 1.0.0.31)");

    const uint64_t videoNumber = segment.AddVideoTrack(int32_t(video.width), int32_t(video.height), int32_t(kTrackVideo));
    auto* videoTrack = static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(videoNumber));
    if (!videoNumber || !videoTrack) return fail(L"libwebm could not add the video track.");
    videoTrack->set_codec_id("V_MPEG4/ISO/AVC");
    videoTrack->set_name("Video");
    videoTrack->set_frame_rate(video.fps);
    if (video.fps > 0) videoTrack->set_default_duration(uint64_t(1e9 / video.fps));
    if (!videoTrack->SetCodecPrivate(video.avcC.data(), video.avcC.size())) return fail(L"libwebm could not store the AVC configuration.");

    for (size_t i = 0; i < audio.size(); ++i) {
        const auto& spec = audio[i];
        if (spec.audioSpecificConfig.empty()) return fail(L"Matroska AAC track has no AudioSpecificConfig.");
        const uint64_t number = segment.AddAudioTrack(int32_t(spec.sampleRate), int32_t(spec.channels), int32_t(2 + i));
        auto* track = static_cast<mkvmuxer::AudioTrack*>(segment.GetTrackByNumber(number));
        if (!number || !track) return fail(L"libwebm could not add an audio track.");
        track->set_codec_id("A_AAC");
        track->set_name(spec.name.c_str());
        track->set_bit_depth(16);
        track->set_default_duration(1'000'000'000ull * 1024 / spec.sampleRate);
        if (!track->SetCodecPrivate(spec.audioSpecificConfig.data(), spec.audioSpecificConfig.size())) return fail(L"libwebm could not store the AAC configuration.");
        impl_->audioTrackNumbers.push_back(number);
    }
    impl_->trackNumbers.push_back(kTrackVideo);
    for (uint64_t number : impl_->audioTrackNumbers) impl_->trackNumbers.push_back(number);
    impl_->queues.assign(impl_->trackNumbers.size(), {});
    // Header + EBML + tracks must be on disk before the first frame so even a very early crash
    // leaves a parsable (empty) stream.
    if (!impl_->file.drain()) { error = L"Could not write the Matroska header: " + win32Text(impl_->file.lastError()); impl_->file.closeHandle(); return false; }
    impl_->open = true;
    impl_->startFlusher();
    return true;
}

bool MatroskaWriter::writeVideoLengthPrefixed(const uint8_t* data, size_t size, int64_t ptsHns, bool keyframe, std::wstring& error) {
    if (!impl_->open) { error = L"The Matroska writer is not open."; return false; }
    return impl_->enqueue(0, data, size, std::max<int64_t>(ptsHns, 0) * 100, keyframe, error);
}

bool MatroskaWriter::writeVideo(const uint8_t* annexB, size_t size, int64_t ptsHns, bool keyframe, std::wstring& error) {
    mkvAnnexBToLengthPrefixed(annexB, size, impl_->scratch);
    if (impl_->scratch.empty()) { error = L"The H.264 access unit contained no video NAL units."; return false; }
    return writeVideoLengthPrefixed(impl_->scratch.data(), impl_->scratch.size(), ptsHns, keyframe, error);
}

bool MatroskaWriter::writeAudio(size_t track, const uint8_t* data, size_t size, int64_t ptsHns, std::wstring& error) {
    if (!impl_->open || track >= impl_->audioTrackNumbers.size()) { error = L"Matroska audio track is unavailable."; return false; }
    return impl_->enqueue(1 + track, data, size, std::max<int64_t>(ptsHns, 0) * 100, true, error);
}

bool MatroskaWriter::finalize(std::wstring& error) {
    if (!impl_->open) { error = L"The Matroska writer is not open."; return false; }
    impl_->stopFlusherThread();
    impl_->open = false;
    std::wstring pumpError;
    const bool pumped = impl_->pump(true, pumpError);
    if (!pumped) { error = pumpError; impl_->file.drain(); impl_->file.durableFlush(); impl_->file.closeHandle(); return false; }
    const bool segmentOk = impl_->segment.Finalize();
    const bool drained = impl_->file.drain();
    const bool flushed = drained && impl_->file.durableFlush();
    const DWORD code = impl_->file.lastError();
    impl_->file.closeHandle();
    if (!segmentOk || !drained || !flushed) {
        error = !drained ? L"Could not finish writing the Matroska file: " + win32Text(code)
            : !flushed ? L"Could not flush the Matroska file to disk."
            : L"libwebm could not finalize the Matroska file (cues/duration). The footage written so far is kept as an unfinished recording.";
        return false;
    }
    impl_->finalized = true;
    return true;
}

void MatroskaWriter::abandon() noexcept {
    if (!impl_) return;
    impl_->stopFlusherThread();
    if (impl_->open) {
        impl_->open = false;
        std::wstring ignored;
        impl_->pump(true, ignored); // best effort: hand held frames to the file before closing
        impl_->file.drain(); // best effort; bytes already handed to WriteFile stay in the OS cache
        impl_->file.durableFlush();
    }
    impl_->file.closeHandle();
}

// ---------------------------------------------------------------------------
// Inspection / recovery

bool mkvFileLooksOrphaned(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false; // sharing violation = a live writer (or unreadable): never touch
    CloseHandle(handle);
    return true;
}

namespace {
struct ParsedFile {
    FileReader reader;
    mkvparser::EBMLHeader header;
    mkvparser::Segment* segment{};
    ~ParsedFile() { delete segment; }
};

template <typename Callback>
MkvInspection walk(const std::wstring& path, Callback&& onFrame) {
    MkvInspection info;
    ParsedFile file;
    if (!file.reader.open(path)) { info.problem = L"The file could not be opened for reading."; return info; }
    long long pos = 0;
    if (file.header.Parse(&file.reader, pos) < 0) { info.problem = L"Not a Matroska/EBML file."; return info; }
    info.docType = file.header.m_docType ? file.header.m_docType : "";
    if (mkvparser::Segment::CreateInstance(&file.reader, pos, file.segment) != 0 || !file.segment) {
        info.problem = L"The Matroska segment header is missing."; return info;
    }
    const long loaded = file.segment->Load();
    if (loaded < 0 && loaded != mkvparser::E_BUFFER_NOT_FULL) { info.problem = L"The Matroska stream is corrupt (load " + std::to_wstring(loaded) + L")."; return info; }
    const mkvparser::Tracks* tracks = file.segment->GetTracks();
    if (!tracks || !tracks->GetTracksCount()) { info.problem = L"The Matroska file has no track table."; return info; }
    for (unsigned long i = 0; i < tracks->GetTracksCount(); ++i) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(i);
        if (!track) continue;
        MkvTrackInfo entry;
        entry.number = track->GetNumber(); entry.type = int(track->GetType());
        entry.codecId = track->GetCodecId() ? track->GetCodecId() : "";
        entry.name = track->GetNameAsUTF8() ? track->GetNameAsUTF8() : "";
        size_t privateSize = 0;
        if (const unsigned char* codecPrivate = track->GetCodecPrivate(privateSize); codecPrivate && privateSize)
            entry.codecPrivate.assign(codecPrivate, codecPrivate + privateSize);
        if (track->GetType() == mkvparser::Track::kVideo) {
            auto* v = static_cast<const mkvparser::VideoTrack*>(track);
            entry.width = uint32_t(v->GetWidth()); entry.height = uint32_t(v->GetHeight()); entry.frameRate = v->GetFrameRate();
        } else if (track->GetType() == mkvparser::Track::kAudio) {
            auto* a = static_cast<const mkvparser::AudioTrack*>(track);
            entry.sampleRate = a->GetSamplingRate(); entry.channels = uint32_t(a->GetChannels());
        }
        info.tracks.push_back(std::move(entry));
    }
    const mkvparser::SegmentInfo* segmentInfo = file.segment->GetInfo();
    if (segmentInfo) info.headerDurationSeconds = segmentInfo->GetDuration() > 0 ? double(segmentInfo->GetDuration()) / 1e9 : 0; // GetDuration() is already in ns
    info.hasCues = file.segment->GetCues() != nullptr;
    info.finalized = info.hasCues && info.headerDurationSeconds > 0;

    std::vector<unsigned char> frameBuffer;
    const mkvparser::Cluster* cluster = file.segment->GetFirst();
    while (cluster && !cluster->EOS()) {
        ++info.clusters;
        const mkvparser::BlockEntry* entry = nullptr;
        long status = cluster->GetFirst(entry);
        while (status == 0 && entry && !entry->EOS()) {
            const mkvparser::Block* block = entry->GetBlock();
            if (!block) { info.truncatedTail = true; break; }
            const uint64_t number = block->GetTrackNumber();
            const double seconds = double(block->GetTime(cluster)) / 1e9;
            for (int f = 0; f < block->GetFrameCount(); ++f) {
                const mkvparser::Block::Frame& frame = block->GetFrame(f);
                if (frame.len <= 0 || frame.len > 128 * 1024 * 1024) { info.truncatedTail = true; continue; }
                frameBuffer.resize(size_t(frame.len));
                if (frame.Read(&file.reader, frameBuffer.data()) != 0) { info.truncatedTail = true; continue; }
                const mkvparser::Track* track = tracks->GetTrackByNumber(long(number));
                if (track && track->GetType() == mkvparser::Track::kVideo) { ++info.videoFrames; if (block->IsKey()) ++info.videoKeyframes; }
                else ++info.audioFrames;
                info.durationSeconds = std::max(info.durationSeconds, seconds);
                auto& stats = info.trackStats[number];
                ++stats.frames; if (stats.firstSeconds < 0) stats.firstSeconds = seconds; stats.lastSeconds = std::max(stats.lastSeconds, seconds);
                if (!onFrame(number, int64_t(block->GetTime(cluster)), block->IsKey(), frameBuffer)) { return info; }
            }
            const mkvparser::BlockEntry* next = nullptr;
            status = cluster->GetNext(entry, next);
            entry = next;
        }
        if (status < 0) info.truncatedTail = true; // partial final cluster
        cluster = file.segment->GetNext(cluster);
    }
    if (loaded == mkvparser::E_BUFFER_NOT_FULL) info.truncatedTail = true;
    info.valid = info.videoFrames > 0 && info.videoKeyframes > 0;
    if (!info.valid && info.problem.empty()) info.problem = L"No complete video keyframe was found.";
    return info;
}
} // namespace

MkvInspection mkvInspect(const std::wstring& path) {
    return walk(path, [](uint64_t, int64_t, bool, const std::vector<unsigned char>&) { return true; });
}

bool mkvRecover(const std::wstring& source, const std::wstring& destination, MkvInspection& result, std::wstring& error,
    const std::atomic_bool* cancel) {
    // Pass 1 validates and collects configuration; pass 2 copies. Two passes avoid creating the
    // destination unless the source is demonstrably recoverable.
    result = mkvInspect(source);
    if (!result.valid) { error = L"Recovery refused: " + result.problem; return false; }
    MkvVideoSpec video;
    std::vector<MkvAudioSpec> audio;
    std::vector<uint64_t> audioNumbers;
    uint64_t videoNumber = 0;
    for (const auto& track : result.tracks) {
        if (track.type == 1 && !videoNumber) {
            videoNumber = track.number; video.width = track.width; video.height = track.height;
            video.fps = track.frameRate > 0 ? track.frameRate : 60.0; video.avcC = track.codecPrivate;
        } else if (track.type == 2) {
            MkvAudioSpec spec; spec.name = track.name; spec.audioSpecificConfig = track.codecPrivate;
            spec.sampleRate = uint32_t(track.sampleRate > 0 ? track.sampleRate : 48000); spec.channels = track.channels ? track.channels : 2;
            audio.push_back(std::move(spec)); audioNumbers.push_back(track.number);
        }
    }
    if (!videoNumber || video.avcC.empty()) { error = L"Recovery refused: the video track configuration is missing."; return false; }
    MatroskaWriter writer;
    if (!writer.open(destination, video, audio, error)) return false;
    bool writeOk = true;
    std::wstring writeError;
    bool seenKey = false;
    bool cancelled = false;
    walk(source, [&](uint64_t number, int64_t ns, bool key, const std::vector<unsigned char>& data) {
        if (cancel && cancel->load()) { cancelled = true; return false; }
        if (number == videoNumber) {
            if (!seenKey && !key) return true; // start at the first keyframe
            seenKey = true;
            writeOk = writer.writeVideoLengthPrefixed(data.data(), data.size(), ns / 100, key, writeError);
        } else {
            for (size_t i = 0; i < audioNumbers.size(); ++i)
                if (audioNumbers[i] == number) { writeOk = writer.writeAudio(i, data.data(), data.size(), ns / 100, writeError); break; }
        }
        return writeOk;
    });
    if (cancelled) { writer.abandon(); error = L"Recovery was cancelled; the original file is untouched."; return false; }
    if (!writeOk) { writer.abandon(); error = L"Recovery copy failed: " + writeError; return false; }
    if (!writer.finalize(error)) return false;
    return true;
}

namespace fs = std::filesystem;

std::vector<MkvRecoveryItem> mkvRecoverOrphans(const std::wstring& folder, const std::atomic_bool* cancel) {
    std::vector<MkvRecoveryItem> items;
    std::error_code ec;
    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || entryError) continue;
        const std::wstring name = it->path().filename().wstring();
        constexpr wchar_t kSuffix[] = L".partial.mkv";
        constexpr size_t kLength = (sizeof(kSuffix) / sizeof(wchar_t)) - 1;
        if (name.size() > kLength && _wcsicmp(name.c_str() + name.size() - kLength, kSuffix) == 0) candidates.push_back(it->path());
    }
    for (const fs::path& source : candidates) {
        MkvRecoveryItem item;
        item.source = source.wstring();
        if (cancel && cancel->load()) { item.left = true; item.note = L"Recovery postponed (application closing)."; items.push_back(std::move(item)); break; }
        std::error_code sizeError;
        const auto size = fs::file_size(source, sizeError);
        if (sizeError || size == 0) { item.left = true; item.note = L"Left untouched: empty or unreadable file."; items.push_back(std::move(item)); continue; }
        if (!mkvFileLooksOrphaned(item.source)) { item.left = true; item.note = L"Left untouched: another process has it open (active recording)."; items.push_back(std::move(item)); continue; }
        MkvInspection before = mkvInspect(item.source);
        if (!before.valid) { item.left = true; item.note = L"Left untouched: not recoverable (" + before.problem + L")."; items.push_back(std::move(item)); continue; }
        // "<stem>.partial.mkv" -> "<stem>_recovered.mkv" (numbered if taken; CREATE_NEW also guards the race).
        const std::wstring name = source.filename().wstring();
        const std::wstring stem = name.substr(0, name.size() - 12);
        fs::path target = source.parent_path() / (stem + L"_recovered.mkv");
        for (unsigned n = 2; fs::exists(target, ec) || fs::exists(target.wstring() + L".tracks", ec); ++n)
            target = source.parent_path() / (stem + L"_recovered" + std::to_wstring(n) + L".mkv");
        std::wstring error;
        MkvInspection copied;
        const fs::path staging = target.wstring() + L".recovering";
        std::error_code stagingError; fs::remove(staging, stagingError); // only ever our own earlier staging file
        if (!mkvRecover(item.source, staging.wstring(), copied, error, cancel)) {
            fs::remove(staging, stagingError); // a staging file we created; the original is untouched
            item.left = true; item.note = L"Left untouched: " + error; items.push_back(std::move(item)); continue;
        }
        MkvInspection after = mkvInspect(staging.wstring());
        if (!after.valid || !after.finalized || after.videoFrames != before.videoFrames || after.tracks.size() != before.tracks.size()) {
            fs::remove(staging, stagingError);
            item.left = true; item.note = L"Left untouched: the repaired copy failed validation."; items.push_back(std::move(item)); continue;
        }
        std::error_code renameError;
        fs::rename(staging, target, renameError);
        if (renameError) { fs::remove(staging, stagingError); item.left = true; item.note = L"Left untouched: could not publish the repaired copy."; items.push_back(std::move(item)); continue; }
        // Track labels for the Media window (same sidecar the normal recorder writes).
        std::map<unsigned, std::string> names;
        for (const auto& track : after.tracks) if (track.type == 2 && !track.name.empty()) names[unsigned(track.number)] = track.name;
        if (!names.empty()) saveClipTrackNames(target.wstring(), names);
        // Preserve the original bytes under a name no scanner treats as a clip.
        fs::path marker = source.wstring() + L".recovered";
        for (unsigned n = 2; fs::exists(marker, ec); ++n) marker = source.wstring() + L".recovered" + std::to_wstring(n);
        fs::rename(source, marker, renameError);
        item.recovered = target.wstring(); item.recoveredOk = true; item.seconds = after.durationSeconds;
        item.note = renameError ? L"Recovered; original kept in place (could not rename it)." : L"Recovered; original preserved as " + marker.filename().wstring() + L".";
        items.push_back(std::move(item));
    }
    return items;
}
