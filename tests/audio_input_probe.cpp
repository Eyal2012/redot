#include "audio_capture.h"
#include "borderless_access.h"
#include <iostream>
#include <filesystem>
#include <mmdeviceapi.h>
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) return 2;
    const auto root = std::filesystem::absolute(argv[1]).lexically_normal();
    if (root.wstring().rfind(std::filesystem::current_path().wstring() + L"\\", 0) != 0 || std::filesystem::exists(root)) return 2;
    std::filesystem::create_directories(root); SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::wstring error; const auto devices = enumerateMicrophones(error);
    const auto defaultId = defaultAudioEndpointId(true);
    std::wcout << L"Default recording input: " << defaultId << L"; " << audioEndpointName(defaultId) << L"\n";
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator; Microsoft::WRL::ComPtr<IMMDevice> communications;
    LPWSTR communicationsId{};
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &communications)) && SUCCEEDED(communications->GetId(&communicationsId))) {
        std::wcout << L"Default communications input (used by the previous build): " << communicationsId << L"; " << audioEndpointName(communicationsId) << L"\n";
        CoTaskMemFree(communicationsId);
    }
    std::wcout << L"Enumeration error: " << error << L"; named active inputs=" << devices.size() << L"\n";
    for (const auto& device : devices) std::wcout << L"Named input: " << device.name << L"; id=" << device.id << L"\n";
    MicrophoneMonitor monitor;
    const auto testId = argc == 3 ? std::wstring(argv[2]) : defaultId;
    if (!monitor.start(testId, error)) { std::wcout << L"Open failed: " << error << L"\n"; return 1; }
    for (unsigned sample = 0; sample < 10 && monitor.busy(); ++sample) {
        Sleep(1000); const auto ingress = monitor.ingress(); std::lock_guard lock(ingress->mutex);
        std::wcout << L"t=" << sample + 1 << L"s; actual=" << ingress->audioEndpointNames[1] << L"; endpoint=" << ingress->audioEndpointIds[1]
            << L"; open=" << ingress->audioSourceOpen[1] << L"; Windows mute/volume=" << ingress->audioEndpointMuted[1] << L"/" << ingress->audioEndpointVolume[1]
            << L"; level=" << ingress->microphoneLevel.load() << L"; packets=" << ingress->audioPackets[1].load()
            << L"; input/silent/nonzero frames=" << ingress->audioInputFrames[1].load() << L"/" << ingress->audioSilentFrames[1].load() << L"/" << ingress->audioNonzeroFrames[1].load()
            << L"; queued PCM=" << ingress->audio.size() << L"\n";
        if (!ingress->audio.empty()) return 1;
    }
    monitor.requestStop(); while (monitor.busy()) Sleep(50);
    if (!monitor.error().empty()) { std::wcout << L"Open error: " << monitor.error() << L"\n"; return 1; }
    std::wcout << L"PASS: microphone opened and checked without a PCM queue; no listening/audible speech claim.\n";
}
