// PandaClip opt-in playback checkpoints. Injected into the local Qt source
// build, not the SDK. No trace is produced unless a fresh build path is given.
#pragma once
#include <QtCore/qglobal.h>
#include <QtMultimedia/qaudioformat.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <sstream>
#include <audioclient.h>
#include <windows.h>

namespace PandaClipTrace {
struct Write {
    uint64_t client{}, tick{}, requested{}, accepted{}, frames{}, padding{}, clock{};
    double volume{};
    long result{};
    std::string kind{"render"};
    std::string format;
    std::vector<char> input, converted, submitted;
};
class Writer {
    static constexpr size_t Limit = 16 * 1024 * 1024;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Write> queue_;
    size_t queued_{};
    bool stop_{};
    std::atomic_bool enabled_{};
    std::thread worker_;
    std::filesystem::path folder_;
    std::atomic_uint64_t dropped_{}, written_{};
    std::ofstream csv_;
    void run() {
        // Path validation, creation and every disk operation run here, never
        // on Qt's UI/audio threads. Failure is an incomplete diagnostic run.
        std::error_code ec;
        const auto allowed = std::filesystem::weakly_canonical(std::filesystem::current_path() / L"build", ec);
        folder_ = std::filesystem::weakly_canonical(folder_, ec);
        const auto relative = folder_.lexically_relative(allowed);
        if (ec || relative.empty() || *relative.begin() == L".." || std::filesystem::exists(folder_) || !std::filesystem::create_directories(folder_, ec) || ec) { enabled_=false; return; }
        csv_.open(folder_ / L"writes.csv");
        csv_ << "kind,client,qpc_ticks,requested_bytes,accepted_bytes,output_frames,padding_frames,device_clock,volume,hresult\n";
        if (!csv_) { enabled_=false; return; }
        struct Streams { std::ofstream input, converted, submitted; };
        std::map<uint64_t, Streams> streams;
        for (;;) {
            Write w;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (queue_.empty()) break;
                w = std::move(queue_.front()); queue_.pop_front();
                queued_ -= w.input.size() + w.converted.size() + w.submitted.size();
            }
            if (written_ + w.input.size() + w.converted.size() + w.submitted.size() > 1024ull * 1024 * 1024) {
                ++dropped_; continue;
            }
            if (!w.format.empty()) {
                std::ofstream file(folder_ / (std::to_wstring(w.client) + L"-format.txt"));
                file << w.format;
                if (!file) ++dropped_;
                continue;
            }
            auto& files = streams[w.client];
            if (!files.input.is_open()) {
                const auto prefix = std::to_wstring(w.client);
                files.input.open(folder_ / (prefix + L"-B.pcm"), std::ios::binary);
                files.converted.open(folder_ / (prefix + L"-converted.pcm"), std::ios::binary);
                files.submitted.open(folder_ / (prefix + L"-C.pcm"), std::ios::binary);
            }
            files.input.write(w.input.data(), w.input.size());
            files.converted.write(w.converted.data(), w.converted.size());
            files.submitted.write(w.submitted.data(), w.submitted.size());
            csv_ << w.kind << ',' << w.client << ',' << w.tick << ',' << w.requested << ',' << w.accepted << ','
                 << w.frames << ',' << w.padding << ',' << w.clock << ',' << w.volume << ',' << w.result << '\n';
            if (!files.input || !files.converted || !files.submitted || !csv_) ++dropped_;
            written_ += w.input.size() + w.converted.size() + w.submitted.size();
        }
        csv_.flush();
        std::ofstream summary(folder_ / L"summary.txt");
        summary << "dropped_or_failed=" << dropped_ << "\npayload_bytes=" << written_ << '\n';
    }
public:
    Writer() {
        wchar_t text[32768]{};
        if (!GetEnvironmentVariableW(L"PANDACLIP_PLAYBACK_TRACE", text, 32768)) return;
        folder_ = text;
        enabled_ = true;
        worker_ = std::thread([this] { run(); });
    }
    ~Writer() { finish(); }
    void finish() {
        if (!worker_.joinable()) return;
        { std::lock_guard lock(mutex_); stop_ = true; }
        ready_.notify_one(); worker_.join();
        enabled_ = false;
    }
    bool enabled() const { return enabled_; }
    void format(uint64_t id, const QAudioFormat& input, const QAudioFormat& output, const wchar_t* device, uint64_t frequency, const char* renderer = "wasapi") {
        if (!enabled_) return;
        std::ostringstream file;
        LARGE_INTEGER qpc{}; QueryPerformanceFrequency(&qpc);
        file << "input_rate=" << input.sampleRate() << "\ninput_channels=" << input.channelCount()
             << "\ninput_type=" << int(input.sampleFormat()) << "\ninput_frame_bytes=" << input.bytesPerFrame()
             << "\noutput_rate=" << output.sampleRate() << "\noutput_channels=" << output.channelCount()
             << "\noutput_type=" << int(output.sampleFormat()) << "\noutput_frame_bytes=" << output.bytesPerFrame()
             << "\nrenderer=" << renderer << "\nclock_frequency=" << frequency << "\nqpc_frequency=" << qpc.QuadPart
             << "\ndevice=" << QString::fromWCharArray(device).toUtf8().constData() << '\n';
        Write w; w.client = id; w.format = file.str(); enqueue(std::move(w));
    }
    void enqueue(Write w) {
        if (!enabled_) return;
        const auto size = w.input.size() + w.converted.size() + w.submitted.size();
        std::lock_guard lock(mutex_);
        if (stop_) { ++dropped_; return; }
        if (size > Limit || queued_ + size > Limit || queue_.size() >= 8192) { ++dropped_; return; }
        queued_ += size; queue_.push_back(std::move(w)); ready_.notify_one();
    }
};
inline Writer& writer() { static Writer instance; return instance; }
inline uint64_t nextClient() { static std::atomic_uint64_t value{}; return ++value; }
inline uint64_t tick() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
inline void event(uint64_t id, const char* kind, HRESULT hr, uint64_t clock = 0, uint64_t padding = 0) {
    if (!writer().enabled()) return;
    Write w; w.client = id; w.tick = tick(); w.kind = kind; w.result = hr; w.clock = clock; w.padding = padding;
    writer().enqueue(std::move(w));
}
}
