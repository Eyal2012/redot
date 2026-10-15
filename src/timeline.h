#pragma once
#include <windows.h>
#include <chrono>
#include <cstdint>

// WGC SystemRelativeTime and WASAPI QPCPosition are both QPC in 100 ns units.
// Calibrate once instead of assuming a particular standard-library clock epoch.
inline std::chrono::steady_clock::time_point qpcTime(int64_t hns) {
    struct Mapping {
        int64_t qpc;
        std::chrono::steady_clock::time_point steady;
        Mapping() {
            LARGE_INTEGER count{}, frequency{};
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&count);
            steady = std::chrono::steady_clock::now();
            qpc = count.QuadPart / frequency.QuadPart * 10'000'000 +
                count.QuadPart % frequency.QuadPart * 10'000'000 / frequency.QuadPart;
        }
    };
    static const Mapping mapping;
    return mapping.steady + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::nanoseconds((hns - mapping.qpc) * 100));
}

inline uint64_t diagnosticSetting(const wchar_t* name) {
    wchar_t value[32]{};
    GetEnvironmentVariableW(name, value, 32);
    return _wcstoui64(value, nullptr, 10);
}
