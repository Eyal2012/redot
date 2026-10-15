#include "clip_library.h"
#include "legacy_clip_player.h"
#include "settings_store.h"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>

static bool pumpUntil(const std::function<bool()> &condition, unsigned milliseconds = 10000)
{
    const auto end = GetTickCount64() + milliseconds;
    do
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (condition())
            return true;
        Sleep(10);
    } while (GetTickCount64() < end);
    return false;
}
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: services-test finalized-folder isolated-localappdata\n";
        return 2;
    }
    SetEnvironmentVariableW(L"LOCALAPPDATA", argv[2]);
    SetEnvironmentVariableW(L"PANDACLIP_TEST_QUIET_UI",L"1");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);
    bool ok = true;
    auto check = [&](bool value, const wchar_t *name) {
        std::wcout << (value ? L"PASS " : L"FAIL ") << name << L"\n";
        ok = value && ok;
    };
    const auto settingsPath = std::filesystem::path(argv[2]) / L"PandaClip" / L"settings-v1.txt";
    {
        SettingsStore settings;
        settings.save({{"duration", L"37"}, {"sourceTitle", L"Unicode \u05e4\u05e0\u05d3\u05d4"}});
    }
    {
        SettingsStore settings;
        std::wstring warning;
        auto loaded = settings.load(warning);
        check(loaded["duration"] == L"37" && loaded["sourceTitle"] == L"Unicode \u05e4\u05e0\u05d3\u05d4" &&
                  warning.empty(),
              L"atomic UTF-8 settings roundtrip");
    }
    {
        std::ofstream file(settingsPath);
        file << "damaged settings";
    }
    {
        SettingsStore settings;
        std::wstring warning;
        check(settings.load(warning).empty() && !warning.empty(),
              L"damaged settings return safe defaults with warning");
    }
    ClipLibrary library;
    std::vector<ClipInfo> clips;
    std::wstring status;
    library.request(argv[1]);
    check(pumpUntil([&] { return library.take(clips, status); }, 30000), L"background gallery scan");
    auto found = std::find_if(clips.begin(), clips.end(), [](auto &c) {
        return c.audioLabels == std::vector<std::wstring>{L"Mix", L"Game", L"Microphone"};
    });
    for (auto &clip : clips)
    {
        ComPtr<IMFSourceReader> r;
        ComPtr<IMFMediaType> mt;
        UINT32 parN{}, parD{};
        if (SUCCEEDED(MFCreateSourceReaderFromURL(clip.path.c_str(), nullptr, &r)) &&
            SUCCEEDED(r->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &mt)))
            MFGetAttributeRatio(mt.Get(), MF_MT_PIXEL_ASPECT_RATIO, &parN, &parD);
        std::wcout << L"PAR=" << parN << L"/" << parD << L" ";
        std::wcout << clip.name << L": ";
        for (auto &label : clip.audioLabels)
            std::wcout << label << L" ";
        std::wcout << L"; thumbnail=" << bool(clip.thumbnail) << L"\n";
    }
    check(found != clips.end(), L"saved track layout gives Mix/Game/Microphone");
    HWND window = CreateWindowExW(0, L"STATIC", L"PandaClip owned playback verification", WS_OVERLAPPEDWINDOW, 30, 30,
                                  640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    // Hidden owned playback surface; the quiet policy mutes before starting MFPlay.
    ComPtr<ClipPlayer> player;
    player.Attach(new ClipPlayer);
    if (found != clips.end())
    {
        player->streamInfo(found->audioStreams, found->audioLabels);
        check(SUCCEEDED(player->open(window, found->path)) &&
                  pumpUntil([&] { return player->ready() && player->position() > 0.1; }),
              L"MFPlay async open and advancing playback");
        std::wcout << L"Playback geometry: " << player->geometry() << L"\n";
        player->toggle();
        pumpUntil([] { return false; }, 100);
        player->seek(2.0);
        check(pumpUntil([&] { return std::abs(player->position() - 2.0) < 0.08; }),
              L"seek paused playback to 2 seconds");
        for (auto stream : found->audioStreams)
        {
            bool selected = player->select(stream);
            pumpUntil([] { return false; }, 400);
            check(selected && std::abs(player->position() - 2.0) < 0.1,
                  L"switch audio track while preserving paused position");
        }
        player->toggle();
        check(pumpUntil([&] { return player->position() > 2.3; }), L"resume after track switch");
        player->volume(0.5f, true);
        player->close();
        auto missing = found->path + L".missing";
        HRESULT hr = player->open(window, missing);
        check(FAILED(hr) || pumpUntil([&] { return FAILED(player->error()); }),
              L"missing playback file produces error");
        player->close();
    }
    if(found!=clips.end()){
        auto sourcePath=found->path;auto fixture=std::filesystem::path(argv[2])/L"gallery-incremental";
        std::filesystem::create_directories(fixture);std::vector<ClipInfo> updated;std::wstring updateStatus;
        library.request(fixture.wstring());check(pumpUntil([&]{return library.take(updated,updateStatus);})&&updated.empty(),L"empty gallery fixture");
        auto saved=fixture/L"saved.mp4", external=fixture/L"external.mp4";
        std::filesystem::copy_file(sourcePath,saved,std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(sourcePath,external,std::filesystem::copy_options::overwrite_existing);
        library.upsert(fixture.wstring(),saved.wstring());
        check(pumpUntil([&]{return library.take(updated,updateStatus);})&&updated.size()==1&&updated.front().name==L"saved.mp4",L"finalized save adds one clip without scanning unrelated directory entries");
        library.request(fixture.wstring());check(pumpUntil([&]{return library.take(updated,updateStatus);})&&updated.size()==2,L"manual refresh discovers external files");
    }
    DestroyWindow(window);
    player.Reset();
    library.request((std::filesystem::path(argv[1]) / L"does-not-exist").wstring());
    check(pumpUntil([&] { return library.take(clips, status); }) && status.find(L"inaccessible") != std::wstring::npos,
          L"missing folder error");
    library.cancel();
    MFShutdown();
    CoUninitialize();
    return ok ? 0 : 1;
}
