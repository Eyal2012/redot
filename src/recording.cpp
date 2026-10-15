#include "clip_track_names.h"
#include "recording.h"
#include "timeline.h"
#include "logging.h"
#include "audio_capture.h"
#include "audio_timeline.h"
#include "audio_capture_trace.h"
#include "mkv_writer.h"
#include "../third_party/NVIDIA/VideoCodecSDK/include/nvEncodeAPI.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mmreg.h>
#include <propvarutil.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>
#include <optional>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {



constexpr int64_t kHnsPerSecond = 10'000'000;
std::wstring win32Error(DWORD error) {
    wchar_t* text = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = count && text ? std::wstring(text, count) : L"Windows error";
    if (text) LocalFree(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

std::wstring nvStatusText(NVENCSTATUS status) {
    switch (status) {
    case NV_ENC_ERR_NO_ENCODE_DEVICE: return L"No NVIDIA NVENC device was found.";
    case NV_ENC_ERR_UNSUPPORTED_DEVICE: return L"The NVIDIA driver or GPU does not support this NVENC configuration.";
    case NV_ENC_ERR_INVALID_VERSION: return L"The NVIDIA driver is too old for the included NVENC API header.";
    case NV_ENC_ERR_OUT_OF_MEMORY: return L"NVIDIA NVENC ran out of memory.";
    case NV_ENC_ERR_ENCODER_NOT_INITIALIZED: return L"NVIDIA NVENC is not initialized.";
    case NV_ENC_ERR_RESOURCE_NOT_REGISTERED: return L"The GPU input resource was not registered with NVENC.";
    case NV_ENC_ERR_RESOURCE_NOT_MAPPED: return L"The GPU input resource was not mapped with NVENC.";
    case NV_ENC_ERR_INVALID_PARAM: return L"NVIDIA NVENC rejected an encoder parameter.";
    default: return L"NVIDIA NVENC API error " + std::to_wstring(static_cast<int>(status)) + L".";
    }
}

std::wstring makeMessage(NVENCSTATUS status, const wchar_t* operation) {
    return std::wstring(operation) + L": " + nvStatusText(status);
}

struct CaptureClock {
    std::chrono::steady_clock::time_point origin;
    std::chrono::steady_clock::time_point nextDeadline;
    uint64_t frameIndex{};
};

class ReplayAacEncoder {
public:
    ~ReplayAacEncoder() { reset(); }
    bool open(std::wstring& error) {
        MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Audio, MFAudioFormat_AAC};
        IMFActivate** activates = nullptr; UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT,
            nullptr, &outputInfo, &activates, &count);
        if (FAILED(hr) || !count) { error = L"Windows Media Foundation did not expose an AAC encoder for replay packets."; return false; }
        hr = activates[0]->ActivateObject(IID_PPV_ARGS(&transform_));
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);
        ComPtr<IMFMediaType> input, output;
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&input);
        if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
        if (SUCCEEDED(hr)) hr = transform_->SetInputType(0, input.Get(), 0);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&output);
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AVG_BITRATE, 192000);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
        if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        if (SUCCEEDED(hr)) hr = transform_->SetOutputType(0, output.Get(), 0);
        if (SUCCEEDED(hr)) hr = transform_->GetOutputCurrentType(0, &outputType_);
        if (SUCCEEDED(hr)) hr = transform_->GetOutputStreamInfo(0, &outputInfo_);
        if (SUCCEEDED(hr)) {
            UINT32 size = 0;
            if (SUCCEEDED(outputType_->GetBlobSize(MF_MT_USER_DATA, &size)) && size) {
                userData_.resize(size);
                hr = outputType_->GetBlob(MF_MT_USER_DATA, userData_.data(), size, &size);
            }
        }
        if (SUCCEEDED(hr)) hr = transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (SUCCEEDED(hr)) hr = transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        if (FAILED(hr)) { error = L"Could not initialize the Windows AAC encoder MFT (HRESULT 0x" + hex(hr) + L")."; reset(); return false; }
        return true;
    }
    const std::vector<uint8_t>& userData() const { return userData_; }
    IMFMediaType* outputType() const { return outputType_.Get(); }
    void shutdown() noexcept { reset(); }
    bool encode(const std::vector<int16_t>& pcm, uint64_t firstFrame, ReplayTrack track,
        std::vector<ReplayPacket>& output, std::wstring& error) {
        if (!transform_ || pcm.empty()) return true;
        const DWORD bytes = static_cast<DWORD>(pcm.size() * sizeof(int16_t));
        ComPtr<IMFMediaBuffer> buffer; HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
        BYTE* data = nullptr; DWORD max = 0;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&data, &max, nullptr);
        if (SUCCEEDED(hr)) { if (max < bytes) hr = E_OUTOFMEMORY; else CopyMemory(data, pcm.data(), bytes); buffer->Unlock(); }
        if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(bytes);
        ComPtr<IMFSample> sample;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        const uint64_t frames = pcm.size() / 2;
        const int64_t pts = static_cast<int64_t>(firstFrame * 10'000'000ULL / 48000);
        const int64_t duration = static_cast<int64_t>(frames * 10'000'000ULL / 48000);
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(pts);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
        if (SUCCEEDED(hr)) hr = transform_->ProcessInput(0, sample.Get(), 0);
        if (FAILED(hr)) { error = L"Could not submit PCM to the replay AAC encoder (HRESULT 0x" + hex(hr) + L")."; return false; }
        return pull(track, output, error);
    }
    bool drain(ReplayTrack track, std::vector<ReplayPacket>& output, std::wstring& error) {
        if (!transform_) return true;
        HRESULT hr = transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        if (FAILED(hr)) { error = L"Could not drain the replay AAC encoder (HRESULT 0x" + hex(hr) + L")."; return false; }
        return pull(track, output, error);
    }
private:
    static std::wstring hex(HRESULT hr) { wchar_t value[16]{}; swprintf_s(value, L"%08X", static_cast<unsigned>(hr)); return value; }
    bool pull(ReplayTrack track, std::vector<ReplayPacket>& packets, std::wstring& error) {
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            MFT_OUTPUT_DATA_BUFFER out{}; out.dwStreamID = 0;
            ComPtr<IMFSample> allocated;
            if (!(outputInfo_.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> buffer;
                HRESULT hr = MFCreateMemoryBuffer(std::max<DWORD>(outputInfo_.cbSize, 65536), &buffer);
                if (SUCCEEDED(hr)) hr = MFCreateSample(&allocated);
                if (SUCCEEDED(hr)) hr = allocated->AddBuffer(buffer.Get());
                if (FAILED(hr)) { error = L"Could not allocate AAC output memory (HRESULT 0x" + hex(hr) + L")."; return false; }
                out.pSample = allocated.Get();
            }
            DWORD status = 0;
            const HRESULT hr = transform_->ProcessOutput(0, 1, &out, &status);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) { if (out.pEvents) out.pEvents->Release(); return true; }
            if (FAILED(hr)) { if (out.pEvents) out.pEvents->Release(); error = L"The AAC MFT failed to produce a replay packet (HRESULT 0x" + hex(hr) + L")."; return false; }
            ComPtr<IMFSample> sample;
            if (allocated && out.pSample == allocated.Get()) sample = allocated;
            else sample.Attach(out.pSample);
            if (out.pEvents) out.pEvents->Release();
            if (!sample) continue;
            LONGLONG pts = 0, duration = 0;
            if (FAILED(sample->GetSampleTime(&pts))) pts = fallbackPts_;
            if (FAILED(sample->GetSampleDuration(&duration)) || duration <= 0) duration = 10'000'000LL * 1024 / 48000;
            fallbackPts_ = std::max(fallbackPts_, pts + duration);
            ComPtr<IMFMediaBuffer> contiguous; HRESULT copyHr = sample->ConvertToContiguousBuffer(&contiguous);
            BYTE* bytes = nullptr; DWORD maximum = 0, length = 0;
            if (SUCCEEDED(copyHr)) copyHr = contiguous->Lock(&bytes, &maximum, &length);
            if (FAILED(copyHr) || !length) { if (SUCCEEDED(copyHr)) contiguous->Unlock(); error = L"The AAC MFT returned an empty packet."; return false; }
            ReplayPacket packet; packet.track = track; packet.pts = packet.dts = pts; packet.duration = duration;
            packet.bytes.assign(bytes, bytes + length); contiguous->Unlock(); packets.push_back(std::move(packet));
        }
        error = L"The AAC MFT produced too many packets for one input block."; return false;
    }
    void reset() { transform_.Reset(); outputType_.Reset(); outputInfo_ = {}; }
    ComPtr<IMFTransform> transform_;
    ComPtr<IMFMediaType> outputType_;
    MFT_OUTPUT_STREAM_INFO outputInfo_{};
    std::vector<uint8_t> userData_;
    int64_t fallbackPts_{};
};

class NvencMp4Writer {
public:
    ~NvencMp4Writer() { closeEncoder(); }

    bool open(std::shared_ptr<GpuResources> gpu, const std::wstring& folder,
        RecordingQuality quality, const RecordingAudioOptions& audio, std::wstring& error,
        std::shared_ptr<ReplayBuffer> replay, bool replayOnly, VideoConfig video = {}) {
        video_ = video; kOutputWidth = video.width; kOutputHeight = video.height; kFps = video.fps;
        gpu_ = std::move(gpu);
        replay_ = std::move(replay);
        replayOnly_ = replayOnly;
        if (!gpu_ || gpu_->adapterVendorId != 0x10DE) {
            error = L"NVENC requires the capture device to be on an NVIDIA GPU. Current Direct3D adapter: " +
                (gpu_ ? gpu_->adapterName : L"unavailable") + L". Set Windows Graphics preference for ReDot to High performance NVIDIA GPU.";
            return false;
        }
        HMODULE module = LoadLibraryW(L"nvEncodeAPI64.dll");
        if (!module) module = LoadLibraryW(L"nvEncodeAPI.dll");
        if (!module) {
            error = L"NVIDIA's NVENC driver library (nvEncodeAPI64.dll) is unavailable. Install or update the NVIDIA graphics driver.";
            return false;
        }
        module_ = module;
        using CreateInstanceProc = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
        auto createInstance = reinterpret_cast<CreateInstanceProc>(GetProcAddress(module_, "NvEncodeAPICreateInstance"));
        if (!createInstance) { error = L"The installed NVIDIA driver does not expose the NVENC API."; return false; }
        functions_ = {};
        functions_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        NVENCSTATUS nv = createInstance(&functions_);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"NvEncodeAPICreateInstance failed"); return false; }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openParams{};
        openParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        openParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        openParams.device = gpu_->device.Get();
        openParams.apiVersion = NVENCAPI_VERSION;
        nv = functions_.nvEncOpenEncodeSessionEx(&openParams, &encoder_);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not open an NVENC Direct3D 11 session"); return false; }

        if (!supportsH264AndInput(error) || !createEncoder(quality, error) || !createInputTexture(error) ||
            !readSequenceHeader(error)) return false;
        HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(mf)) { error = L"Windows Media Foundation could not start (HRESULT 0x" + hex(mf) + L")."; return false; }
        mfStarted_ = true;
        const bool audioEnabled = (audio.gameMode != GameAudioMode::Off && (video.trackMix || video.trackGame)) || (audio.microphoneEnabled && (video.trackMix || video.trackMicrophone));
        audioEnabled_ = audioEnabled; mixEnabled_ = audioEnabled && video.trackMix; audioGameEnabled_ = audio.gameMode != GameAudioMode::Off && video.trackGame; audioMicrophoneEnabled_ = audio.microphoneEnabled && video.trackMicrophone;
        if (audioEnabled) {
            if (mixEnabled_ && !mixAac_.open(error)) return false;
            if (audioGameEnabled_ && !gameAac_.open(error)) return false;
            if (audioMicrophoneEnabled_ && !microphoneAac_.open(error)) return false;
        }
        if(audio.scene){
            auto allNames=audio.scene->stems();auto kinds=audio.scene->microphoneStems();for(size_t i=0;i<allNames.size();++i)if(i<kinds.size()&&(kinds[i]?video.trackMicrophone:video.trackGame)){sceneNames_.push_back(allNames[i]);sceneInputIndexes_.push_back(i);}if(allNames.size()>16){error=L"A recording session supports at most 16 distinct scene audio stems.";return false;}
            audioGameEnabled_=audioMicrophoneEnabled_=false;
            mixEnabled_=!allNames.empty()&&video.trackMix;audioEnabled_=mixEnabled_||!sceneNames_.empty();
            if(mixEnabled_&&!mixAac_.open(error))return false;
            for(size_t i=0;i<sceneNames_.size();++i){auto aac=std::make_unique<ReplayAacEncoder>();if(!aac->open(error))return false;sceneAac_.push_back(std::move(aac));sceneStreams_.push_back({});}
        }
        std::vector<ReplayAudioStream> sceneTracks;for(size_t i=0;i<sceneNames_.size();++i)sceneTracks.push_back({ReplayTrack(4+i),sceneNames_[i]});
        if (replay_) replay_->reset(sequenceHeader_, aacConfiguration().userData(), audioEnabled && audio.gameMode != GameAudioMode::Off,
            audioEnabled && audio.microphoneEnabled, video,sceneTracks,bool(audio.scene)&&mixEnabled_);
        if (!replayOnly_ && !createMuxer(folder, audio, error)) return false;
        wchar_t gpuName[256]{};
        wcsncpy_s(gpuName, gpu_->adapterName.c_str(), _TRUNCATE);
        logLine(L"NVENC ready: adapter=%s, API=%u.%u, output=%s, preset=%s.", gpuName,
            NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION, video.description().c_str(),
            quality == RecordingQuality::HighQuality ? L"P6 High Quality" : L"P4 Balanced");
        return true;
    }

    bool encode(const ComPtr<ID3D11Texture2D>& source, uint64_t frameIndex, std::wstring& error, ID3D11ShaderResourceView* view = nullptr) {
        if (!source || (!writer_ && !mkv_ && !replayOnly_)) { error = L"The encoder or recording file writer is not initialized."; return false; }
        if ((writer_ || mkv_) && diagnosticSetting(L"PANDACLIP_TEST_RECORD_FAIL_FRAME") && frameIndex >= diagnosticSetting(L"PANDACLIP_TEST_RECORD_FAIL_FRAME")) {
            error = L"Controlled recording write failure (test hook); incomplete file will not be reported Saved."; return false;
        }
#ifdef REDOT_UI_VERIFICATION
        if(frameIndex==0)logLine(L"Quiet encode: before draw");
#endif
        HRESULT hr = drawResizedFrame(source, error, view);
#ifdef REDOT_UI_VERIFICATION
        if(frameIndex==0)logLine(L"Quiet encode: after draw");
#endif
        if (FAILED(hr)) return false;

        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = registeredInput_;
        NVENCSTATUS nv = functions_.nvEncMapInputResource(encoder_, &map);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not map the GPU frame into NVENC"); return false; }

        NV_ENC_PIC_PARAMS picture{};
        picture.version = NV_ENC_PIC_PARAMS_VER;
        picture.inputBuffer = map.mappedResource;
        picture.outputBitstream = bitstream_.bitstreamBuffer;
        picture.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
        picture.inputWidth = kOutputWidth;
        picture.inputHeight = kOutputHeight;
        picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        picture.inputTimeStamp = frameIndex * kHnsPerSecond / kFps;
        picture.inputDuration = (frameIndex + 1) * kHnsPerSecond / kFps - picture.inputTimeStamp;
        if (frameIndex == 0 || frameIndex % (kFps * 2) == 0) picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
#ifdef REDOT_UI_VERIFICATION
        if(frameIndex==0)logLine(L"Quiet encode: before picture");
#endif
        nv = functions_.nvEncEncodePicture(encoder_, &picture);
#ifdef REDOT_UI_VERIFICATION
        if(frameIndex==0)logLine(L"Quiet encode: after picture");
#endif
        if (nv == NV_ENC_ERR_NEED_MORE_INPUT) {
            functions_.nvEncUnmapInputResource(encoder_, map.mappedResource);
            error = L"NVENC buffered a frame despite the low-delay configuration; cannot safely reuse the input surface.";
            return false;
        }
        if (nv != NV_ENC_SUCCESS) {
            functions_.nvEncUnmapInputResource(encoder_, map.mappedResource);
            error = makeMessage(nv, L"NVENC could not encode a frame");
            return false;
        }

        NV_ENC_LOCK_BITSTREAM lock{};
        lock.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = bitstream_.bitstreamBuffer;
        lock.doNotWait = 0;
        nv = functions_.nvEncLockBitstream(encoder_, &lock);
        if (nv != NV_ENC_SUCCESS) {
            functions_.nvEncUnmapInputResource(encoder_, map.mappedResource);
            error = makeMessage(nv, L"Could not retrieve the encoded H.264 frame");
            return false;
        }
#ifdef REDOT_UI_VERIFICATION
        if(frameIndex==0)logLine(L"Quiet encode: bitstream returned");
#endif
        // LockBitstream has completed this synchronous submission; the input texture is now reusable.
        const bool written = writeSample(static_cast<const BYTE*>(lock.bitstreamBufferPtr),
            lock.bitstreamSizeInBytes, frameIndex, lock.pictureType == NV_ENC_PIC_TYPE_IDR, error);
        const NVENCSTATUS unlock = functions_.nvEncUnlockBitstream(encoder_, bitstream_.bitstreamBuffer);
        const NVENCSTATUS unmap = functions_.nvEncUnmapInputResource(encoder_, map.mappedResource);
        if (unlock != NV_ENC_SUCCESS && written) { error = makeMessage(unlock, L"Could not release the NVENC output buffer"); return false; }
        if (unmap != NV_ENC_SUCCESS && written) { error = makeMessage(unmap, L"Could not release the NVENC input surface"); return false; }
        return written;
    }

    bool finalize(std::wstring& error) {
        if (encoder_ && initialized_) {
            NV_ENC_PIC_PARAMS eos{};
            eos.version = NV_ENC_PIC_PARAMS_VER;
            eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
            const NVENCSTATUS nv = functions_.nvEncEncodePicture(encoder_, &eos);
            if (nv != NV_ENC_SUCCESS && nv != NV_ENC_ERR_NEED_MORE_INPUT) {
                error = makeMessage(nv, L"Could not flush pending NVENC output");
                return false;
            }
        }
        if (mkv_) {
            const bool finished = mkv_->finalize(error);
            mkv_.reset();
            if (!finished) { error = L"Could not finalize the MKV file: " + error; return false; }
            mkvFinalized_ = true;
        }
        if (writer_) {
            const HRESULT hr = writer_->Finalize();
            writer_ = nullptr;
            if (FAILED(hr)) { error = L"Could not finalize the MP4 file (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }
        }
        return true;
    }

    bool writeAudio(uint64_t firstFrame, const std::vector<int16_t>& mixed,
        const std::vector<int16_t>& game, const std::vector<int16_t>& microphone,
        std::wstring& error) {
        if (!audioEnabled_) return true;
        if (diagnosticSetting(L"PANDACLIP_SYNC_TRACE")) {
            const auto& trace = audioGameEnabled_ ? game : microphone;
            for (size_t i = 0; i < trace.size() / 2; ++i) {
                const bool loud = std::abs(static_cast<int>(trace[i * 2])) > 3000;
                const uint64_t at = firstFrame + i;
                if (loud && at > traceLastLoud_ + 7200) logLine(L"Sync trace pre-AAC cue: frame=%llu, pts=%.3f ms.", at, at / 48.0);
                if (loud) traceLastLoud_ = at;
            }
        }
        return (!mixEnabled_ || encodeAudio(mixAac_, ReplayTrack::Mix, mixedStream_, firstFrame, mixed, error)) &&
            (!audioGameEnabled_ || encodeAudio(gameAac_, ReplayTrack::Game, gameStream_, firstFrame, game, error)) &&
            (!audioMicrophoneEnabled_ || encodeAudio(microphoneAac_, ReplayTrack::Microphone, microphoneStream_, firstFrame, microphone, error));
    }

    bool drainAudio(std::wstring& error) {
        for(size_t i=0;i<sceneAac_.size();++i)if(!drainAudioEncoder(*sceneAac_[i],ReplayTrack(4+i),sceneStreams_[i],error))return false;
        return (!mixEnabled_ || drainAudioEncoder(mixAac_, ReplayTrack::Mix, mixedStream_, error)) &&
            (!audioGameEnabled_ || drainAudioEncoder(gameAac_, ReplayTrack::Game, gameStream_, error)) &&
            (!audioMicrophoneEnabled_ || drainAudioEncoder(microphoneAac_, ReplayTrack::Microphone, microphoneStream_, error));
    }
    bool writeSceneAudio(uint64_t first,const std::vector<int16_t>& mix,const std::vector<std::vector<int16_t>>& stems,std::wstring& error){
        if(mixEnabled_&&!encodeAudio(mixAac_,ReplayTrack::Mix,mixedStream_,first,mix,error))return false;
        for(size_t i=0;i<sceneAac_.size();++i){if(sceneInputIndexes_[i]>=stems.size()){error=L"Scene audio stream layout changed during encoding.";return false;}if(!encodeAudio(*sceneAac_[i],ReplayTrack(4+i),sceneStreams_[i],first,stems[sceneInputIndexes_[i]],error))return false;}
        return true;
    }

    bool ownsPartial() const noexcept { return partialOwned_; }
    void discardPartial(std::wstring& error) {
        // A failed initialization can still own an open sink. Release it before
        // removing only our temporary file, never a successfully saved path.
        writer_ = nullptr;
        if (mkv_) { mkv_->abandon(); mkv_.reset(); }
        if (!partialOwned_) return;
        if (video_.container == ContainerFormat::Mkv && !replayOnly_) {
            // Matroska is written incrementally: whatever was captured before the failure is the user's
            // footage. Keep it (never delete); startup recovery validates and repairs it. Only a file that
            // never received a single video packet is an empty shell we created and may remove.
            if (mkvFrames_ == 0) { std::error_code removeError; std::filesystem::remove(partialPath_, removeError); }
            else error += L" The footage recorded before the failure was kept as " + partialPath_ + L" and will be repaired the next time ReDot starts.";
            return;
        }
        std::error_code ec;
        std::filesystem::remove(partialPath_, ec);
        if (ec) {
            const auto warning = L" Incomplete temporary file remains: " + partialPath_ + L" (cleanup error " + std::to_wstring(ec.value()) + L").";
            error += warning; logLine(L"%s", warning.c_str());
        }
    }
    const std::wstring& partialPath() const noexcept { return partialPath_; }
    void saveTrackNames() { std::map<unsigned,std::string> names; if(mixedStream_)names[*mixedStream_+1]="Mix";if(gameStream_)names[*gameStream_+1]="Game";if(microphoneStream_)names[*microphoneStream_+1]="Microphone";for(size_t i=0;i<sceneStreams_.size();++i)if(sceneStreams_[i]){auto& label=sceneNames_[i];int n=WideCharToMultiByte(CP_UTF8,0,label.data(),int(label.size()),nullptr,0,nullptr,nullptr);std::string utf8(n,'\0');WideCharToMultiByte(CP_UTF8,0,label.data(),int(label.size()),utf8.data(),n,nullptr,nullptr);names[*sceneStreams_[i]+1]=utf8;}saveClipTrackNames(finalPath_,names); }
    const std::wstring& finalPath() const noexcept { return finalPath_; }
    uint64_t viewsCreated() const noexcept { return viewsCreated_; }
    uint64_t viewsReused() const noexcept { return viewsReused_; }
    bool mfStarted() const noexcept { return mfStarted_; }
    void closeEncoder() noexcept {
        writer_ = nullptr;
        mkv_.reset(); // abandon(): an unfinished Matroska stream stays recoverable
        if (registeredInput_ && encoder_ && functions_.nvEncUnregisterResource) {
            functions_.nvEncUnregisterResource(encoder_, registeredInput_);
            registeredInput_ = nullptr;
        }
        if (inputTexture_) inputTexture_.Reset();
        if (bitstream_.bitstreamBuffer && encoder_ && functions_.nvEncDestroyBitstreamBuffer) {
            functions_.nvEncDestroyBitstreamBuffer(encoder_, bitstream_.bitstreamBuffer);
            bitstream_.bitstreamBuffer = nullptr;
        }
        if (encoder_ && functions_.nvEncDestroyEncoder) functions_.nvEncDestroyEncoder(encoder_);
        encoder_ = nullptr;
        initialized_ = false;
        mixAac_.shutdown(); gameAac_.shutdown(); microphoneAac_.shutdown();
        for (auto& encoder : sceneAac_) encoder->shutdown();
        sceneAac_.clear(); // Release scene MFTs on their worker before MF/COM shutdown.
        if (module_) FreeLibrary(module_);
        module_ = nullptr;
    }

private:
    static std::wstring hex(HRESULT hr) {
        wchar_t value[16]{}; swprintf_s(value, L"%08X", static_cast<unsigned>(hr)); return value;
    }
    static std::wstring hresultText(HRESULT hr) {
        wchar_t* text = nullptr;
        const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0,
            reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        std::wstring result = count && text ? std::wstring(text, count) : L"Windows Media Foundation error";
        if (text) LocalFree(text);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
        return result;
    }

    bool queryCap(NV_ENC_CAPS cap, int& value, std::wstring& error) {
        NV_ENC_CAPS_PARAM params{};
        params.version = NV_ENC_CAPS_PARAM_VER;
        params.capsToQuery = cap;
        const NVENCSTATUS nv = functions_.nvEncGetEncodeCaps(encoder_, NV_ENC_CODEC_H264_GUID, &params, &value);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not query NVENC hardware capabilities"); return false; }
        return true;
    }

    bool supportsH264AndInput(std::wstring& error) {
        uint32_t count = 0;
        NVENCSTATUS nv = functions_.nvEncGetEncodeGUIDCount(encoder_, &count);
        if (nv != NV_ENC_SUCCESS || count == 0) { error = L"NVIDIA driver reported no hardware encoder codecs."; return false; }
        std::vector<GUID> codecs(count);
        uint32_t actual = 0;
        nv = functions_.nvEncGetEncodeGUIDs(encoder_, codecs.data(), count, &actual);
        if (nv != NV_ENC_SUCCESS || std::none_of(codecs.begin(), codecs.begin() + actual,
            [](const GUID& id) { return IsEqualGUID(id, NV_ENC_CODEC_H264_GUID); })) {
            error = L"The selected NVIDIA GPU/driver does not expose H.264 encoding through NVENC."; return false;
        }
        int maxWidth = 0, maxHeight = 0, rateModes = 0, maxMacroblocksPerSecond = 0;
        if (!queryCap(NV_ENC_CAPS_WIDTH_MAX, maxWidth, error) || !queryCap(NV_ENC_CAPS_HEIGHT_MAX, maxHeight, error) ||
            !queryCap(NV_ENC_CAPS_SUPPORTED_RATECONTROL_MODES, rateModes, error) ||
            !queryCap(NV_ENC_CAPS_MB_PER_SEC_MAX, maxMacroblocksPerSecond, error)) return false;
        const int macroblocksPerFrame = static_cast<int>(((kOutputWidth + 15) / 16) * ((kOutputHeight + 15) / 16));
        const int maxFramesPerSecond = macroblocksPerFrame > 0 ? maxMacroblocksPerSecond / macroblocksPerFrame : 0;
        logLine(L"NVENC capabilities: H.264=1, max=%dx%d, max macroblocks/sec=%d (estimated requested-resolution fps=%d), rateControlMask=0x%X.",
            maxWidth, maxHeight, maxMacroblocksPerSecond, maxFramesPerSecond, rateModes);
        if (maxWidth < static_cast<int>(kOutputWidth) || maxHeight < static_cast<int>(kOutputHeight)) {
            error = L"The NVIDIA GPU does not support the requested NVENC output resolution."; return false;
        }
        if (maxFramesPerSecond < static_cast<int>(kFps)) {
            error = L"The NVIDIA GPU's reported H.264 throughput is below the requested output dimensions and FPS."; return false;
        }
        uint32_t formatsCount = 0;
        nv = functions_.nvEncGetInputFormatCount(encoder_, NV_ENC_CODEC_H264_GUID, &formatsCount);
        if (nv != NV_ENC_SUCCESS || formatsCount == 0) { error = L"NVIDIA NVENC reported no H.264 input formats."; return false; }
        std::vector<NV_ENC_BUFFER_FORMAT> formats(formatsCount);
        uint32_t formatsActual = 0;
        nv = functions_.nvEncGetInputFormats(encoder_, NV_ENC_CODEC_H264_GUID, formats.data(), formatsCount, &formatsActual);
        if (nv != NV_ENC_SUCCESS || std::find(formats.begin(), formats.begin() + formatsActual, NV_ENC_BUFFER_FORMAT_ARGB) == formats.begin() + formatsActual) {
            error = L"This NVENC device does not support Direct3D BGRA input required by the GPU resize pipeline."; return false;
        }
        if (video_.rateMode != 2 && (rateModes & (video_.rateMode == 1 ? NV_ENC_PARAMS_RC_CBR : NV_ENC_PARAMS_RC_VBR)) == 0) {
            error = L"This NVENC device reports no support for the selected H.264 rate-control mode."; return false;
        }
        return true;
    }

    ReplayAacEncoder& aacConfiguration() { return mixEnabled_ ? mixAac_ : !sceneAac_.empty()?*sceneAac_.front():audioGameEnabled_ ? gameAac_ : microphoneAac_; }

    bool createEncoder(RecordingQuality quality, std::wstring& error) {
        NV_ENC_PRESET_CONFIG preset{};
        preset.version = NV_ENC_PRESET_CONFIG_VER;
        preset.presetCfg.version = NV_ENC_CONFIG_VER;
        const GUID presetId = quality == RecordingQuality::HighQuality ? NV_ENC_PRESET_P6_GUID : NV_ENC_PRESET_P4_GUID;
        NVENCSTATUS nv = functions_.nvEncGetEncodePresetConfigEx(encoder_, NV_ENC_CODEC_H264_GUID,
            presetId, NV_ENC_TUNING_INFO_HIGH_QUALITY, &preset);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not load the selected H.264 quality preset"); return false; }
        NV_ENC_CONFIG config = preset.presetCfg;
        config.gopLength = kFps * 2;
        config.frameIntervalP = 1;
        config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
        config.rcParams.rateControlMode = video_.rateMode == 2 ? NV_ENC_PARAMS_RC_CONSTQP : video_.rateMode == 1 ? NV_ENC_PARAMS_RC_CBR : NV_ENC_PARAMS_RC_VBR;
        config.rcParams.averageBitRate = video_.bitrateMbps * 1000000;
        config.rcParams.maxBitRate = (video_.rateMode == 1 ? video_.bitrateMbps : video_.maxBitrateMbps) * 1000000;
        config.rcParams.vbvBufferSize = video_.bitrateMbps * 2000000;
        config.rcParams.constQP = {video_.qp, video_.qp, video_.qp};
        config.rcParams.enableLookahead = 0;
        config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
        auto& vui = config.encodeCodecConfig.h264Config.h264VUIParameters;
        vui.videoSignalTypePresentFlag = 1;
        vui.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
        vui.videoFullRangeFlag = 0;
        vui.colourDescriptionPresentFlag = 1;
        vui.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        vui.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        vui.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;

        NV_ENC_INITIALIZE_PARAMS init{};
        init.version = NV_ENC_INITIALIZE_PARAMS_VER;
        init.encodeGUID = NV_ENC_CODEC_H264_GUID;
        init.presetGUID = presetId;
        init.encodeWidth = kOutputWidth;
        init.encodeHeight = kOutputHeight;
        init.darWidth = kOutputWidth;
        init.darHeight = kOutputHeight;
        init.frameRateNum = kFps;
        init.frameRateDen = 1;
        init.enablePTD = 1;
        init.enableEncodeAsync = 0;
        init.encodeConfig = &config;
        init.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
        nv = functions_.nvEncInitializeEncoder(encoder_, &init);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"NVENC could not initialize H.264 with the requested settings"); return false; }
        initialized_ = true;
        return true;
    }

    bool createInputTexture(std::wstring& error) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kOutputWidth;
        desc.Height = kOutputHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = gpu_->device->CreateTexture2D(&desc, nullptr, &inputTexture_);
        if (FAILED(hr)) { error = L"Could not allocate the reusable GPU encoder surface (HRESULT 0x" + hex(hr) + L")."; return false; }
        hr = gpu_->device->CreateRenderTargetView(inputTexture_.Get(), nullptr, &inputTarget_);
        if (FAILED(hr)) { error = L"Could not create the GPU encoder render target (HRESULT 0x" + hex(hr) + L")."; return false; }

        NV_ENC_REGISTER_RESOURCE reg{};
        reg.version = NV_ENC_REGISTER_RESOURCE_VER;
        reg.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.width = kOutputWidth;
        reg.height = kOutputHeight;
        reg.pitch = 0;
        reg.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
        reg.bufferUsage = NV_ENC_INPUT_IMAGE;
        reg.resourceToRegister = inputTexture_.Get();
        NVENCSTATUS nv = functions_.nvEncRegisterResource(encoder_, &reg);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not register the reusable Direct3D 11 surface with NVENC"); return false; }
        registeredInput_ = reg.registeredResource;

        NV_ENC_CREATE_BITSTREAM_BUFFER create{};
        create.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        nv = functions_.nvEncCreateBitstreamBuffer(encoder_, &create);
        if (nv != NV_ENC_SUCCESS) { error = makeMessage(nv, L"Could not allocate an NVENC H.264 output buffer"); return false; }
        bitstream_ = create;
        return true;
    }

    bool createMuxer(const std::wstring& folder, const RecordingAudioOptions&, std::wstring& error) {
        HRESULT hr = S_OK;
        std::error_code fsError;
        std::filesystem::create_directories(folder, fsError);
        if (fsError) { error = L"Could not create the selected output folder: " + folder + L" (" + win32Error(static_cast<DWORD>(fsError.value())) + L")"; return false; }
        SYSTEMTIME now{}; GetLocalTime(&now);
        wchar_t stamp[64]{};
        swprintf_s(stamp, L"ReDot_%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        const bool matroska = video_.container == ContainerFormat::Mkv;
        const std::wstring extension = matroska ? L".mkv" : L".mp4";
        finalPath_ = (std::filesystem::path(folder) / (std::wstring(stamp) + extension)).wstring();
        partialPath_ = finalPath_.substr(0, finalPath_.size() - 4) + L".partial" + extension;
        for (unsigned suffix = 1; std::filesystem::exists(finalPath_) || std::filesystem::exists(partialPath_) ||
            std::filesystem::exists(partialPath_ + L".recovered"); ++suffix) {
            finalPath_ = (std::filesystem::path(folder) / (std::wstring(stamp) + L"_" + std::to_wstring(suffix) + extension)).wstring();
            partialPath_ = finalPath_.substr(0, finalPath_.size() - 4) + L".partial" + extension;
        }
        if (matroska) return createMatroskaMuxer(error);

        ComPtr<IMFAttributes> attributes;
        hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(partialPath_.c_str(), nullptr, attributes.Get(), &writer_);
        if (SUCCEEDED(hr)) partialOwned_ = true;
        if (FAILED(hr)) { error = L"Could not create the MP4 writer (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }

        std::array<uint8_t, NV_MAX_SEQ_HDR_LEN> sequence{};
        NV_ENC_SEQUENCE_PARAM_PAYLOAD sequenceParams{};
        sequenceParams.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
        sequenceParams.spsppsBuffer = sequence.data();
        sequenceParams.inBufferSize = static_cast<uint32_t>(sequence.size());
        uint32_t sequenceSize{};
        sequenceParams.outSPSPPSPayloadSize = &sequenceSize;
        NVENCSTATUS nv = functions_.nvEncGetSequenceParams(encoder_, &sequenceParams);
        if (nv != NV_ENC_SUCCESS || sequenceSize == 0) { error = makeMessage(nv, L"Could not read H.264 sequence headers for MP4"); return false; }

        ComPtr<IMFMediaType> mediaType;
        hr = MFCreateMediaType(&mediaType);
        if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AVG_BITRATE, video_.bitrateMbps * 1000000);
        if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, kOutputWidth, kOutputHeight);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, kFps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = mediaType->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, sequence.data(), sequenceSize);
        if (SUCCEEDED(hr)) hr = writer_->AddStream(mediaType.Get(), &streamIndex_);
        if (SUCCEEDED(hr)) hr = writer_->SetInputMediaType(streamIndex_, mediaType.Get(), nullptr);
        if (SUCCEEDED(hr) && mixEnabled_) hr = addAudioTrack(L"mixed", mixedStream_, error);
        if (SUCCEEDED(hr) && audioGameEnabled_) hr = addAudioTrack(L"game", gameStream_, error);
        if (SUCCEEDED(hr) && audioMicrophoneEnabled_) hr = addAudioTrack(L"microphone", microphoneStream_, error);
        for(size_t i=0;i<sceneStreams_.size()&&SUCCEEDED(hr);++i)hr=addAudioTrack(sceneNames_[i].c_str(),sceneStreams_[i],error);
        if (SUCCEEDED(hr)) hr = writer_->BeginWriting();
        if (FAILED(hr)) { if (error.empty()) error = L"Could not initialize the H.264/AAC MP4 streams (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }
        return true;
    }

    static std::string utf8(const std::wstring& text) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(n > 0 ? n : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), n, nullptr, nullptr);
        return result;
    }

    // Matroska track numbers: video 1, audio 2.. in the same order the MP4 writer creates its streams
    // (Mix, Game, Microphone, scene stems). `stream` stores 1 + audio ordinal, so stream + 1 is the
    // Matroska track number, exactly the key saveTrackNames() already writes for MP4 track IDs.
    bool createMatroskaMuxer(std::wstring& error) {
        MkvVideoSpec spec;
        spec.width = kOutputWidth; spec.height = kOutputHeight; spec.fps = kFps;
        if (!mkvBuildAvcC(sequenceHeader_, spec.avcC, error)) return false;
        std::vector<MkvAudioSpec> tracks;
        auto add = [&](const std::string& name, std::optional<DWORD>& stream) {
            MkvAudioSpec track;
            track.name = name;
            track.audioSpecificConfig = mkvAudioSpecificConfigFromMfUserData(aacConfiguration().userData());
            if (track.audioSpecificConfig.empty()) { error = L"The Windows AAC encoder did not provide an AudioSpecificConfig for Matroska."; return false; }
            track.isDefault = tracks.empty();
            stream = static_cast<DWORD>(1 + tracks.size());
            tracks.push_back(std::move(track));
            return true;
        };
        if (mixEnabled_ && !add("Mix", mixedStream_)) return false;
        if (audioGameEnabled_ && !add("Game", gameStream_)) return false;
        if (audioMicrophoneEnabled_ && !add("Microphone", microphoneStream_)) return false;
        for (size_t i = 0; i < sceneStreams_.size(); ++i) if (!add(utf8(sceneNames_[i]), sceneStreams_[i])) return false;
        mkv_ = std::make_unique<MatroskaWriter>();
        if (!mkv_->open(partialPath_, spec, tracks, error)) { mkv_.reset(); return false; }
        partialOwned_ = true; // CREATE_NEW succeeded: this exact file is ours
        return true;
    }

    HRESULT addAudioTrack(const wchar_t* name, std::optional<DWORD>& stream, std::wstring& error) {
        ComPtr<IMFMediaType> output, input;
        HRESULT hr = MFCreateMediaType(&output);
        if (SUCCEEDED(hr) && aacConfiguration().outputType()) hr = aacConfiguration().outputType()->CopyAllItems(output.Get());
        else if (SUCCEEDED(hr)) hr = MF_E_TRANSFORM_TYPE_NOT_SET;
        DWORD index{};
        if (SUCCEEDED(hr)) hr = writer_->AddStream(output.Get(), &index);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&input);
        if (SUCCEEDED(hr) && aacConfiguration().outputType()) hr = aacConfiguration().outputType()->CopyAllItems(input.Get());
        if (SUCCEEDED(hr)) hr = writer_->SetInputMediaType(index, input.Get(), nullptr);
        if (SUCCEEDED(hr)) stream = index;
        else error = std::wstring(L"Media Foundation could not create the AAC ") + name + L" track (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr);
        return hr;
    }

    bool writePcmSample(DWORD stream, uint64_t firstFrame, const std::vector<int16_t>& pcm, std::wstring& error) {
        if (pcm.empty() || pcm.size() % 2) { error = L"An audio block did not contain interleaved stereo PCM."; return false; }
        const DWORD bytes = static_cast<DWORD>(pcm.size() * sizeof(int16_t));
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
        BYTE* destination = nullptr; DWORD maximum = 0;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&destination, &maximum, nullptr);
        if (SUCCEEDED(hr)) { if (maximum < bytes) hr = E_OUTOFMEMORY; else CopyMemory(destination, pcm.data(), bytes); buffer->Unlock(); }
        if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(bytes);
        ComPtr<IMFSample> sample;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        const uint64_t frames = pcm.size() / 2;
        const LONGLONG start = static_cast<LONGLONG>(firstFrame * kHnsPerSecond / 48000);
        const LONGLONG end = static_cast<LONGLONG>((firstFrame + frames) * kHnsPerSecond / 48000);
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(start);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(end - start);
        if (SUCCEEDED(hr)) hr = writer_->WriteSample(stream, sample.Get());
        if (FAILED(hr)) { error = L"Could not encode/write an AAC PCM input block (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }
        return true;
    }

    bool writeAacPacket(DWORD stream, const ReplayPacket& packet, std::wstring& error) {
        if (mkv_) {
            if (stream == 0) { error = L"Invalid Matroska audio track mapping."; return false; }
            return mkv_->writeAudio(stream - 1, packet.bytes.data(), packet.bytes.size(), packet.pts, error);
        }
        if (!writer_) return true;
        ComPtr<IMFMediaBuffer> buffer; HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.bytes.size()), &buffer);
        BYTE* destination = nullptr; DWORD maximum = 0;
        if (SUCCEEDED(hr)) hr = buffer->Lock(&destination, &maximum, nullptr);
        if (SUCCEEDED(hr)) { if (maximum < packet.bytes.size()) hr = E_OUTOFMEMORY; else CopyMemory(destination, packet.bytes.data(), packet.bytes.size()); buffer->Unlock(); }
        if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(static_cast<DWORD>(packet.bytes.size()));
        ComPtr<IMFSample> sample;
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(packet.pts);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(packet.duration);
        if (SUCCEEDED(hr)) hr = writer_->WriteSample(stream, sample.Get());
        if (FAILED(hr)) { error = L"Could not mux an encoded AAC packet (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }
        return true;
    }

    bool encodeAudio(ReplayAacEncoder& encoder, ReplayTrack track, std::optional<DWORD> stream,
        uint64_t firstFrame, const std::vector<int16_t>& pcm, std::wstring& error) {
        std::vector<ReplayPacket> packets;
        if (!encoder.encode(pcm, firstFrame, track, packets, error)) return false;
        for (auto& packet : packets) {
            if (stream && !writeAacPacket(*stream, packet, error)) return false;
            if (replay_) replay_->push(std::move(packet));
        }
        return true;
    }

    bool drainAudioEncoder(ReplayAacEncoder& encoder, ReplayTrack track, std::optional<DWORD> stream, std::wstring& error) {
        std::vector<ReplayPacket> packets;
        if (!encoder.drain(track, packets, error)) return false;
        for (auto& packet : packets) {
            if (stream && !writeAacPacket(*stream, packet, error)) return false;
            if (replay_) replay_->push(std::move(packet));
        }
        return true;
    }

    HRESULT drawResizedFrame(const ComPtr<ID3D11Texture2D>& source, std::wstring& error, ID3D11ShaderResourceView* view) {
        if (!gpu_ || !gpu_->contextMutex) { error = L"The shared Direct3D context is unavailable."; return E_FAIL; }
        std::lock_guard contextLock(*gpu_->contextMutex);
        D3D11_TEXTURE2D_DESC sourceDesc{};
        source->GetDesc(&sourceDesc);
        ComPtr<ID3D11ShaderResourceView> sourceView = view;
        if (!sourceView) {
            HRESULT hr = gpu_->device->CreateShaderResourceView(source.Get(), nullptr, &sourceView);
            if (FAILED(hr)) { error = L"Could not create a view of the captured frame for NVENC."; return hr; }
            ++viewsCreated_;
        } else ++viewsReused_;
        const float dimensions[] = {static_cast<float>(sourceDesc.Width), static_cast<float>(sourceDesc.Height),
            static_cast<float>(kOutputWidth), static_cast<float>(kOutputHeight)};
        gpu_->context->UpdateSubresource(gpu_->constants.Get(), 0, nullptr, dimensions, 0, 0);
        ID3D11RenderTargetView* target = inputTarget_.Get();
        gpu_->context->OMSetRenderTargets(1, &target, nullptr);
        const FLOAT background[] = {0.055f, 0.063f, 0.078f, 1.0f};
        gpu_->context->ClearRenderTargetView(target, background);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(kOutputWidth), static_cast<float>(kOutputHeight), 0.0f, 1.0f};
        gpu_->context->RSSetViewports(1, &viewport);
        ID3D11Buffer* constants = gpu_->constants.Get();
        ID3D11ShaderResourceView* texture = sourceView.Get();
        ID3D11SamplerState* sampler = gpu_->sampler.Get();
        gpu_->context->VSSetShader(gpu_->vertexShader.Get(), nullptr, 0);
        gpu_->context->PSSetShader(gpu_->pixelShader.Get(), nullptr, 0);
        gpu_->context->PSSetConstantBuffers(0, 1, &constants);
        gpu_->context->PSSetShaderResources(0, 1, &texture);
        gpu_->context->PSSetSamplers(0, 1, &sampler);
        gpu_->context->IASetInputLayout(nullptr);gpu_->context->OMSetBlendState(nullptr,nullptr,~0u);gpu_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu_->context->Draw(3, 0);
        ID3D11ShaderResourceView* noTexture = nullptr;
        gpu_->context->PSSetShaderResources(0, 1, &noTexture);
        gpu_->context->OMSetRenderTargets(0, nullptr, nullptr);
        return S_OK;
    }

    bool writeSample(const BYTE* bytes, DWORD length, uint64_t frameIndex, bool keyframe, std::wstring& error) {
        if (!bytes || length == 0) { error = L"NVENC returned an empty H.264 frame."; return false; }
        if (length > 64 * 1024 * 1024) { error = L"NVENC returned an implausibly large H.264 packet (" + std::to_wstring(length) + L" bytes)."; return false; }
        if (replayOnly_) {
            if (replay_) {
                ReplayPacket packet; packet.track = ReplayTrack::Video;
                packet.pts = packet.dts = frameIndex * kHnsPerSecond / kFps;
                packet.duration = ((frameIndex + 1) * kHnsPerSecond / kFps) - packet.pts;
                packet.keyframe = keyframe;
                packet.bytes.assign(bytes, bytes + length); replay_->push(std::move(packet));
            }
            return true;
        }
        if (mkv_) {
            if (!mkv_->writeVideo(bytes, length, static_cast<int64_t>(frameIndex * kHnsPerSecond / kFps), keyframe, error)) return false;
            ++mkvFrames_;
            if (replay_) {
                ReplayPacket packet; packet.track = ReplayTrack::Video;
                packet.pts = packet.dts = frameIndex * kHnsPerSecond / kFps;
                packet.duration = ((frameIndex + 1) * kHnsPerSecond / kFps) - packet.pts;
                packet.keyframe = keyframe;
                packet.bytes.assign(bytes, bytes + length);
                replay_->push(std::move(packet));
            }
            return true;
        }
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreateMemoryBuffer(length, &buffer);
        const wchar_t* stage = L"MFCreateMemoryBuffer";
        BYTE* destination = nullptr;
        DWORD maximum = 0;
        DWORD currentLength = 0;
        if (SUCCEEDED(hr)) { stage = L"IMFMediaBuffer::Lock"; hr = buffer->Lock(&destination, &maximum, &currentLength); }
        if (SUCCEEDED(hr)) {
            if (maximum < length) hr = E_OUTOFMEMORY;
            else CopyMemory(destination, bytes, length);
            buffer->Unlock();
        }
        if (SUCCEEDED(hr)) { stage = L"IMFMediaBuffer::SetCurrentLength"; hr = buffer->SetCurrentLength(length); }
        ComPtr<IMFSample> sample;
        if (SUCCEEDED(hr)) { stage = L"MFCreateSample"; hr = MFCreateSample(&sample); }
        if (SUCCEEDED(hr)) { stage = L"IMFSample::AddBuffer"; hr = sample->AddBuffer(buffer.Get()); }
        const LONGLONG sampleTime = static_cast<LONGLONG>(frameIndex * kHnsPerSecond / kFps);
        const LONGLONG sampleEnd = static_cast<LONGLONG>((frameIndex + 1) * kHnsPerSecond / kFps);
        if (SUCCEEDED(hr)) { stage = L"IMFSample::SetSampleTime"; hr = sample->SetSampleTime(sampleTime); }
        if (SUCCEEDED(hr)) { stage = L"IMFSample::SetSampleDuration"; hr = sample->SetSampleDuration(sampleEnd - sampleTime); }
        if (SUCCEEDED(hr) && keyframe) { stage = L"IMFSample::SetCleanPoint"; hr = sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE); }
        if (SUCCEEDED(hr) && writer_) { stage = L"IMFSinkWriter::WriteSample"; hr = writer_->WriteSample(streamIndex_, sample.Get()); }
        if (FAILED(hr)) { error = std::wstring(stage) + L" failed for the " + std::to_wstring(length) + L"-byte H.264 packet (HRESULT 0x" + hex(hr) + L"): " + hresultText(hr); return false; }
        if (replay_) {
            ReplayPacket packet; packet.track = ReplayTrack::Video;
            packet.pts = packet.dts = frameIndex * kHnsPerSecond / kFps;
            packet.duration = ((frameIndex + 1) * kHnsPerSecond / kFps) - packet.pts;
            packet.keyframe = keyframe;
            packet.bytes.assign(bytes, bytes + length);
            replay_->push(std::move(packet));
        }
        return true;
    }

    bool readSequenceHeader(std::wstring& error) {
        std::array<uint8_t, NV_MAX_SEQ_HDR_LEN> sequence{};
        NV_ENC_SEQUENCE_PARAM_PAYLOAD params{};
        params.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER; params.spsppsBuffer = sequence.data();
        params.inBufferSize = static_cast<uint32_t>(sequence.size());
        uint32_t size{}; params.outSPSPPSPayloadSize = &size;
        const NVENCSTATUS nv = functions_.nvEncGetSequenceParams(encoder_, &params);
        if (nv != NV_ENC_SUCCESS || size == 0) { error = makeMessage(nv, L"Could not read H.264 sequence headers"); return false; }
        sequenceHeader_.assign(sequence.begin(), sequence.begin() + size);
        return true;
    }

    std::shared_ptr<GpuResources> gpu_;
    HMODULE module_{};
    NV_ENCODE_API_FUNCTION_LIST functions_{};
    void* encoder_{};
    bool initialized_{};
    bool mfStarted_{};
    ComPtr<ID3D11Texture2D> inputTexture_;
    ComPtr<ID3D11RenderTargetView> inputTarget_;
    NV_ENC_REGISTERED_PTR registeredInput_{};
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstream_{};
    ComPtr<IMFSinkWriter> writer_;
    std::unique_ptr<MatroskaWriter> mkv_;
    bool mkvFinalized_{};
    uint64_t mkvFrames_{};
    DWORD streamIndex_{};
    std::optional<DWORD> mixedStream_;
    std::optional<DWORD> gameStream_;
    std::optional<DWORD> microphoneStream_;
    std::wstring partialPath_;
    bool partialOwned_{};
    std::wstring finalPath_;
    std::shared_ptr<ReplayBuffer> replay_;
    uint64_t viewsCreated_{}, viewsReused_{};
    bool replayOnly_{};
    std::vector<uint8_t> sequenceHeader_;
    ReplayAacEncoder mixAac_, gameAac_, microphoneAac_;
    std::vector<std::unique_ptr<ReplayAacEncoder>> sceneAac_;
    std::vector<std::optional<DWORD>> sceneStreams_;
    std::vector<std::wstring> sceneNames_;
    std::vector<size_t> sceneInputIndexes_;
    uint64_t traceLastLoud_{};
    VideoConfig video_;
    UINT kOutputWidth{1920}, kOutputHeight{1080}, kFps{60};
    bool mixEnabled_{};
    bool audioEnabled_{};
    bool audioGameEnabled_{};
    bool audioMicrophoneEnabled_{};
};



} // namespace

void RecordingIngress::noteAvailableFrame(std::chrono::steady_clock::time_point timestamp) {
    std::lock_guard lock(mutex);
    if (stopping) return;
    if (!availableFrames.fetch_add(1)) firstAvailableAt = timestamp;
    else if (timestamp > lastAvailableAt) {
        const auto gap = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(timestamp - lastAvailableAt).count());
        maxAvailableGapMicros.store(std::max(maxAvailableGapMicros.load(), gap));
    }
    lastAvailableAt = std::max(lastAvailableAt, timestamp);
}

void RecordingIngress::push(RecordedFrame frame) {
    {
        std::lock_guard lock(mutex);
        if (stopping) return;
        const uint64_t sourceIndex = sourceFrames.fetch_add(1, std::memory_order_relaxed);
        if (!sourceIndex) firstSourceAt = frame.capturedAt;
        if (sourceIndex && lastSourceAt != std::chrono::steady_clock::time_point{} && frame.capturedAt > lastSourceAt) {
            const auto gap = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(frame.capturedAt - lastSourceAt).count());
            uint64_t prior = maxSourceGapMicros.load(std::memory_order_relaxed);
            while (gap > prior && !maxSourceGapMicros.compare_exchange_weak(prior, gap, std::memory_order_relaxed)) {}
        }
        lastSourceAt = std::max(lastSourceAt, frame.capturedAt);
        if (frames.size() == Capacity) {
            frames.pop_front();
            queueDrops.fetch_add(1, std::memory_order_relaxed);
        }
        const auto position = std::upper_bound(frames.begin(), frames.end(), frame.capturedAt, [](const auto& time, const RecordedFrame& existing) { return time < existing.capturedAt; });
        frames.insert(position, std::move(frame));
        videoQueueHighWater.store(std::max<uint64_t>(videoQueueHighWater.load(), frames.size()));
    }
    wake.notify_one();
}

void RecordingIngress::push(RecordedAudio packet) {
    {
        std::lock_guard lock(mutex);
        if (stopping) return;
        if (audio.size() == AudioCapacity) { audio.pop_front(); audioQueueDrops.fetch_add(1, std::memory_order_relaxed); }
        audio.push_back(std::move(packet));
        audioQueueHighWater.store(std::max<uint64_t>(audioQueueHighWater.load(), audio.size()));
    }
    wake.notify_one();
}

void RecordingIngress::requestStop() {
    {
        std::lock_guard lock(mutex);
        if (!stopping) { stopping = true; stopAt = std::chrono::steady_clock::now(); }
    }
    wake.notify_one();
}

RecordingPipeline::~RecordingPipeline() {
    requestStop();
    join();
}

bool RecordingPipeline::start(HWND notifyWindow, std::shared_ptr<GpuResources> gpu,
    const std::wstring& outputFolder, RecordingQuality quality, const RecordingAudioOptions& audio, std::wstring& error,
    bool replayOnly, std::shared_ptr<ReplayBuffer> replay, VideoConfig video) {
    if (!video.validate(error)) return false;
    if (busy()) { error = L"A recording is already running or finalizing."; return false; }
    if (worker_.joinable()) worker_.join();
    if (outputFolder.empty()) { error = L"Choose an output folder before starting a recording."; return false; }
    ingress_ = std::make_shared<RecordingIngress>();
    ingress_->video = video; ingress_->liveGameMute.store(audio.gameMuted); ingress_->liveMicMute.store(audio.microphoneMuted);
    ingress_->replayBuffer = std::move(replay);
    elapsedMilliseconds_.store(0);
    recording_.store(false);
    busy_.store(true);
    auto input = ingress_;
    try {
        worker_ = std::thread(&RecordingPipeline::run, this, notifyWindow, std::move(gpu), input, outputFolder, quality, audio, replayOnly, video);
    } catch (const std::system_error&) {
        ingress_.reset(); busy_.store(false); error = L"Could not start the recording worker."; return false;
    }
    return true;
}

std::shared_ptr<RecordingIngress> RecordingPipeline::ingress() const { return ingress_; }

void RecordingPipeline::requestStop() {
    const auto current = ingress_;
    if (current) current->requestStop();
}

void RecordingPipeline::join() {
    if (worker_.joinable()) worker_.join();
    ingress_.reset();
    recording_.store(false);
    busy_.store(false);
    { std::lock_guard lock(completionMutex_); failedCompletion_.reset(); }
    completionPending_.store(false);
}

std::unique_ptr<RecordingEvent> RecordingPipeline::takeFailedCompletion() {
    if(busy_.load())return {};
    std::lock_guard lock(completionMutex_);return std::move(failedCompletion_);
}

bool RecordingPipeline::busy() const noexcept { return busy_.load() || completionPending_.load(); }
bool RecordingPipeline::recording() const noexcept { return recording_.load(); }
uint64_t RecordingPipeline::elapsedMilliseconds() const noexcept { return elapsedMilliseconds_.load(); }

void RecordingPipeline::post(HWND window, RecordingEvent* event) noexcept {
    if (!window || !PostMessageW(window, kRecordingEventMessage, 0, reinterpret_cast<LPARAM>(event))) delete event;
}

void RecordingPipeline::run(HWND notifyWindow, std::shared_ptr<GpuResources> gpu,
    std::shared_ptr<RecordingIngress> ingress, std::wstring outputFolder,
    RecordingQuality quality, RecordingAudioOptions audio, bool replayOnly, VideoConfig video) noexcept {

    // Declared first, destroyed last: the UI may join on Finished. Never publish
    // completion while COM/MF or an opt-in PCM writer is still draining.
    struct Completion {
        std::atomic_bool& busy; std::atomic_bool& recording; std::atomic_bool& pending; HWND window;
        std::mutex& mutex;std::unique_ptr<RecordingEvent>& fallback;
        std::unique_ptr<RecordingEvent> event;
        ~Completion() {
            recording.store(false);
            pending.store(window && event);
            busy.store(false);
#ifdef REDOT_UI_VERIFICATION
            if(auto delay=diagnosticSetting(L"PANDACLIP_TEST_COMPLETION_POST_DELAY_MS"))Sleep(static_cast<DWORD>(std::min<uint64_t>(delay,5000)));
            const bool fail=diagnosticSetting(L"PANDACLIP_TEST_COMPLETION_POST_FAIL")!=0;
#else
            const bool fail=false;
#endif
            if (!fail && window && event && PostMessageW(window, kRecordingEventMessage, 0, reinterpret_cast<LPARAM>(event.get()))) event.release();
            else if(window && event){
                std::lock_guard lock(mutex);fallback=std::move(event);
                logLine(L"Recording completion notification rejected; terminal result retained for owner health polling.");
            }else pending.store(false);
        }
    } completion{busy_, recording_, completionPending_, notifyWindow,completionMutex_,failedCompletion_};

    if(audio.scene&&!video.trackMix&&!video.trackGame&&!video.trackMicrophone)audio.scene.reset(); // Video-only must not open scene audio devices.
    const auto kFps = video.fps;
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comStarted = SUCCEEDED(com);
    std::wstring failure;
    NvencMp4Writer writer;
    bool mfStarted = false;
    bool writerOpened = false;
    try {
        if (FAILED(com) && com != RPC_E_CHANGED_MODE) { failure = L"Could not initialize the recording worker's COM apartment."; }
        if (failure.empty()) {
            writerOpened = writer.open(gpu, outputFolder, quality, audio, failure, ingress->replayBuffer, replayOnly, video);
            mfStarted = writer.mfStarted();
        }

        if (failure.empty()) {
            ingress->audioTrace=AudioCaptureTrace::fromEnvironment();
            wchar_t traceRequested[2]{};
            if(!ingress->audioTrace && GetEnvironmentVariableW(L"PANDACLIP_AUDIO_CAPTURE_TRACE",traceRequested,2))logLine(L"Audio checkpoint request refused: use a fresh folder inside this workspace's build directory; maximum four sessions per review process. Recording continues without checkpoint files.");
            if(ingress->audioTrace)logLine(L"Opt-in lossless audio checkpoints: %s (120-second bounded trace).",ingress->audioTrace->folder().c_str());
            std::vector<std::unique_ptr<AudioCapture>> audioCaptures;
            if (audio.scene) { if(!audio.scene->start(ingress,failure))throw std::runtime_error("scene audio startup"); }
            if (!audio.scene && audio.gameMode != GameAudioMode::Off) {
                auto capture = std::make_unique<AudioCapture>(AudioSourceKind::Game, ingress, audio.targetProcessId, audio.gameMode, audio.systemAudioDeviceId);
                if (!capture->start(failure)) throw std::runtime_error("could not start game audio capture");
                audioCaptures.push_back(std::move(capture));
            }
            if (!audio.scene && audio.microphoneEnabled) {
                auto capture = std::make_unique<AudioCapture>(AudioSourceKind::Microphone, ingress, 0, GameAudioMode::Off, audio.microphoneDeviceId);
                if (!capture->start(failure)) throw std::runtime_error("could not start microphone capture");
                audioCaptures.push_back(std::move(capture));
            }
            recording_.store(true);
            const bool audioEnabled = (audio.scene&&!audio.scene->stems().empty()) || (audio.gameMode != GameAudioMode::Off && (video.trackMix || video.trackGame)) || (audio.microphoneEnabled && (video.trackMix || video.trackMicrophone));
            post(notifyWindow, new RecordingEvent{RecordingEventKind::Started, true,
                (replayOnly ? L"Replay active: " : L"Recording active: ") + video.description() + (audioEnabled ? L" with selected AAC tracks." : L". Video only."), 0});

            ComPtr<ID3D11Texture2D> lastFrame;
            ComPtr<ID3D11ShaderResourceView> lastView;
            std::shared_ptr<void> lastOwner;
            CaptureClock clock{};
            bool haveClock = false;
            AudioTimeline audioTimeline;
            std::deque<RecordedAudio> pendingAudio;
            uint64_t audioFramesWritten = 0;
            bool audioStopped = false;
            std::wstring lastAudioErrorSent;
            auto nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            const auto firstFrameLimit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            for (;;) {
                std::unique_lock lock(ingress->mutex);
                while (!ingress->audio.empty()) { if (pendingAudio.size() == RecordingIngress::AudioCapacity) { pendingAudio.pop_front(); ingress->audioQueueDrops.fetch_add(1); } pendingAudio.push_back(std::move(ingress->audio.front())); ingress->audio.pop_front(); }
                std::wstring liveAudioError = ingress->audioError;
                if (!haveClock) {
                    if (!ingress->wake.wait_until(lock, firstFrameLimit, [&] { return !ingress->frames.empty() || !ingress->audio.empty() || ingress->stopping; })) {
                        failure = L"No owned WGC frame reached the encoder within 10 seconds. Check that the selected window is visible and capturable, then restart capture."; break;
                    }
                    while (!ingress->audio.empty()) { if (pendingAudio.size() == RecordingIngress::AudioCapacity) { pendingAudio.pop_front(); ingress->audioQueueDrops.fetch_add(1); } pendingAudio.push_back(std::move(ingress->audio.front())); ingress->audio.pop_front(); }
                    if (ingress->frames.empty() && ingress->stopping) break;
                    if (ingress->frames.empty() && std::chrono::steady_clock::now() >= firstFrameLimit) {
                        failure = L"Audio arrived but no owned WGC video frame reached the encoder within 10 seconds. Check the selected window and restart capture."; break;
                    }
                    if (ingress->frames.empty()) continue;
                    clock.origin = ingress->frames.front().capturedAt;
                    clock.nextDeadline = clock.origin;
                    logLine(L"Shared timeline origin (steady ns)=%lld; game/mic adjustment=%d/%d ms.", std::chrono::duration_cast<std::chrono::nanoseconds>(clock.origin.time_since_epoch()).count(), audio.gameSyncMilliseconds, audio.microphoneSyncMilliseconds);
                    haveClock = true;
                    if(ingress->audioTrace)ingress->audioTrace->origin(clock.origin);
                    if(audio.scene)audio.scene->origin(clock.origin);
                }
                for (const auto& packet : pendingAudio) audioTimeline.ingest(packet, clock.origin, *ingress, audio);
                pendingAudio.clear();

                const auto deadline = clock.nextDeadline;
                // A composed slot becomes available only after its GPU draw.
                // Wait one bounded render period, keeping the PTS cutoff and
                // common audio/video origin unchanged. No audio offset is added.
                const auto readyAt=deadline+(ingress->composedVideo.load()?std::chrono::nanoseconds(1'000'000'000ULL/kFps):std::chrono::nanoseconds(0));
                ingress->wake.wait_until(lock, readyAt);
                while (!ingress->audio.empty()) { audioTimeline.ingest(ingress->audio.front(), clock.origin, *ingress, audio); ingress->audio.pop_front(); }
                liveAudioError = ingress->audioError;
                if (!liveAudioError.empty() && liveAudioError != lastAudioErrorSent) {
                    lastAudioErrorSent = liveAudioError;
                    post(notifyWindow, new RecordingEvent{RecordingEventKind::Progress, true,
                        L"Audio source warning: " + liveAudioError, elapsedMilliseconds_.load(), clock.frameIndex,
                        ingress->queueDrops.load(), ingress->repeatedFrames.load(), ingress->replacedFrames.load()});
                }
                const bool stopping = ingress->stopping;
                const auto stopAt = ingress->stopAt;
                if (stopping && !audioStopped) {
                    lock.unlock();
                    for (auto& capture : audioCaptures) capture->stop();
                    if(audio.scene)audio.scene->stop();
                    audioStopped = true;
                    lock.lock();
                    while (!ingress->audio.empty()) { audioTimeline.ingest(ingress->audio.front(), clock.origin, *ingress, audio); ingress->audio.pop_front(); }
                }
                if (stopping && deadline > stopAt) break;
                if (!stopping && std::chrono::steady_clock::now() < readyAt) continue;

                ComPtr<ID3D11Texture2D> selected;
                ComPtr<ID3D11ShaderResourceView> selectedView;
                std::shared_ptr<void> selectedOwner;
                while (!ingress->frames.empty() && ingress->frames.front().capturedAt <= deadline) {
                    if (selected) ingress->replacedFrames.fetch_add(1, std::memory_order_relaxed);
                    selected = std::move(ingress->frames.front().texture);
                    selectedView = std::move(ingress->frames.front().view);
                    selectedOwner = std::move(ingress->frames.front().owner);
                    ingress->frames.pop_front();
                }
                if (selected) { lastFrame = std::move(selected); lastView = std::move(selectedView); lastOwner = std::move(selectedOwner); }
                else if (lastFrame) ingress->repeatedFrames.fetch_add(1, std::memory_order_relaxed);
                if (!lastFrame) {
                    if (stopping) break;
                    clock.nextDeadline = clock.origin + std::chrono::nanoseconds(clock.frameIndex * 1'000'000'000ULL / kFps);
                    continue;
                }
                lock.unlock();

                const auto encodeStart = std::chrono::steady_clock::now();
                if (!writer.encode(lastFrame, clock.frameIndex, failure, lastView.Get())) break;
                ingress->encoderViewsCreated.store(writer.viewsCreated(), std::memory_order_relaxed);
                ingress->encoderViewsReused.store(writer.viewsReused(), std::memory_order_relaxed);
                const uint64_t encodeMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - encodeStart).count());
                ingress->totalEncodeMicros.fetch_add(encodeMicros, std::memory_order_relaxed);
#ifdef REDOT_TEST_FIXTURE
                ingress->verificationEncodeHistogram[std::min<uint64_t>(400,encodeMicros/50)].fetch_add(1,std::memory_order_relaxed);
#endif
                uint64_t previousMax = ingress->maxEncodeMicros.load(std::memory_order_relaxed);
                while (encodeMicros > previousMax && !ingress->maxEncodeMicros.compare_exchange_weak(previousMax, encodeMicros, std::memory_order_relaxed)) {}
                if (encodeMicros > 1'000'000 / kFps) ingress->slowEncodeFrames.fetch_add(1, std::memory_order_relaxed);
                const auto audioDeadline = stopping ? stopAt : std::chrono::steady_clock::now() - std::chrono::milliseconds(53 + std::max({0, -audio.gameSyncMilliseconds, -audio.microphoneSyncMilliseconds,audio.scene?audio.scene->advanceMilliseconds():0}));
                const int64_t audioElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(audioDeadline - clock.origin).count();
                const uint64_t eligibleFrames = audioElapsed > 0 ? static_cast<uint64_t>(audioElapsed) * 48000 / 1'000'000'000ULL : 0;
                const uint64_t audioEnd = (eligibleFrames / AudioTimeline::BlockFrames) * AudioTimeline::BlockFrames;
                std::vector<int16_t> mixed, gamePcm, micPcm;
                while (audioFramesWritten + AudioTimeline::BlockFrames <= audioEnd &&
                    (audio.gameMode != GameAudioMode::Off || audio.microphoneEnabled || (audio.scene&&!audio.scene->stems().empty()))) {
                    audio.gameMuted = ingress->liveGameMute.load(); audio.microphoneMuted = ingress->liveMicMute.load();
                    if(audio.scene){std::vector<std::vector<int16_t>> stems;audio.scene->render(audioFramesWritten,AudioTimeline::BlockFrames,mixed,stems);if(!writer.writeSceneAudio(audioFramesWritten,mixed,stems,failure))break;}
                    else{audioTimeline.build(audioFramesWritten, AudioTimeline::BlockFrames, audio, mixed, gamePcm, micPcm, ingress.get());if (!writer.writeAudio(audioFramesWritten, mixed, gamePcm, micPcm, failure)) break;}
                    audioFramesWritten += AudioTimeline::BlockFrames;
                }
                if (!failure.empty()) break;
                ingress->encodedFrames.fetch_add(1, std::memory_order_relaxed);
                ++clock.frameIndex;
                clock.nextDeadline = clock.origin + std::chrono::nanoseconds(clock.frameIndex * 1'000'000'000ULL / kFps);
                elapsedMilliseconds_.store(clock.frameIndex * 1000 / kFps);
                if (std::chrono::steady_clock::now() >= nextStats) {
                    size_t queued = 0, audioQueued = 0;
                    std::chrono::steady_clock::duration sourceSpan{};
                    { std::lock_guard qlock(ingress->mutex); queued = ingress->frames.size(); audioQueued = ingress->audio.size(); sourceSpan = ingress->lastSourceAt - ingress->firstSourceAt; }
                    const double sourceFps = std::chrono::duration<double>(sourceSpan).count() > 0 ?
                        (ingress->sourceFrames.load() - 1) / std::chrono::duration<double>(sourceSpan).count() : 0.0;
                    logLine(L"Recording diagnostics: WGC sourceFrames=%llu (%.2f fps, max gap %.1f ms), CFR slots=%llu, encodeSlow=%llu/max %.2f ms, queueDrops=%llu, late/replaced=%llu, repeated=%llu, audioQueueDrops=%llu, lateAudio=%llu, timestampFallbacks=%llu, audioQueued=%zu/%zu, gameLevel=%llu, micLevel=%llu, videoQueued=%zu/%zu.",
                        ingress->sourceFrames.load(), sourceFps, ingress->maxSourceGapMicros.load() / 1000.0, clock.frameIndex,
                        ingress->slowEncodeFrames.load(), ingress->maxEncodeMicros.load() / 1000.0,
                        ingress->queueDrops.load(), ingress->replacedFrames.load(), ingress->repeatedFrames.load(),
                        ingress->audioQueueDrops.load(), ingress->lateAudioFrames.load(), ingress->timestampFallbacks.load(),
                        audioQueued, RecordingIngress::AudioCapacity, ingress->gameLevel.load(), ingress->microphoneLevel.load(), queued, RecordingIngress::Capacity);
                    logLine(L"Capture availability: beforeGPU=%llu, accepted=%llu, GPU pool drops=%llu, largest available timestamp gap=%.2f ms, max callback delivery=%.2f ms, WGC timestamp fallbacks=%llu.",
                        ingress->availableFrames.load(), ingress->sourceFrames.load(), ingress->gpuPoolDrops.load(), ingress->maxAvailableGapMicros.load() / 1000.0,
                        ingress->maxCaptureDeliveryMicros.load() / 1000.0, ingress->captureTimestampFallbacks.load());
                    post(notifyWindow, new RecordingEvent{RecordingEventKind::Progress, true,
                        liveAudioError.empty() ? L"Recording with synchronized AAC audio." : L"Recording continues; " + liveAudioError,
                        elapsedMilliseconds_.load(), clock.frameIndex,
                        ingress->queueDrops.load(), ingress->repeatedFrames.load(), ingress->replacedFrames.load()});
                    nextStats = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
                if (stopping) {
                    std::lock_guard qlock(ingress->mutex);
                    if (clock.nextDeadline > ingress->stopAt) break;
                }
            }
            if (failure.empty() && clock.frameIndex && (audio.gameMode != GameAudioMode::Off || audio.microphoneEnabled || (audio.scene&&!audio.scene->stems().empty()))) {
                // Close the AAC timeline against the final CFR video timestamp;
                // pad a short underflow with silence rather than ending early.
                const uint64_t targetAudioEnd = ((clock.frameIndex * 48000 / kFps) / AudioTimeline::BlockFrames) * AudioTimeline::BlockFrames;
                std::vector<int16_t> mixed, gamePcm, micPcm;
                while (audioFramesWritten + AudioTimeline::BlockFrames <= targetAudioEnd) {
                    audio.gameMuted = ingress->liveGameMute.load(); audio.microphoneMuted = ingress->liveMicMute.load();
                    if(audio.scene){std::vector<std::vector<int16_t>> stems;audio.scene->render(audioFramesWritten,AudioTimeline::BlockFrames,mixed,stems);if(!writer.writeSceneAudio(audioFramesWritten,mixed,stems,failure))break;}
                    else{audioTimeline.build(audioFramesWritten, AudioTimeline::BlockFrames, audio, mixed, gamePcm, micPcm, ingress.get());if (!writer.writeAudio(audioFramesWritten, mixed, gamePcm, micPcm, failure)) break;}
                    audioFramesWritten += AudioTimeline::BlockFrames;
                }
            }
            if (failure.empty() && clock.frameIndex == 0) failure = L"No capture frames arrived, so no video file was created.";
        }

        if(audio.scene)audio.scene->stop();
        if(ingress->audioTrace){
            ingress->audioTrace->finish();
            logLine(L"Audio checkpoints finished: %s; failed=%d dropped events=%llu. Check summary.txt before listening comparisons.",ingress->audioTrace->folder().c_str(),ingress->audioTrace->failed(),ingress->audioTrace->droppedEvents());
        }
        if (writerOpened && failure.empty() && !writer.drainAudio(failure)) {
            logLine(L"AAC drain failed: %s", failure.c_str());
        }
        bool finalized = false;
        if (writerOpened && failure.empty()) finalized = writer.finalize(failure);
        else if (writerOpened) { std::wstring finalizeError; writer.finalize(finalizeError); }

        std::wstring finalMessage;
        bool succeeded = writerOpened && failure.empty() && finalized;
        if (succeeded && replayOnly) {
            finalMessage = L"Replay encoder stopped. Buffered compressed video remains available until replay is disabled or settings change.";
        } else if (succeeded) {
            std::error_code ec;
            std::filesystem::rename(writer.partialPath(), writer.finalPath(), ec);
            if (ec) { succeeded = false; failure = L"The recording was finalized, but could not be moved to its final filename: " + win32Error(static_cast<DWORD>(ec.value())); }
            else {
                writer.saveTrackNames();
                finalMessage = L"Saved recording: " + writer.finalPath();
                std::lock_guard qlock(ingress->mutex);
                if (!ingress->audioError.empty()) finalMessage += L"\nAudio warning: " + ingress->audioError;
            }
        }
        if (!succeeded) {
            if (failure.empty()) failure = L"Recording could not be finalized.";
            writer.discardPartial(failure);
            logLine(L"Recording failed: %s", failure.c_str());
            finalMessage = L"Recording failed: " + failure;
        } else if (!replayOnly) {
            std::chrono::steady_clock::duration sourceSpan{};
            { std::lock_guard qlock(ingress->mutex); sourceSpan = ingress->lastSourceAt - ingress->firstSourceAt; }
            const double sourceFps = std::chrono::duration<double>(sourceSpan).count() > 0 ?
                (ingress->sourceFrames.load() - 1) / std::chrono::duration<double>(sourceSpan).count() : 0.0;
            const uint64_t frames = ingress->encodedFrames.load();
            const double averageEncodeMs = frames ? ingress->totalEncodeMicros.load() / static_cast<double>(frames) / 1000.0 : 0.0;
            logLine(L"Recording saved: %s; WGC sourceFrames=%llu (%.2f fps, max gap %.1f ms); CFR frames=%llu; duration=%.3f s; encode avg/max=%.2f/%.2f ms, slow=%llu; videoQueueDrops=%llu; repeated=%llu; late/replaced=%llu; audioQueueDrops=%llu; lateAudio=%llu; timestampFallbacks=%llu.",
                writer.finalPath().c_str(), ingress->sourceFrames.load(), sourceFps, ingress->maxSourceGapMicros.load() / 1000.0,
                frames, elapsedMilliseconds_.load() / 1000.0, averageEncodeMs, ingress->maxEncodeMicros.load() / 1000.0,
                ingress->slowEncodeFrames.load(), ingress->queueDrops.load(), ingress->repeatedFrames.load(), ingress->replacedFrames.load(),
                ingress->audioQueueDrops.load(), ingress->lateAudioFrames.load(), ingress->timestampFallbacks.load());
        } else {
            logLine(L"Replay encoder stopped after %llu CFR frames.", ingress->encodedFrames.load());
        }
        writer.closeEncoder();
        { std::lock_guard lock(ingress->mutex); ingress->frames.clear(); ingress->audio.clear(); }
        recording_.store(false);
        completion.event.reset(new RecordingEvent{RecordingEventKind::Finished, succeeded, std::move(finalMessage), elapsedMilliseconds_.load(),
            ingress->encodedFrames.load(), ingress->queueDrops.load(), ingress->repeatedFrames.load(), ingress->replacedFrames.load(), succeeded && !replayOnly ? writer.finalPath() : std::wstring{}});
    } catch (const std::exception& e) {
        if (failure.empty()) {
            const std::string message = e.what();
            failure = L"Recording worker error: " + std::wstring(message.begin(), message.end());
        }
    } catch (...) {
        failure = L"Unknown error in the recording worker.";
    }
    if (!failure.empty() && !completion.event) {
        writer.closeEncoder();
        writer.discardPartial(failure);
        { std::lock_guard lock(ingress->mutex); ingress->frames.clear(); ingress->audio.clear(); }
        recording_.store(false);
        logLine(L"Recording worker failed: %s", failure.c_str());
        completion.event.reset(new RecordingEvent{RecordingEventKind::Finished, false,
            L"Recording failed: " + failure, elapsedMilliseconds_.load(), ingress->encodedFrames.load(), ingress->queueDrops.load(),
            ingress->repeatedFrames.load(), ingress->replacedFrames.load()});
    }
#ifdef REDOT_UI_VERIFICATION
    Sleep(static_cast<DWORD>(std::min<uint64_t>(3000, diagnosticSetting(L"PANDACLIP_TEST_RECORD_CLEANUP_DELAY_MS"))));
#endif
    if (mfStarted) MFShutdown();
    if(ingress->audioTrace)ingress->audioTrace->finish();
    if (comStarted) CoUninitialize();
}


bool checkAudioTimelineAdjustments() {
    for (const auto source : {AudioSourceKind::Game, AudioSourceKind::Microphone}) for (int offset : {-500, -100, 0, 100, 500}) {
        AudioTimeline timeline; RecordingIngress ingress; RecordingAudioOptions options;
        options.gameSyncMilliseconds = source == AudioSourceKind::Game ? offset : 0;
        options.microphoneSyncMilliseconds = source == AudioSourceKind::Microphone ? offset : 0;
        const auto origin = std::chrono::steady_clock::now();
        RecordedAudio packet; packet.source = source; packet.capturedAt = origin + std::chrono::seconds(1); packet.stereo48k = {0.75f, 0.75f};
        timeline.ingest(packet, origin, ingress, options);
        std::vector<int16_t> mixed, game, microphone;
        timeline.build(0, 96000, options, mixed, game, microphone);
        const auto at = std::find_if(mixed.begin(), mixed.end(), [](int16_t value) { return value != 0; });
        const auto stem = source == AudioSourceKind::Game ? game : microphone;
        if (at == mixed.end() || (at - mixed.begin()) / 2 != 48000 + offset * 48 || mixed != stem || ingress.lateAudioFrames.load()) return false;
    }
    return true;
}

// Test-only entry point: known PCM cues through the production AAC MFT and mux path.
// No WASAPI, WGC, speaker scheduling, or NVENC influences this measurement.
bool writeAacDelayProbe(const std::wstring& path) {
    if (!checkAudioTimelineAdjustments()) return false;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
    if (FAILED(MFStartup(MF_VERSION))) { CoUninitialize(); return false; }
    bool ok = false;
    {
        ReplayAacEncoder encoder; std::wstring error;
        ComPtr<IMFSinkWriter> writer;
        HRESULT hr = encoder.open(error) ? MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer) : E_FAIL;
        DWORD stream{};
        if (SUCCEEDED(hr)) hr = writer->AddStream(encoder.outputType(), &stream);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(stream, encoder.outputType(), nullptr);
        if (SUCCEEDED(hr)) hr = writer->BeginWriting();
        std::vector<ReplayPacket> packets;
        for (uint64_t first = 0; SUCCEEDED(hr) && first < 48000 * 6; first += 1024) {
            std::vector<int16_t> pcm(2048);
            for (uint64_t i = 0; i < 1024; ++i) {
                const auto at = first + i;
                for (uint64_t cue : {48000ull, 144000ull, 240000ull}) if (at >= cue && at < cue + 8640) {
                    const auto n = at - cue;
                    const double envelope = std::min(1.0, std::min(n / 240.0, (8640 - n) / 240.0));
                    const auto value = static_cast<int16_t>(std::sin(n * 2 * 3.141592653589793 * 1000 / 48000) * 14000 * envelope);
                    pcm[i * 2] = pcm[i * 2 + 1] = value;
                }
            }
            if (!encoder.encode(pcm, first, ReplayTrack::Mix, packets, error)) hr = E_FAIL;
        }
        if (SUCCEEDED(hr) && !encoder.drain(ReplayTrack::Mix, packets, error)) hr = E_FAIL;
        for (const auto& packet : packets) {
            if (FAILED(hr)) break;
            ComPtr<IMFMediaBuffer> buffer; ComPtr<IMFSample> sample;
            hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.bytes.size()), &buffer);
            BYTE* data{};
            if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
            if (SUCCEEDED(hr)) { CopyMemory(data, packet.bytes.data(), packet.bytes.size()); buffer->Unlock(); hr = buffer->SetCurrentLength(static_cast<DWORD>(packet.bytes.size())); }
            if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
            if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
            if (SUCCEEDED(hr)) hr = sample->SetSampleTime(packet.pts);
            if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(packet.duration);
            if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.Get());
        }
        if (SUCCEEDED(hr)) hr = writer->Finalize();
        ok = SUCCEEDED(hr);
        logLine(L"AAC delay probe: expected PCM cues at 1000, 3000, 5000 ms; success=%d; %s", ok, error.c_str());
    }
    MFShutdown(); CoUninitialize(); return ok;
}

#ifdef REDOT_TEST_FIXTURE
// Test-only entry, absent from the shipping app. Encoding/muxing classes above
// are the same ones used by the recording worker. PCM never reaches a speaker.
bool writeQuietMediaFixture(std::shared_ptr<GpuResources> gpu,const std::wstring &folder,std::wstring &error,bool matroska) {
    bool ok=true;
    for(int mode=0;mode<4 && ok;++mode){
        auto directory=std::filesystem::path(folder)/(mode==0?L"video":mode==1?L"game":mode==2?L"mic":L"both");
        std::filesystem::create_directories(directory);
        auto replay=std::make_shared<ReplayBuffer>();replay->setEnabled(true);
        VideoConfig video;video.width=1280;video.height=720;video.fps=30;video.container=matroska?ContainerFormat::Mkv:ContainerFormat::Mp4; // replay exports stay MP4 either way
        RecordingAudioOptions audio;audio.gameMode=(mode==1||mode==3)?GameAudioMode::SelectedApplication:GameAudioMode::Off;
        audio.microphoneEnabled=mode==2||mode==3;
        NvencMp4Writer writer;ok=writer.open(gpu,directory.wstring(),RecordingQuality::Balanced,audio,error,replay,false,video);
        bool started=writer.mfStarted();ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=1280;desc.Height=720;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        if(ok)ok=SUCCEEDED(gpu->device->CreateTexture2D(&desc,nullptr,&texture))&&SUCCEEDED(gpu->device->CreateRenderTargetView(texture.Get(),nullptr,&target));
        // Quiet, deliberately colorful library fixtures, not gameplay samples.
        std::vector<uint32_t> poster(1280*720);
        for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x){
            unsigned r=(x*170/1280+mode*41)%230+25,g=(y*145/720+mode*61)%205+20,b=((x+y)*180/2000+mode*83)%205+30;
            bool tile=(x/110+y/110)%2==0;if(tile){r=std::min(255u,r+30);b=std::min(255u,b+25);}
            if(y>520){r/=3;g/=3;b/=3;}if((x-600)*(x-600)+(y-300)*(y-300)<110*110){r=240;g=185;b=55;}
            if(mode==0){r/=5;g/=5;b/=5;}
            poster[y*1280+x]=0xff000000|(r<<16)|(g<<8)|b;
        }
        for(uint64_t f=0;ok && f<180;++f){
            bool flash=(f>=30&&f<38)||(f>=90&&f<98)||(f>=150&&f<158);
            float color[]={flash?1.f:float(f%30)/90.f,flash?1.f:0.08f,flash?1.f:0.12f,1.f};
            if(flash)gpu->context->ClearRenderTargetView(target.Get(),color);else gpu->context->UpdateSubresource(texture.Get(),0,nullptr,poster.data(),1280*4,0);
            ok=writer.encode(texture,f,error);
            std::vector<int16_t> pcm(3200),mic(3200),mix(3200);
            for(uint64_t i=0;i<1600;++i){uint64_t at=f*1600+i;int16_t value=0;
                for(uint64_t cue:{48000ull,144000ull,240000ull})if(at>=cue&&at<cue+8640){uint64_t n=at-cue;double envelope=std::min(1.,std::min(n/240.,(8640-n)/240.));value=static_cast<int16_t>(std::sin(n*2*3.141592653589793*1000/48000)*10000*envelope);}
                pcm[i*2]=pcm[i*2+1]=audio.gameMode!=GameAudioMode::Off?value:0;
                mic[i*2]=mic[i*2+1]=audio.microphoneEnabled?value:0;
                mix[i*2]=mix[i*2+1]=static_cast<int16_t>(pcm[i*2]+mic[i*2]);
            }
            if(ok)ok=writer.writeAudio(f*1600,mix,pcm,mic,error);
        }
        if(ok)ok=writer.drainAudio(error)&&writer.finalize(error);
        if(ok){std::error_code ec;std::filesystem::rename(writer.partialPath(),writer.finalPath(),ec);ok=!ec;writer.saveTrackNames();}
        if(ok){replay->saveAsync(nullptr,directory.wstring(),5);while(replay->pendingSaves())Sleep(10);ok=replay->exportFailures==0;}
        if(!ok)writer.discardPartial(error);
        writer.closeEncoder();if(started)MFShutdown();
    }
    return ok;
}
#endif
