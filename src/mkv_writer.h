#pragma once
// Incremental Matroska (.mkv) writer for H.264 + AAC on top of libwebm's mkvmuxer,
// plus validation/recovery helpers built on mkvparser. No dependency on the
// recording engine: the recorder hands it compressed packets only.
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct MkvVideoSpec {
    uint32_t width{}, height{};
    double fps{60.0};
    std::vector<uint8_t> avcC; // AVCDecoderConfigurationRecord (CodecPrivate)
};
struct MkvAudioSpec {
    std::string name;                 // UTF-8 track name (Mix, Game, Microphone, scene stem...)
    std::vector<uint8_t> audioSpecificConfig; // CodecPrivate for A_AAC
    uint32_t sampleRate{48000}, channels{2};
    bool isDefault{};
};

// Annex-B <-> length-prefixed helpers (exposed for tests).
bool mkvBuildAvcC(const std::vector<uint8_t>& annexBParameterSets, std::vector<uint8_t>& avcC, std::wstring& error);
// Converts one Annex-B access unit to 4-byte length-prefixed NALs and drops access-unit delimiters.
void mkvAnnexBToLengthPrefixed(const uint8_t* annexB, size_t size, std::vector<uint8_t>& out);
// Extracts the AudioSpecificConfig from Media Foundation MF_MT_USER_DATA (HEAACWAVEINFO + ASC).
std::vector<uint8_t> mkvAudioSpecificConfigFromMfUserData(const std::vector<uint8_t>& userData);

class MatroskaWriter {
public:
    MatroskaWriter();
    ~MatroskaWriter();
    MatroskaWriter(const MatroskaWriter&) = delete;
    MatroskaWriter& operator=(const MatroskaWriter&) = delete;

    // Creates `path` with CREATE_NEW (never overwrites) and shares it read-only while open,
    // so a second process can tell an active recording from an orphan.
    bool open(const std::wstring& path, const MkvVideoSpec& video, const std::vector<MkvAudioSpec>& audio,
        std::wstring& error);
    // Annex-B access unit; timestamps in 100 ns units on the recording clock.
    bool writeVideo(const uint8_t* annexB, size_t size, int64_t ptsHns, bool keyframe, std::wstring& error);
    bool writeVideoLengthPrefixed(const uint8_t* data, size_t size, int64_t ptsHns, bool keyframe, std::wstring& error);
    bool writeAudio(size_t track, const uint8_t* data, size_t size, int64_t ptsHns, std::wstring& error);
    // Writes cues/duration/segment size, flushes to disk and closes. After success the file is complete.
    bool finalize(std::wstring& error);
    // Closes the handle without finalizing: the file stays a valid-but-unfinished (recoverable) Matroska stream.
    void abandon() noexcept;
    bool isOpen() const noexcept;
    uint64_t bytesWritten() const noexcept;
    uint64_t flushes() const noexcept;
    uint64_t clampedTimestamps() const noexcept; // frames whose timestamp had to be raised to keep file order
    uint64_t maxQueuedBytes() const noexcept;    // high-water of the bounded interleave queue
    // Flush interval for the background durability thread (default 1000 ms; 0 disables).
    void setFlushIntervalMilliseconds(unsigned value) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct MkvTrackInfo {
    uint64_t number{};
    int type{}; // 1 video, 2 audio
    std::string codecId, name;
    std::vector<uint8_t> codecPrivate;
    uint32_t width{}, height{};
    double sampleRate{};
    uint32_t channels{};
    double frameRate{};
};
struct MkvInspection {
    bool valid{};          // header + tracks parsed and at least one decodable frame found
    bool finalized{};      // segment size known, duration present (clean finalize)
    bool hasCues{};
    bool truncatedTail{};  // data after the last complete frame could not be parsed
    std::string docType;
    std::vector<MkvTrackInfo> tracks;
    uint64_t clusters{}, videoFrames{}, videoKeyframes{}, audioFrames{};
    double durationSeconds{};   // from the last parsed frame (not the header)
    double headerDurationSeconds{};
    struct TrackStats { uint64_t frames{}; double firstSeconds{-1}, lastSeconds{}; };
    std::map<uint64_t, TrackStats> trackStats;
    std::wstring problem;
};
// Parses a Matroska file read-only (shared read, never modifies it).
MkvInspection mkvInspect(const std::wstring& path);
// Copies every complete frame of `source` into a new, finalized file at `destination`
// (CREATE_NEW; fails if it exists). The source is only read.
bool mkvRecover(const std::wstring& source, const std::wstring& destination, MkvInspection& result, std::wstring& error,
    const std::atomic_bool* cancel = nullptr);
// True when no other process holds the file open for writing (no sharing violation on a read/write open).
bool mkvFileLooksOrphaned(const std::wstring& path);

// Startup recovery for one output folder. Looks only at "*.partial.mkv" regular files that no process has open
// for writing (an active recording holds its file with read-only sharing, so it is never touched). A file is
// repaired only if it contains a complete video keyframe; the repaired result is a NEW "<name>_recovered.mkv"
// (finalized, with cues and duration) that is validated before it is published. The original is never
// overwritten or deleted: it is renamed to "<name>.partial.mkv.recovered" once the copy is verified, and
// files that cannot be validated are left exactly as they are.
struct MkvRecoveryItem {
    std::wstring source, recovered, note;
    bool recoveredOk{};   // a validated recovered clip now exists
    bool left{};          // intentionally left untouched (active, empty, unreadable, cancelled)
    double seconds{};
};
std::vector<MkvRecoveryItem> mkvRecoverOrphans(const std::wstring& folder, const std::atomic_bool* cancel = nullptr);
