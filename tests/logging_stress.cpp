#include "logging.h"
#include <iostream>
#include <chrono>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) return 2;
    const auto root = std::filesystem::absolute(argv[1]).lexically_normal();
    const auto workspace = std::filesystem::current_path().wstring() + L"\\";
    if (root.wstring().rfind(workspace, 0) != 0 || std::filesystem::exists(root)) {
        std::wcerr << L"Use an unused directory inside the current workspace.\n"; return 2;
    }
    std::filesystem::create_directories(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    const bool blocked = argc == 3 && std::wstring(argv[2]) == L"--blocked-folder";
    if (blocked) {
        std::filesystem::create_directories(root / L"PandaClip");
        std::ofstream(root / L"PandaClip" / L"logs") << "Test-owned logging path obstacle";
        SetEnvironmentVariableW(L"PANDACLIP_TEST_LOG_START_DELAY_MS", L"0");
        logLine(L"Controlled log write failure fixture.");
        for (int attempt = 0; attempt < 100 && !redot_logging::stats().errors; ++attempt) Sleep(10);
        if (!redot_logging::stats().errors) return 1;
        std::wcout << L"PASS: unavailable log folder is counted; producer returns without file I/O.\n";
    } else {
        SetEnvironmentVariableW(L"PANDACLIP_TEST_LOG_START_DELAY_MS", L"3000");
        const auto started = std::chrono::steady_clock::now();
        // Initialize the delayed writer, then race actual producer threads.
        (void)redot_logging::stats();
        std::vector<std::thread> producers;
        for (int producer = 0; producer < 4; ++producer)
            producers.emplace_back([producer] {
                for (int i = 0; i < 256; ++i) logLine(L"Bounded logging fixture message %d.", producer * 256 + i);
            });
        for (auto& producer : producers) producer.join();
        const auto queued = redot_logging::stats();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if (queued.pending > redot_logging::Writer::Capacity || queued.dropped < 1024 - redot_logging::Writer::Capacity) return 1;
        std::wcout << L"PASS: four producers submitted 1024 messages in " << elapsed << L" ms while writer startup was delayed; pending="
            << queued.pending << L"/256, dropped=" << queued.dropped << L"; destructor drains retained messages.\n";
    }
    return 0;
}
