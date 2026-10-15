#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <array>
#include <vector>
#include "replay.h"

using Microsoft::WRL::ComPtr;

constexpr UINT kRecordingEventMessage = WM_APP + 20;
enum class RecordingEventKind { Started, Progress, Finished };
struct RecordingEvent {
    RecordingEventKind kind{};
    bool success{};
    std::wstring text;
    uint64_t elapsedMilliseconds{};
    uint64_t encodedFrames{};
    uint64_t queueDrops{};
    uint64_t repeatedFrames{};
    uint64_t replacedFrames{};
    std::wstring finalizedPath;
};

enum class RecordingQuality { Balanced, HighQuality };

struct GpuResources {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    std::shared_ptr<std::mutex> contextMutex = std::make_shared<std::mutex>();
    std::wstring adapterName;
    UINT adapterVendorId{};
};

struct RecordedFrame {
    ComPtr<ID3D11Texture2D> texture;
    std::chrono::steady_clock::time_point capturedAt;
    std::shared_ptr<void> owner; // leases an owned GPU copy, never a returned WGC surface
    ComPtr<ID3D11ShaderResourceView> view; // optional view of this exact leased texture
};

enum class AudioSourceKind : uint8_t { Game, Microphone };
struct RecordedAudio {
    AudioSourceKind source{};
    std::chrono::steady_clock::time_point capturedAt{};
    std::vector<float> stereo48k;
    // WASAPI streams anchor once to QPC. Integer output indexes prevent gaps
    // from independently rounding every packet's arrival/device timestamp.
    bool continuousClock{};
    uint64_t firstFrame48k{};
    std::chrono::steady_clock::time_point clockAnchor{};
};
class AudioCaptureTrace;

struct AudioDeviceInfo { std::wstring id; std::wstring name; };
enum class GameAudioMode : uint8_t { Off, SelectedApplication, WholeSystem };
struct RecordingIngress;
class SceneAudioProvider {
public:
    virtual ~SceneAudioProvider()=default;
    virtual std::vector<std::wstring> stems() const=0;
    virtual std::vector<bool> microphoneStems() const {return std::vector<bool>(stems().size(),false);}
    virtual bool start(std::shared_ptr<RecordingIngress> output,std::wstring& error)=0;
    virtual void origin(std::chrono::steady_clock::time_point value)=0;
    virtual void render(uint64_t first,uint64_t frames,std::vector<int16_t>& mix,std::vector<std::vector<int16_t>>& stems)=0;
    virtual int advanceMilliseconds() const=0;
    virtual std::vector<std::pair<std::wstring,std::wstring>> status()const{return {};}
    virtual void stop()=0;
};
struct RecordingAudioOptions {
    std::shared_ptr<SceneAudioProvider> scene;
    GameAudioMode gameMode{GameAudioMode::SelectedApplication};
    DWORD targetProcessId{};
    std::wstring microphoneDeviceId;
    std::wstring systemAudioDeviceId;
    bool microphoneEnabled{};
    bool gameMuted{};
    bool microphoneMuted{};
    float gameVolume{1.0f};
    float microphoneVolume{1.0f};
    int gameSyncMilliseconds{}; // positive delays this source; negative advances it
    int microphoneSyncMilliseconds{};
};

std::wstring defaultAudioEndpointId(bool microphone);
std::vector<AudioDeviceInfo> enumerateMicrophones(std::wstring& error);
std::vector<AudioDeviceInfo> enumerateAudioDevices(std::wstring& error, bool microphone);
std::wstring audioEndpointName(const std::wstring& id);

#include "audio_meter.h"
struct RecordingIngress {
    static constexpr size_t Capacity = 8;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<RecordedFrame> frames;
    static constexpr size_t AudioCapacity = 96;
    std::deque<RecordedAudio> audio;
    std::wstring audioError;
    std::array<std::wstring, 2> audioSourceStatus;
    // Strings/endpoint controls protected by mutex. Counters describe raw input,
    // before ReDot mute/gain, so silence can be localized to capture vs mixing.
    std::array<std::wstring, 2> audioEndpointIds, audioEndpointNames;
    std::array<bool, 2> audioSourceOpen{}, audioEndpointMuted{};
    std::array<float, 2> audioEndpointVolume{1.0f, 1.0f};
    std::array<std::atomic_uint64_t, 2> audioPackets{}, audioInputFrames{}, audioSilentFrames{}, audioNonzeroFrames{}, audioLastPacketMilliseconds{};
    std::array<std::atomic_uint64_t,2> audioDiscontinuities{}, audioMissingInputFrames{}, audioDuplicateInputFrames{}, audioTimestampJumps{}, audioSourceClipping{};
    std::atomic_uint64_t audioTimelineOverwrites{}, audioTimelineMissing{}, audioMixClipped{};
    std::shared_ptr<AudioCaptureTrace> audioTrace;
    VideoConfig video;
    std::atomic_bool composedVideo{false}; // Completed GPU scene frames have one bounded render period of delivery latency.
    std::atomic_bool liveGameMute{false}, liveMicMute{false};
    bool stopping{};
    std::chrono::steady_clock::time_point stopAt{};
    std::atomic_uint64_t queueDrops{0};
    std::atomic_uint64_t repeatedFrames{0};
    std::atomic_uint64_t replacedFrames{0};
    std::atomic_uint64_t encodedFrames{0};
    std::atomic_uint64_t audioQueueDrops{0};
    std::atomic_uint64_t lateAudioFrames{0};
    std::atomic_uint64_t gameLevel{0};
    std::atomic_uint64_t microphoneLevel{0};
    std::array<AudioMeter,2> rawMeters;
    std::atomic_uint64_t timestampFallbacks{0};
    std::atomic_uint64_t sourceFrames{0};
    std::atomic_uint64_t startupSnapshots{0}; // cached owned image, not fresh WGC availability
    std::atomic_uint64_t availableFrames{0}, gpuPoolDrops{0};
    std::chrono::steady_clock::time_point firstAvailableAt{}, lastAvailableAt{};
    std::atomic_uint64_t maxAvailableGapMicros{0};
    std::chrono::steady_clock::time_point firstSourceAt{};
    std::chrono::steady_clock::time_point lastSourceAt{};
    std::atomic_uint64_t maxSourceGapMicros{0};
    std::atomic_uint64_t totalEncodeMicros{0};
#ifdef REDOT_TEST_FIXTURE
    // Fixed 50-us buckets; verification only, absent from production builds.
    std::array<std::atomic_uint64_t,401> verificationEncodeHistogram{};
#endif
    std::atomic_uint64_t encoderViewsCreated{0}, encoderViewsReused{0};
    std::atomic_uint64_t maxEncodeMicros{0};
    std::atomic_uint64_t slowEncodeFrames{0};
    std::atomic_uint64_t videoQueueHighWater{0}, audioQueueHighWater{0};
    std::atomic_uint64_t maxCaptureDeliveryMicros{0}, captureTimestampFallbacks{0};
    std::atomic_int64_t gameDriftPpm{0}, microphoneDriftPpm{0};
    std::atomic_uint64_t audioReconnects{0}, audioFailures{0};
    std::shared_ptr<ReplayBuffer> replayBuffer;

    void push(RecordedFrame frame);
    void noteAvailableFrame(std::chrono::steady_clock::time_point timestamp);
    void push(RecordedAudio packet);
    void requestStop();
};

inline bool seedRetainedCaptureImage(const std::shared_ptr<RecordingIngress>& ingress,
    const ComPtr<ID3D11Texture2D>& image, const std::shared_ptr<void>& owner) {
    if(!ingress || !image || !owner)return false;
    ingress->startupSnapshots.fetch_add(1);
    ingress->push({image,std::chrono::steady_clock::now(),owner});
    return true;
}

class RecordingPipeline {
public:
    ~RecordingPipeline();
    bool start(HWND notifyWindow, std::shared_ptr<GpuResources> gpu,
        const std::wstring& outputFolder, RecordingQuality quality,
        const RecordingAudioOptions& audio, std::wstring& error,
        bool replayOnly = false, std::shared_ptr<ReplayBuffer> replay = {}, VideoConfig video = {});
    std::shared_ptr<RecordingIngress> ingress() const;
    void requestStop();
    void join();
    // Terminal-event fallback when the owned HWND queue rejects PostMessage.
    std::unique_ptr<RecordingEvent> takeFailedCompletion();
    bool busy() const noexcept;
    bool recording() const noexcept;
    uint64_t elapsedMilliseconds() const noexcept;

private:
    void run(HWND notifyWindow, std::shared_ptr<GpuResources> gpu,
        std::shared_ptr<RecordingIngress> ingress, std::wstring outputFolder,
        RecordingQuality quality, RecordingAudioOptions audio, bool replayOnly, VideoConfig video) noexcept;
    void post(HWND window, RecordingEvent* event) noexcept;

    std::thread worker_;
    std::shared_ptr<RecordingIngress> ingress_;
    std::atomic_bool busy_{false};
    // Keep a notified session unavailable until its owner consumes Finished
    // and joins it. Otherwise an old event can join a newly started worker.
    std::atomic_bool completionPending_{false};
    std::mutex completionMutex_;
    std::unique_ptr<RecordingEvent> failedCompletion_;
    std::atomic_bool recording_{false};
    std::atomic_uint64_t elapsedMilliseconds_{0};
};
