#pragma once

#include "recording.h"
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>

class AudioCapture {
public:
    AudioCapture(AudioSourceKind source, std::shared_ptr<RecordingIngress> ingress,
        DWORD processId, GameAudioMode gameMode, std::wstring deviceId, bool meterOnly = false);
    ~AudioCapture();
    bool start(std::wstring& error);
    void stop() noexcept;
private:
    void run() noexcept;
    AudioSourceKind source_;
    std::shared_ptr<RecordingIngress> ingress_;
    DWORD processId_{};
    GameAudioMode gameMode_{};
    std::wstring deviceId_;
    bool meterOnly_{};
    std::atomic_bool stop_{false};
    std::atomic_bool failed_{false};
    std::mutex startupMutex_;
    std::condition_variable startupWake_;
    bool startupDone_{};
    std::wstring startupError_;
    std::thread thread_;
    void signalStartup(std::wstring error = {});
};

// Opens the selected mic through the same WASAPI implementation as recording.
// No encoder or PCM queue is created; startup and stopping happen on this worker.
class MicrophoneMonitor {
public:
    ~MicrophoneMonitor();
    bool start(const std::wstring& endpointId, std::wstring& error);
    void requestStop() noexcept { stop_.store(true); }
    bool busy() const noexcept { return busy_.load(); }
    std::shared_ptr<RecordingIngress> ingress() const { return ingress_; }
    std::wstring error() const { std::lock_guard lock(mutex_); return error_; }
    void clearError() { std::lock_guard lock(mutex_); error_.clear(); }
private:
    std::thread worker_;
    std::atomic_bool busy_{false}, stop_{false};
    std::shared_ptr<RecordingIngress> ingress_;
    mutable std::mutex mutex_;
    std::wstring error_;
};
