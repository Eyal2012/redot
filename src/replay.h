#pragma once

#include <windows.h>
#include "video_config.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class ReplayTrack : uint8_t { Video, Mix, Game, Microphone };
struct ReplayAudioStream { ReplayTrack track; std::wstring name; };

struct ReplayPacket {
    ReplayTrack track{};
    int64_t pts{};       // 100 ns units on the common recording clock
    int64_t dts{};
    int64_t duration{};
    bool keyframe{};
    std::vector<uint8_t> bytes;
};

struct ReplaySnapshot {
    std::vector<std::shared_ptr<const ReplayPacket>> packets;
    std::vector<uint8_t> h264SequenceHeader;
    std::vector<uint8_t> aacUserData;
    VideoConfig video;
    bool hasMix{};
    bool hasGame{};
    bool hasMicrophone{};
    std::vector<ReplayAudioStream> sceneAudio;
    uint64_t bytes{};
    int64_t offset{};
    double durationSeconds{};
};

constexpr UINT kReplayEventMessage = WM_APP + 21;
struct ReplayEvent {
    bool success{};
    std::wstring text;
    std::wstring finalizedPath;
};

class ReplayBuffer : public std::enable_shared_from_this<ReplayBuffer> {
public:
    explicit ReplayBuffer(uint64_t memoryLimitBytes = 512ull * 1024 * 1024);
    void reset(std::vector<uint8_t> h264SequenceHeader, std::vector<uint8_t> aacUserData, bool gameAudio, bool microphoneAudio, VideoConfig video = {}, std::vector<ReplayAudioStream> sceneAudio = {}, bool sceneMix = false);
    void push(ReplayPacket packet);
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept;
    bool snapshot(unsigned requestedSeconds, ReplaySnapshot& result, std::wstring& error);
    bool reserveSave() noexcept;
    void finishSave(uint64_t bytes) noexcept;
    uint64_t memoryBytes() const noexcept;
    std::atomic_uint64_t memoryEvictions{0};
    double availableSeconds() const noexcept;
    uint64_t packetCount() const noexcept;
    unsigned pendingSaves() const noexcept;
    uint64_t memoryLimitBytes() const noexcept;
    void setMemoryLimitBytes(uint64_t bytes) noexcept;
    void clearHistory() noexcept;
    void setHistorySeconds(unsigned seconds);
    uint64_t retainedBytes() const { std::lock_guard lock(mutex_); return retainedBytes_; }
    uint64_t rollingBytes() const { std::lock_guard lock(mutex_); return ringBytes_; }
    void saveAsync(HWND notifyWindow, const std::wstring& folder, unsigned requestedSeconds);
    std::atomic_uint64_t exportsCompleted{0}, exportFailures{0}, lastExportMilliseconds{0}, maxExportMilliseconds{0};
private:
    // Capacity, not logical payload length: moved vectors can retain spare RAM.
    // 256 bytes conservatively cover packet/control-block/deque allocation overhead.
    static uint64_t packetMemory(const ReplayPacket& packet) noexcept { return packet.bytes.capacity() + 256; }
    uint64_t configurationMemoryLocked() const noexcept;
    void evictLocked();
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<ReplayPacket>> packets_;
    std::vector<uint8_t> sequenceHeader_;
    std::vector<uint8_t> aacUserData_;
    std::vector<ReplayAudioStream> sceneAudio_;
    uint64_t ringBytes_{};
    uint64_t retainedBytes_{};
    uint64_t memoryLimitBytes_;
    VideoConfig video_;
    unsigned historySeconds_{120};
    bool hasMix_{};
    bool hasGame_{};
    bool hasMicrophone_{};
    std::atomic_bool enabled_{false};
    std::atomic_uint64_t visibleBytes_{0};
    std::atomic_uint64_t visibleDurationMs_{0};
    std::atomic_uint64_t packetCount_{0};
    std::atomic_uint pendingSaves_{0};
};
