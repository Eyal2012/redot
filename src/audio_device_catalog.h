#pragma once
#include "audio_capture.h"
#include "logging.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// One coalesced request and one result. Endpoint discovery never runs on the UI thread.
class AudioDeviceCatalog
{
  public:
    struct Result
    {
        std::vector<AudioDeviceInfo> inputs, outputs;
        std::wstring inputError, outputError;
    };
    AudioDeviceCatalog()
    {
        worker_ = std::thread([this] { run(); });
    }
    ~AudioDeviceCatalog()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            requested_ = false;
        }
        wake_.notify_one();
        worker_.join();
    }
    void request()
    {
        {
            std::lock_guard lock(mutex_);
            if (!stopping_)
                requested_ = true;
        }
        wake_.notify_one();
    }
    bool take(Result &value)
    {
        std::lock_guard lock(mutex_);
        if (!ready_)
            return false;
        value = std::move(result_);
        ready_ = false;
        return true;
    }
    bool busy()
    {
        std::lock_guard lock(mutex_);
        return working_ || requested_;
    }
    void cancel()
    {
        std::lock_guard lock(mutex_);
        requested_ = false;
    }

  private:
    void run()
    {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;)
        {
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return requested_ || stopping_; });
                if (stopping_)
                    break;
                requested_ = false;
                working_ = true;
            }
            auto started = std::chrono::steady_clock::now();
            Result value;
            if (SUCCEEDED(com))
            {
                value.inputs = enumerateMicrophones(value.inputError);
                value.outputs = enumerateAudioDevices(value.outputError, false);
            }
            else
                value.inputError = value.outputError = L"Windows audio-device discovery could not initialize COM.";
            logLine(L"Audio device catalog: background enumeration %.2f ms, inputs=%zu outputs=%zu.",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
                    value.inputs.size(), value.outputs.size());
            {
                std::lock_guard lock(mutex_);
                result_ = std::move(value);
                ready_ = true;
                working_ = false;
            }
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    Result result_;
    bool requested_{}, working_{}, ready_{}, stopping_{};
    std::thread worker_;
};
