#include "audio_capture.h"
#include "timeline.h"
#include "audio_resampler.h"
#include "audio_pcm.h"
#include "audio_capture_trace.h"
#include <avrt.h>
#include "logging.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <ksmedia.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <endpointvolume.h>

namespace {
using Microsoft::WRL::ComPtr;

void audioStatus(const std::shared_ptr<RecordingIngress>& ingress, AudioSourceKind source, const std::wstring& text) {
    std::lock_guard lock(ingress->mutex);
    ingress->audioSourceStatus[source == AudioSourceKind::Game ? 0 : 1] = text;
    ingress->audioError.clear();
    for (const auto& status : ingress->audioSourceStatus) if (!status.empty()) {
        if (!ingress->audioError.empty()) ingress->audioError += L"; ";
        ingress->audioError += status;
    }
    ingress->wake.notify_one();
    logLine(L"Audio status: %s", text.c_str());
}

class ActivationCompletion final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    explicit ActivationCompletion(HANDLE event) { DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &event_, 0, FALSE, DUPLICATE_SAME_ACCESS); }
    ~ActivationCompletion() { if (event_) CloseHandle(event_); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *result = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (iid == __uuidof(IAgileObject)) *result = static_cast<IAgileObject*>(this);
        else { *result = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activation = E_FAIL;
        ComPtr<IUnknown> object;
        operation->GetActivateResult(&activation, &object);
        { std::lock_guard lock(mutex_); result_ = activation; if (SUCCEEDED(activation)) object.As(&client_); }
        SetEvent(event_); return S_OK;
    }
    HRESULT result() const { std::lock_guard lock(mutex_); return result_; }
    ComPtr<IAudioClient> client() const { std::lock_guard lock(mutex_); return client_; }
private:
    std::atomic_ulong refs_{1};
    HANDLE event_{};
    mutable std::mutex mutex_;
    HRESULT result_{E_PENDING};
    ComPtr<IAudioClient> client_;
};

std::wstring hrText(HRESULT hr) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"HRESULT 0x%08X", static_cast<unsigned>(hr));
    return buffer;
}

std::wstring micFailure(HRESULT hr, const std::wstring& detail) {
    auto text = detail + L" (" + hrText(hr) + L").";
    if (hr == E_ACCESSDENIED) text += L" Windows microphone permission was denied: Settings > Privacy & security > Microphone; enable Microphone access and Let desktop apps access your microphone (and ReDot if packaged).";
    else if (hr == AUDCLNT_E_DEVICE_IN_USE) text += L" Another app has exclusive use of this input. Close its exclusive stream and retry.";
    else text += L" Check the selected input connection and Windows Sound settings. No other microphone was substituted.";
    return text;
}

ComPtr<IAudioClient> activateProcessLoopback(DWORD pid, std::wstring& error) {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) { error = L"Could not create the application-audio activation event."; return {}; }
    auto* handler = new ActivationCompletion(ready);
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
        &activation, handler, &operation);
    if (SUCCEEDED(hr) && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0) hr = handler->result();
    else if (SUCCEEDED(hr)) { error = L"Timed out while opening game application audio."; hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    ComPtr<IAudioClient> client = SUCCEEDED(hr) ? handler->client() : ComPtr<IAudioClient>{};
    handler->Release(); CloseHandle(ready);
    if (FAILED(hr) || !client) {
        if (error.empty()) error = L"Could not open game application audio (" + hrText(hr) + L"). Windows 10 build 20348 or newer is required.";
        return {};
    }
    return client;
}

bool getDeviceClient(AudioSourceKind source, GameAudioMode mode, DWORD pid, const std::wstring& id,
    ComPtr<IAudioClient>& client, std::wstring& error) {
    if (source == AudioSourceKind::Game && mode == GameAudioMode::SelectedApplication) {
        client = activateProcessLoopback(pid, error);
        return !!client;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { error = L"Could not initialize Windows audio devices (" + hrText(hr) + L")."; return false; }
    ComPtr<IMMDevice> device;
    if (!id.empty()) hr = enumerator->GetDevice(id.c_str(), &device);
    else if (source == AudioSourceKind::Game) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    else if (id.empty()) hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    else hr = enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) { error = source == AudioSourceKind::Game
        ? L"Could not open the selected system output device for whole-system loopback (" + hrText(hr) + L")."
        : micFailure(hr, L"The selected microphone endpoint is unavailable"); return false; }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr)) { error = source == AudioSourceKind::Microphone ? micFailure(hr, L"Could not open the selected microphone") : L"Could not open the audio endpoint (" + hrText(hr) + L")."; return false; }
    return true;
}

std::chrono::steady_clock::time_point fromQpc100ns(UINT64 qpc) {
    return qpcTime(static_cast<int64_t>(qpc));
}
}

std::wstring defaultAudioEndpointId(bool microphone) {
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> endpoint;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(microphone ? eCapture : eRender, eConsole, &endpoint);
    LPWSTR id{};
    if (SUCCEEDED(hr)) hr = endpoint->GetId(&id);
    if (FAILED(hr)) return {};
    std::wstring result = id; CoTaskMemFree(id); return result;
}

std::wstring audioEndpointName(const std::wstring& id) {
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> endpoint; ComPtr<IPropertyStore> properties;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) hr = enumerator->GetDevice(id.c_str(), &endpoint);
    if (SUCCEEDED(hr)) hr = endpoint->OpenPropertyStore(STGM_READ, &properties);
    PROPVARIANT value; PropVariantInit(&value); std::wstring name;
    if (SUCCEEDED(hr) && SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) name = value.pwszVal;
    PropVariantClear(&value); return name.empty() ? L"Selected audio endpoint (name unavailable)" : name;
}

std::vector<AudioDeviceInfo> enumerateAudioDevices(std::wstring& error, bool microphone) {
    std::vector<AudioDeviceInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { error = L"Could not enumerate microphone devices (" + hrText(hr) + L")."; return result; }
    ComPtr<IMMDeviceCollection> devices;
    hr = enumerator->EnumAudioEndpoints(microphone ? eCapture : eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(hr)) { error = L"Could not list microphone devices (" + hrText(hr) + L")."; return result; }
    UINT count = 0; devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        ComPtr<IPropertyStore> properties;
        std::wstring name = L"Microphone";
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value; PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) name = value.pwszVal;
            PropVariantClear(&value);
        }
        result.push_back({id, std::move(name)}); CoTaskMemFree(id);
    }
    return result;
}

AudioCapture::AudioCapture(AudioSourceKind source, std::shared_ptr<RecordingIngress> ingress,
    DWORD processId, GameAudioMode gameMode, std::wstring deviceId, bool meterOnly)
    : source_(source), ingress_(std::move(ingress)), processId_(processId), gameMode_(gameMode), deviceId_(std::move(deviceId)), meterOnly_(meterOnly) {}
AudioCapture::~AudioCapture() { stop(); }
bool AudioCapture::start(std::wstring& error) {
    try { thread_ = std::thread(&AudioCapture::run, this); }
    catch (...) { error = L"Could not start an audio capture thread."; return false; }
    std::unique_lock lock(startupMutex_);
    if (!startupWake_.wait_for(lock, std::chrono::seconds(12), [this] { return startupDone_; })) {
        error = L"Timed out while opening the requested audio source.";
        logLine(L"Audio capture startup timed out after 12 seconds.");
        stop_.store(true);
        lock.unlock();
        if (thread_.joinable()) thread_.join();
        return false;
    }
    error = startupError_;
    lock.unlock();
    if (!error.empty() && thread_.joinable()) thread_.join();
    return error.empty();
}
void AudioCapture::stop() noexcept { stop_.store(true); if (thread_.joinable()) thread_.join(); }
void AudioCapture::signalStartup(std::wstring error) {
    { std::lock_guard lock(startupMutex_); if (startupDone_) return; startupDone_ = true; startupError_ = std::move(error); }
    startupWake_.notify_all();
}

void AudioCapture::run() noexcept {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) { signalStartup(L"Could not initialize the audio worker COM apartment (" + hrText(com) + L")."); return; }
    struct Scheduling {
        DWORD index{}; HANDLE handle=AvSetMmThreadCharacteristicsW(L"Audio", &index);
        ~Scheduling(){if(handle)AvRevertMmThreadCharacteristics(handle);}
    } scheduling;
    if(!scheduling.handle)logLine(L"Audio MMCSS registration unavailable: error %lu.", GetLastError());
    // Pin a default endpoint once. Retries must never resolve a different default.
    if (deviceId_.empty() && !(source_ == AudioSourceKind::Game && gameMode_ == GameAudioMode::SelectedApplication)) {
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> endpoint;
        HRESULT resolve = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(resolve)) resolve = enumerator->GetDefaultAudioEndpoint(source_ == AudioSourceKind::Microphone ? eCapture : eRender, eConsole, &endpoint);
        LPWSTR id = nullptr;
        if (SUCCEEDED(resolve)) resolve = endpoint->GetId(&id);
        if (FAILED(resolve)) { signalStartup(L"The selected default audio endpoint is unavailable: " + hrText(resolve)); if (SUCCEEDED(com)) CoUninitialize(); return; }
        deviceId_ = id; CoTaskMemFree(id);
        logLine(L"Pinned audio endpoint for this session: %s.", deviceId_.c_str());
    }
    for (unsigned recovery = 0; recovery < 4 && !stop_.load(); ++recovery) {
    const size_t sourceIndex = source_ == AudioSourceKind::Game ? 0 : 1;
    std::wstring failure;
    bool sourceWasStarted = false;
    try {
        ComPtr<IAudioClient> client;
        struct SessionCleanup {
            ComPtr<IAudioClient> client; WAVEFORMATEX* format{}; HANDLE event{};
            ~SessionCleanup() { if (client) client->Stop(); if (event) CloseHandle(event); if (format) CoTaskMemFree(format); }
        } cleanup;
        if (!getDeviceClient(source_, gameMode_, processId_, deviceId_, client, failure)) throw std::runtime_error("audio endpoint activation failed");
        cleanup.client = client;
        const bool applicationLoopback = source_ == AudioSourceKind::Game && gameMode_ == GameAudioMode::SelectedApplication;
        ComPtr<IAudioEndpointVolume> endpointVolume;
        if (!applicationLoopback) {
            ComPtr<IMMDeviceEnumerator> endpoints; ComPtr<IMMDevice> endpoint;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&endpoints))) &&
                SUCCEEDED(endpoints->GetDevice(deviceId_.c_str(), &endpoint)))
                endpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume);
        }
        {
            std::lock_guard lock(ingress_->mutex);
            ingress_->audioEndpointIds[sourceIndex] = applicationLoopback ? L"PID " + std::to_wstring(processId_) : deviceId_;
            ingress_->audioEndpointNames[sourceIndex] = applicationLoopback ? L"Selected app process-tree loopback" : audioEndpointName(deviceId_);
        }
        WAVEFORMATEX applicationFormat{};
        WAVEFORMATEX* format = nullptr;
        HRESULT hr = S_OK;
        if (applicationLoopback) {
            // Process-loopback clients do not expose GetMixFormat (E_NOTIMPL).
            // Use the 44.1 kHz stereo PCM format used by Microsoft's sample;
            // WASAPI converts it to the render mix before our 48 kHz resampler.
            applicationFormat.wFormatTag = WAVE_FORMAT_PCM;
            applicationFormat.nChannels = 2;
            applicationFormat.nSamplesPerSec = 44100;
            applicationFormat.wBitsPerSample = 16;
            applicationFormat.nBlockAlign = 4;
            applicationFormat.nAvgBytesPerSec = 176400;
            format = &applicationFormat;
        } else {
            hr = client->GetMixFormat(&format); cleanup.format = format;
            if (FAILED(hr) || !format) { failure = source_ == AudioSourceKind::Microphone ? micFailure(hr, L"Could not query the selected microphone format") : L"Could not query the audio mix format (" + hrText(hr) + L")."; throw std::runtime_error("audio mix format unavailable"); }
        }
        const bool loopback = source_ == AudioSourceKind::Game;
        const bool extensible = format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        const bool floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (extensible && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const bool pcm = format->wFormatTag == WAVE_FORMAT_PCM || (extensible && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM);
        if (!format->nChannels || !format->nSamplesPerSec || format->nBlockAlign < format->nChannels * ((format->wBitsPerSample + 7) / 8) ||
            (!floating && !pcm) || (floating && format->wBitsPerSample != 32 && format->wBitsPerSample != 64) ||
            (pcm && format->wBitsPerSample != 8 && format->wBitsPerSample != 16 && format->wBitsPerSample != 24 && format->wBitsPerSample != 32)) {
            failure = L"Selected audio endpoint returned an unsupported PCM format; capture was not replaced with silent samples.";
            throw std::runtime_error("unsupported audio input format");
        }
        logLine(L"Native PCM layout: source=%zu tag=%u rate=%lu channels=%u bits=%u align=%u mask=0x%08lx.",sourceIndex,format->wFormatTag,format->nSamplesPerSec,format->nChannels,format->wBitsPerSample,format->nBlockAlign,extensible?reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask:0UL);
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); cleanup.event = event;
        if (!event) { failure = L"Could not create the audio event (" + hrText(HRESULT_FROM_WIN32(GetLastError())) + L")."; throw std::runtime_error("audio event unavailable"); }
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0) |
            (applicationLoopback ? (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY) : 0);
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, applicationLoopback ? 0 : 1'000'000, 0, format, nullptr);
        if (SUCCEEDED(hr)) hr = client->SetEventHandle(event);
        ComPtr<IAudioCaptureClient> capture;
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) { failure = source_ == AudioSourceKind::Microphone ? micFailure(hr, L"Could not start the selected microphone") : L"Could not start audio capture (" + hrText(hr) + L")."; throw std::runtime_error("audio stream initialization failed"); }
        sourceWasStarted = true;
        { std::lock_guard lock(ingress_->mutex); ingress_->audioSourceOpen[sourceIndex] = true; }
        signalStartup();
        if (recovery) audioStatus(ingress_, source_, (source_ == AudioSourceKind::Game ? L"Game audio" : L"Microphone") + std::wstring(L" restored on the same selected source."));
        logLine(L"Audio capture started: %s, endpoint=%s, name=%s, %u Hz, %u channels, %u-bit, meterOnly=%d.", source_ == AudioSourceKind::Game ? L"game/system loopback" : L"microphone", ingress_->audioEndpointIds[sourceIndex].c_str(), ingress_->audioEndpointNames[sourceIndex].c_str(), format->nSamplesPerSec, format->nChannels, format->wBitsPerSample, meterOnly_);
        UINT64 lastDevicePos = 0, lastQpcPos = 0;
        double correctedRate = format->nSamplesPerSec;
        AudioResampler resampler;
        bool anchored=false, havePosition=false;
        UINT64 expectedPosition=0;
        std::chrono::steady_clock::time_point anchor{};
        auto publish = [&](std::vector<float> stereo, uint64_t first) {
            if(stereo.empty())return;
            double square=0;float peak=0;uint64_t nonzero=0;
            for(size_t n=0;n<stereo.size();n+=2){
                const float l=stereo[n],r=stereo[n+1];
                square+=static_cast<double>(l)*l+static_cast<double>(r)*r;
                peak=std::max({peak,std::abs(l),std::abs(r)});
                if(std::abs(l)>0.00003f || std::abs(r)>0.00003f)++nonzero;
            }
            const auto count=stereo.size()/2;
            ingress_->audioNonzeroFrames[sourceIndex].fetch_add(nonzero);
            ingress_->rawMeters[sourceIndex].publish(square,peak,count,GetTickCount64());
            (source_==AudioSourceKind::Game?ingress_->gameLevel:ingress_->microphoneLevel).store(
                static_cast<uint64_t>(std::min(1000.0,std::sqrt(square/stereo.size())*1000)));
            if(!meterOnly_){
                RecordedAudio output;output.source=source_;output.clockAnchor=anchor;
                output.continuousClock=true;output.firstFrame48k=first;
                output.capturedAt=anchor+std::chrono::nanoseconds(first*1'000'000'000ULL/48000);
                output.stereo48k=std::move(stereo);ingress_->push(std::move(output));
            }
        };
        const auto sourceStarted = std::chrono::steady_clock::now();
        auto nextEndpointCheck = sourceStarted;
        while (!stop_.load()) {
            if (endpointVolume && std::chrono::steady_clock::now() >= nextEndpointCheck) {
                BOOL muted{}; float volume{1}; endpointVolume->GetMute(&muted); endpointVolume->GetMasterVolumeLevelScalar(&volume);
                { std::lock_guard lock(ingress_->mutex); ingress_->audioEndpointMuted[sourceIndex] = !!muted; ingress_->audioEndpointVolume[sourceIndex] = volume; }
                nextEndpointCheck = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            const auto faultAt = diagnosticSetting(L"PANDACLIP_TEST_AUDIO_FAIL_SECONDS");
            if (recovery == 0 && faultAt && std::chrono::steady_clock::now() - sourceStarted >= std::chrono::seconds(faultAt)) { failure = L"Controlled audio disconnect (test hook)."; break; }
            const DWORD waited = WaitForSingleObject(event, 100);
            if (waited == WAIT_FAILED) { failure = L"Audio event wait failed."; break; }
            // Polling an idle endpoint also observes invalidation if no event arrives.
            UINT32 packet = 0;
            hr = capture->GetNextPacketSize(&packet);
            while (SUCCEEDED(hr) && packet) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD bufferFlags = 0; UINT64 devicePos = 0, qpcPos = 0;
                hr = capture->GetBuffer(&data, &frames, &bufferFlags, &devicePos, &qpcPos);
                if (FAILED(hr)) break;
                ingress_->audioPackets[sourceIndex].fetch_add(1);
                ingress_->audioInputFrames[sourceIndex].fetch_add(frames);
                ingress_->audioLastPacketMilliseconds[sourceIndex].store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
                const bool silent=(bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT)!=0;
                // Own the native packet and return the WASAPI buffer on this
                // thread before conversion, diagnostic delivery or queue work.
                std::vector<char> owned(static_cast<size_t>(frames)*format->nBlockAlign,
                    pcm && format->wBitsPerSample==8?std::bit_cast<char>(uint8_t(128)):0);
                if(!silent && data)CopyMemory(owned.data(),data,owned.size());
                hr=capture->ReleaseBuffer(frames);
                if(FAILED(hr))break;
                if(!silent && !data && frames){hr=E_UNEXPECTED;break;}
                if(silent)ingress_->audioSilentFrames[sourceIndex].fetch_add(frames);
                const bool timestampValid=qpcPos && !(bufferFlags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
                const auto at=timestampValid?fromQpc100ns(qpcPos):std::chrono::steady_clock::now()-
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(frames/correctedRate));
                if(!timestampValid)ingress_->timestampFallbacks.fetch_add(1);
                if(!meterOnly_ && ingress_->audioTrace)ingress_->audioTrace->raw(static_cast<unsigned>(sourceIndex),at,devicePos,bufferFlags,format,owned);
                const bool discontinuity=(bufferFlags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)!=0;
                if(discontinuity){
                    const auto n=++ingress_->audioDiscontinuities[sourceIndex];
                    if(n<=10)logLine(L"WASAPI source %zu discontinuity: packet first=%llu expected=%llu frames=%u flags=%lu.",sourceIndex,devicePos,expectedPosition,frames,bufferFlags);
                }
                if(!anchored){anchor=at;anchored=true;}
                size_t skip=0;
                // Some virtual loopback clients give a constant position zero.
                // Do not reinterpret that as repeated audio. QPC remains the
                // initial clock anchor and conversion advances by sample count.
                const bool positionValid=havePosition && (devicePos || (discontinuity && expectedPosition));
                if(positionValid && devicePos!=expectedPosition){
                    if(devicePos>expectedPosition){
                        const auto missing=devicePos-expectedPosition;
                        ingress_->audioMissingInputFrames[sourceIndex].fetch_add(missing);
                        if(missing<=format->nSamplesPerSec/2){
                            // Preserve a measured device loss as silence, rather
                            // than squeezing subsequent samples over the gap.
                            const auto first=resampler.outputFrames();
                            publish(resampler.process(std::vector<float>(static_cast<size_t>(missing)*2,0),correctedRate),first);
                        }else{
                            resampler=AudioResampler{};anchor=at;
                            ingress_->audioTimestampJumps[sourceIndex].fetch_add(1);
                        }
                    }else{
                        const auto duplicate=expectedPosition-devicePos;
                        if(duplicate<=frames){skip=static_cast<size_t>(duplicate);ingress_->audioDuplicateInputFrames[sourceIndex].fetch_add(duplicate);}
                        else{resampler=AudioResampler{};anchor=at;ingress_->audioTimestampJumps[sourceIndex].fetch_add(1);}
                    }
                    lastQpcPos=0; // Never measure device rate across a gap/reset.
                }
                expectedPosition=devicePos+frames;havePosition=true;
                if(timestampValid && lastQpcPos && qpcPos>lastQpcPos && devicePos>lastDevicePos && qpcPos-lastQpcPos>=10'000'000){
                    const double measured=static_cast<double>(devicePos-lastDevicePos)*10'000'000.0/static_cast<double>(qpcPos-lastQpcPos);
                    const double ppm=std::clamp((measured/format->nSamplesPerSec-1.0)*1'000'000.0,-500.0,500.0);
                    correctedRate=correctedRate*0.8+format->nSamplesPerSec*(1+ppm/1'000'000.0)*0.2;
                    (source_==AudioSourceKind::Game?ingress_->gameDriftPpm:ingress_->microphoneDriftPpm).store(static_cast<int64_t>((correctedRate/format->nSamplesPerSec-1)*1'000'000));
                    lastDevicePos=devicePos;lastQpcPos=qpcPos;
                }
                if(timestampValid && !lastQpcPos){lastDevicePos=devicePos;lastQpcPos=qpcPos;}
                const auto raw=reinterpret_cast<const BYTE*>(owned.data());
                std::vector<float> stereo((frames-skip)*2);
                uint64_t clipped=0;
                for(size_t n=skip;n<frames;++n){
                    const auto pair=stereoPcm(raw,n,format);const auto l=pair[0],r=pair[1];
                    // Keep float headroom for diagnostics; only quantization
                    // at the stem/mix output clips to signed 16-bit range.
                    if(std::abs(l)>1 || std::abs(r)>1)++clipped;
                    stereo[(n-skip)*2]=l;stereo[(n-skip)*2+1]=r;
                }
                ingress_->audioSourceClipping[sourceIndex].fetch_add(clipped);
                const auto first=resampler.outputFrames();publish(resampler.process(stereo,correctedRate),first);
                hr = capture->GetNextPacketSize(&packet);
            }
            if (FAILED(hr)) { failure = L"The audio device disconnected or stopped responding (" + hrText(hr) + L"). Video recording will continue with silence for this source."; break; }
        }
        const auto finalFirst=resampler.outputFrames();
        publish(resampler.drain(),finalFirst);
        client->Stop(); capture.Reset();
    } catch (...) {
        if (failure.empty()) failure = L"Could not initialize the requested audio source.";
    }
    { std::lock_guard lock(ingress_->mutex); ingress_->audioSourceOpen[sourceIndex] = false; }
    logLine(L"Audio continuity: source=%zu discontinuities=%llu missing native=%llu duplicate native=%llu clock resets=%llu over-range input frames=%llu timeline overwritten=%llu missing enabled-source slots=%llu mix clipped=%llu.",sourceIndex,ingress_->audioDiscontinuities[sourceIndex].load(),ingress_->audioMissingInputFrames[sourceIndex].load(),ingress_->audioDuplicateInputFrames[sourceIndex].load(),ingress_->audioTimestampJumps[sourceIndex].load(),ingress_->audioSourceClipping[sourceIndex].load(),ingress_->audioTimelineOverwrites.load(),ingress_->audioTimelineMissing.load(),ingress_->audioMixClipped.load());
    logLine(L"Audio source capture totals: source=%zu, endpoint=%s, packets=%llu, input frames=%llu, WASAPI silent frames=%llu, nonzero resampled frames=%llu, meterOnly=%d.", sourceIndex, deviceId_.c_str(), ingress_->audioPackets[sourceIndex].load(), ingress_->audioInputFrames[sourceIndex].load(), ingress_->audioSilentFrames[sourceIndex].load(), ingress_->audioNonzeroFrames[sourceIndex].load(), meterOnly_);
    (source_ == AudioSourceKind::Game ? ingress_->gameLevel : ingress_->microphoneLevel).store(0);
    if (failure.empty() || stop_.load()) break;
    signalStartup(failure);
    if (!sourceWasStarted && recovery == 0) break;
    ingress_->audioFailures.fetch_add(1);
    const std::wstring label = source_ == AudioSourceKind::Game ? L"Game audio" : L"Microphone";
    audioStatus(ingress_, source_, label + L": " + failure + (recovery < 3 ? L" Retrying the SAME source; silence while unavailable." : L" Recovery exhausted; silence until a new session."));
    logLine(L"Audio recovery attempt %u/4: %s", recovery + 1, failure.c_str());
    if (recovery < 3) {
        const unsigned waits[] = {20, 50, 100};
        for (unsigned tick = 0; tick < waits[recovery] && !stop_.load(); ++tick) Sleep(100);
        if (!stop_.load()) ingress_->audioReconnects.fetch_add(1);
    }
    }
    if (SUCCEEDED(com)) CoUninitialize();
}

MicrophoneMonitor::~MicrophoneMonitor() { requestStop(); if (worker_.joinable()) worker_.join(); }
bool MicrophoneMonitor::start(const std::wstring& endpointId, std::wstring& error) {
    if (busy()) { error = L"The previous microphone check is still stopping."; return false; }
    if (worker_.joinable()) worker_.join();
    if (endpointId.empty()) { error = L"No default input device is available. Choose a named connected microphone in Windows Sound settings."; return false; }
    ingress_ = std::make_shared<RecordingIngress>();
    { std::lock_guard lock(mutex_); error_.clear(); }
    stop_.store(false); busy_.store(true); auto ingress = ingress_;
    try { worker_ = std::thread([this, ingress, endpointId] {
        std::wstring failure;
        try {
            AudioCapture capture(AudioSourceKind::Microphone, ingress, 0, GameAudioMode::Off, endpointId, true);
            if (capture.start(failure)) while (!stop_.load()) Sleep(50);
            capture.stop();
        } catch (...) { failure = L"Microphone check worker failed."; }
        { std::lock_guard lock(mutex_); error_ = std::move(failure); }
        busy_.store(false);
    }); } catch (...) { busy_.store(false); error = L"Could not start the microphone check worker."; return false; }
    return true;
}

std::vector<AudioDeviceInfo> enumerateMicrophones(std::wstring& error) { return enumerateAudioDevices(error, true); }
