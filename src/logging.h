#pragma once

#include <windows.h>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <string>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cstdint>

namespace redot_logging {
// Producers only format/enqueue. Directory creation, cross-process log locking,
// and file I/O are owned by one worker; UI/capture/encoder threads never wait on disk.
class Writer {
public:
    static constexpr size_t Capacity = 256;
    Writer() {
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (count && count <= 32768) {
            std::wstring local(count, L'\0');
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), count)) {
                local.resize(wcsnlen_s(local.c_str(), local.size()));
                folder_ = std::filesystem::path(local) / L"PandaClip" / L"logs";
            }
        }
        wchar_t delay[32]{};
        GetEnvironmentVariableW(L"PANDACLIP_TEST_LOG_START_DELAY_MS", delay, 32);
        startDelayMs_ = static_cast<DWORD>(std::min<uint64_t>(3000, _wcstoui64(delay, nullptr, 10)));
        worker_ = std::thread([this] { run(); });
    }
    ~Writer() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        wake_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    bool enqueue(std::wstring line) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return false;
            if (lines_.size() == Capacity) {
                lines_.pop_front(); dropped_.fetch_add(1); ++unreportedDrops_;
            }
            lines_.push_back(std::move(line));
            pending_.store(lines_.size());
        }
        wake_.notify_one();
        return true;
    }
    uint64_t dropped() const noexcept { return dropped_.load(); }
    uint64_t errors() const noexcept { return errors_.load(); }
    uint64_t pending() const noexcept { return pending_.load(); }
private:
    void write(const std::wstring& line) noexcept {
        try {
            OutputDebugStringW((line + L"\n").c_str());
            if (folder_.empty()) { errors_.fetch_add(1); return; }
            std::error_code ec; std::filesystem::create_directories(folder_, ec);
            if (ec) { errors_.fetch_add(1); return; }
            const int count = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
            if (count <= 0) { errors_.fetch_add(1); return; }
            std::string utf8(count, '\0');
            if (!WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), count, nullptr, nullptr)) { errors_.fetch_add(1); return; }
            HANDLE logMutex = CreateMutexW(nullptr, FALSE, L"Local\\PandaClipDiagnosticLogMutex");
            struct Lock {
                HANDLE handle{}; bool acquired{};
                ~Lock() { if (handle) { if (acquired) ReleaseMutex(handle); CloseHandle(handle); } }
            } lock{logMutex};
            if (logMutex) {
                const DWORD wait = WaitForSingleObject(logMutex, 1000);
                lock.acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
                if (!lock.acquired) { errors_.fetch_add(1); return; }
            }
            std::ofstream stream(folder_ / L"panda.log", std::ios::binary | std::ios::app);
            stream.write(utf8.data(), count); stream.write("\r\n", 2); stream.close();
            if (stream.fail()) errors_.fetch_add(1);
        } catch (...) { errors_.fetch_add(1); }
    }
    void run() noexcept {
        if (startDelayMs_) Sleep(startDelayMs_); // bounded, test-only startup stall
        try {
        for (;;) {
            std::wstring line; uint64_t drops{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !lines_.empty(); });
                if (lines_.empty()) return;
                line = std::move(lines_.front()); lines_.pop_front();
                pending_.store(lines_.size());
                drops = unreportedDrops_; unreportedDrops_ = 0;
            }
            if (drops) write(L"Diagnostic log queue overflow: discarded " + std::to_wstring(drops) + L" oldest messages (capacity 256).");
            write(line);
        }
        } catch (...) {
            errors_.fetch_add(1);
            std::lock_guard lock(mutex_); stopping_ = true;
            dropped_.fetch_add(lines_.size()); lines_.clear(); pending_.store(0);
        }
    }
    std::filesystem::path folder_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::wstring> lines_;
    bool stopping_{};
    uint64_t unreportedDrops_{};
    std::atomic_uint64_t dropped_{0}, errors_{0}, pending_{0};
    DWORD startDelayMs_{};
    std::thread worker_;
};
inline Writer& writer() { static Writer instance; return instance; }
struct Stats { uint64_t pending{}, dropped{}, errors{}; };
inline Stats stats() noexcept {
    try { const auto& log = writer(); return {log.pending(), log.dropped(), log.errors()}; }
    catch (...) { return {0, 0, 1}; }
}
}

inline void logLine(const wchar_t* format, ...) noexcept {
    wchar_t message[2048]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[2304]{};
    swprintf_s(line, L"[%u-%u-%u %u:%u:%u] %s", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, message);
    try { if (!redot_logging::writer().enqueue(line)) OutputDebugStringW(line); }
    catch (...) { OutputDebugStringW(line); }
}
