#pragma once
#include "clip_track_names.h"
#include "mkv_writer.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <propvarutil.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
struct ClipThumb
{
    HBITMAP bitmap{};
    ~ClipThumb()
    {
        if (bitmap)
            DeleteObject(bitmap);
    }
};
struct ClipInfo
{
    std::wstring path, name, date, type, error;
    uint64_t bytes{}, stamp{};
    double duration{};
    UINT width{}, height{}, fpsNum{}, fpsDen{};
    std::shared_ptr<ClipThumb> thumbnail;
    std::vector<DWORD> audioStreams;
    std::vector<std::wstring> audioLabels;
    std::vector<unsigned> audioIds;
};
class ClipLibrary
{
  public:
    ClipLibrary()
    {
        worker_ = std::thread([this] { run(); });
    }
    ~ClipLibrary()
    {
        cancel();
        if (worker_.joinable())
            worker_.join();
    }
    void request(std::wstring folder)
    {
        std::lock_guard l(mutex_);
        folder_ = std::move(folder);
        pending_ = true;
        ready_ = false;
        updates_.clear();
        ++generation_;
        wake_.notify_one();
    }
    void upsert(const std::wstring &folder, const std::wstring &path)
    {
        std::lock_guard l(mutex_);
        if (stop_)
            return;
        if (folder_ != folder || loadedFolder_ != folder || busy_.load())
            pending_ = true;
        folder_ = folder;
        ready_ = false;
        if (updates_.size() < 64)
            updates_.push_back(path);
        else
            pending_ = true;
        ++generation_;
        wake_.notify_one();
    }
    void cancel()
    {
        std::lock_guard l(mutex_);
        stop_ = true;
        ++generation_;
        wake_.notify_one();
    }
    bool busy() const
    {
        return busy_.load();
    }
    bool take(std::vector<ClipInfo> &clips, std::wstring &status)
    {
        std::lock_guard l(mutex_);
        if (!ready_)
            return false;
        clips = std::move(result_);
        status = status_;
        ready_ = false;
        return true;
    }

  private:
    static void describe(const std::filesystem::path &path, std::vector<ClipInfo> &clips)
    {
        auto name = path.filename().wstring();
        // MP4 and MKV recordings/replays. Unfinished (.partial.*) files are active or awaiting recovery and
        // are never listed; recovered copies are ordinary finalized .mkv files.
        const bool mp4 = _wcsicmp(path.extension().c_str(), L".mp4") == 0, mkv = _wcsicmp(path.extension().c_str(), L".mkv") == 0;
        if ((!mp4 && !mkv) || name.find(L".partial.") != std::wstring::npos)
            return;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
            data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            return;
        ClipInfo clip;
        clip.path = path.wstring();
        clip.name = name;
        clip.type = (name.rfind(L"PandaClip_Clip_", 0) == 0 || name.rfind(L"ReDot_Clip_", 0) == 0) ? L"Replay"
                    : (name.rfind(L"PandaClip_", 0) == 0 || name.rfind(L"ReDot_", 0) == 0)    ? L"Recording"
                                                           : mkv ? L"MKV" : L"MP4";
        clip.bytes = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        clip.stamp = (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
        FILETIME local{};
        SYSTEMTIME st{};
        FileTimeToLocalFileTime(&data.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        wchar_t date[40]{};
        swprintf_s(date, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        clip.date = date;
        clips.push_back(std::move(clip));
    }
    static bool isMatroska(const std::wstring &path)
    {
        return _wcsicmp(std::filesystem::path(path).extension().c_str(), L".mkv") == 0;
    }
    // Narrow fallback for Matroska files Windows' source reader cannot open or cannot time (for example a
    // freshly recovered stream): read size/duration/track names straight from the container. No thumbnail.
    static bool inspectMatroskaDirectly(ClipInfo &clip)
    {
        const MkvInspection info = mkvInspect(clip.path);
        if (!info.valid)
            return false;
        clip.duration = info.durationSeconds;
        auto names = loadClipTrackNames(clip.path);
        size_t audioIndex = 0;
        for (const auto &track : info.tracks)
        {
            if (track.type == 1 && !clip.width)
            {
                clip.width = track.width;
                clip.height = track.height;
                clip.fpsNum = static_cast<UINT>(track.frameRate > 0 ? track.frameRate + 0.5 : 0);
                clip.fpsDen = clip.fpsNum ? 1 : 0;
            }
            else if (track.type == 2)
            {
                clip.audioStreams.push_back(static_cast<DWORD>(audioIndex));
                const auto named = names.find(static_cast<unsigned>(track.number));
                clip.audioLabels.push_back(named != names.end() ? named->second
                                           : !track.name.empty() ? settingsLabel(track.name)
                                                                 : L"Audio track " + std::to_wstring(audioIndex + 1));
                clip.audioIds.push_back(static_cast<unsigned>(track.number));
                ++audioIndex;
            }
        }
        return clip.width && clip.height;
    }
    static std::wstring settingsLabel(const std::string &utf8)
    {
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring label(n > 0 ? n : 0, L'\0');
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), label.data(), n);
        return label;
    }
    static void inspect(ClipInfo &clip, bool thumbnail)
    {
        ComPtr<IMFSourceReader> reader;
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 2)))
        {
            clip.error = L"Could not allocate metadata reader";
            return;
        }
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        HRESULT hr = MFCreateSourceReaderFromURL(clip.path.c_str(), attrs.Get(), &reader);
        if (FAILED(hr))
        {
            if (isMatroska(clip.path) && inspectMatroskaDirectly(clip))
                return;
            clip.error = L"Unreadable/damaged media file";
            return;
        }
        auto names = loadClipTrackNames(clip.path);
        // Windows' MP4 source renumbers stream identifiers and groups audio first.
        // Sidecar names describe our muxer's audio creation order (ascending MP4
        // track IDs), which is preserved within the reader's audio stream group.
        // Never equate GetStreamIdentifier or reader indexes with tkhd track IDs.
        for (DWORD stream = 0; stream < 32; stream++)
        {
            ComPtr<IMFMediaType> mt;
            if (FAILED(reader->GetNativeMediaType(stream, 0, &mt)))
                break;
            GUID major{};
            mt->GetGUID(MF_MT_MAJOR_TYPE, &major);
            if (major == MFMediaType_Audio)
                clip.audioStreams.push_back(stream);
        }
        auto name = names.begin();
        for (size_t i = 0; i < clip.audioStreams.size(); ++i)
        {
            if (names.size() == clip.audioStreams.size())
            {
                clip.audioLabels.push_back(name->second);
                clip.audioIds.push_back(name->first);
                ++name;
            }
            else
                clip.audioLabels.push_back(L"Audio track " + std::to_wstring(i + 1));
        }
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                       &value)) &&
            value.vt == VT_UI8)
            clip.duration = value.uhVal.QuadPart / 10000000.0;
        PropVariantClear(&value);
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)))
        {
            clip.audioStreams.clear();
            clip.audioLabels.clear();
            clip.audioIds.clear();
            if (isMatroska(clip.path) && inspectMatroskaDirectly(clip))
                return;
            clip.error = L"No video stream";
            return;
        }
        if (clip.duration <= 0 && isMatroska(clip.path))
            clip.duration = mkvInspect(clip.path).durationSeconds; // Windows could not time this Matroska stream
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &clip.width, &clip.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &clip.fpsNum, &clip.fpsDen);
        if (!thumbnail || !clip.width || !clip.height || clip.width > 8192 || clip.height > 8192)
            return;
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        ComPtr<IMFMediaType> rgb;
        MFCreateMediaType(&rgb);
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
                                               rgb.Get())))
            return;
        ComPtr<IMFSample> sample;
        DWORD flags{};
        LONGLONG time{};
        for (int i = 0; i < 8 && !sample; i++)
        {
            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                          &time, &sample)) ||
                (flags & MF_SOURCE_READERF_ENDOFSTREAM))
                return;
        }
        if (!sample)
            return;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
            return;
        BYTE *data{};
        DWORD length{};
        if (FAILED(buffer->Lock(&data, nullptr, &length)))
            return;
        if (length >= static_cast<uint64_t>(clip.width) * clip.height * 4)
        {
            BITMAPINFO input{};
            input.bmiHeader = {sizeof(BITMAPINFOHEADER),
                               static_cast<LONG>(clip.width),
                               -static_cast<LONG>(clip.height),
                               1,
                               32,
                               BI_RGB};
            BITMAPINFO thumbInfo{};
            thumbInfo.bmiHeader = {sizeof(BITMAPINFOHEADER), 320, -180, 1, 32, BI_RGB};
            void *pixels{};
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &thumbInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
            HGDIOBJ old = SelectObject(dc, bitmap);
            PatBlt(dc, 0, 0, 320, 180, BLACKNESS);
            SetStretchBltMode(dc, HALFTONE);
            const double scale = std::min(320.0 / clip.width, 180.0 / clip.height);
            int w = static_cast<int>(clip.width * scale), h = static_cast<int>(clip.height * scale);
            StretchDIBits(dc, (320 - w) / 2, (180 - h) / 2, w, h, 0, 0, clip.width, clip.height, data, &input,
                          DIB_RGB_COLORS, SRCCOPY);
            SelectObject(dc, old);
            DeleteDC(dc);
            clip.thumbnail = std::make_shared<ClipThumb>();
            clip.thumbnail->bitmap = bitmap;
        }
        buffer->Unlock();
    }
    void run()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION);
        for (;;)
        {
            std::wstring folder;
            std::vector<std::wstring> updates;
            bool fullScan{};
            uint64_t generation;
            {
                std::unique_lock l(mutex_);
                wake_.wait(l, [&] { return stop_ || pending_ || !updates_.empty(); });
                if (stop_)
                    break;
                folder = folder_;
                generation = generation_;
                fullScan = pending_;
                updates = std::move(updates_);
                updates_.clear();
                pending_ = false;
                busy_ = true;
            }
            std::vector<ClipInfo> clips;
            std::wstring status;
            try
            {
                if (fullScan)
                {
                    std::error_code error;
                    std::filesystem::directory_iterator iterator(folder, error);
                    if (error)
                        throw std::runtime_error("folder");
                    for (auto &file : iterator)
                    {
                        if (generation != generation_)
                            break;
                        if (clips.size() == 2000)
                        {
                            status = L"Showing first 2000 videos; choose a smaller output folder to browse more.";
                            break;
                        }
                        auto path = file.path();
                        describe(path, clips);
                    }
                }
                else
                {
                    for (auto &[path, clip] : cache_)
                        clips.push_back(clip);
                    for (auto &path : updates)
                    {
                        std::erase_if(clips, [&](auto &clip) { return clip.path == path; });
                        describe(path, clips);
                    }
                }
                std::sort(clips.begin(), clips.end(), [](auto &a, auto &b) { return a.stamp > b.stamp; });
                if (clips.size() > 2000)
                {
                    clips.resize(2000);
                    status = L"Showing newest 2000 files; choose a smaller folder for more.";
                }
                std::map<std::wstring, ClipInfo> nextCache;
                for (size_t i = 0; i < clips.size() && generation == generation_; ++i)
                {
                    auto &clip = clips[i];
                    auto old = cache_.find(clip.path);
                    if (old != cache_.end() && old->second.bytes == clip.bytes && old->second.stamp == clip.stamp &&
                        (i >= 256 || old->second.thumbnail || !old->second.error.empty()))
                        clip = old->second;
                    else
                        inspect(clip, i < 256);
                    if (i >= 256)
                        clip.thumbnail.reset();
                    nextCache.emplace(clip.path, clip);
                }
                cache_ = std::move(nextCache);
                if (status.empty())
                    status = std::to_wstring(clips.size()) +
                             L" video files. Select a clip to play.";
            }
            catch (...)
            {
                clips.clear();cache_.clear();
                status = L"Output folder is inaccessible. Choose an available folder and Refresh.";
            }
            {
                std::lock_guard l(mutex_);
                if (generation == generation_)
                {
                    result_ = std::move(clips);
                    status_ = std::move(status);
                    ready_ = true;
                    loadedFolder_ = folder;
                }
                busy_ = false;
            }
        }
        MFShutdown();
        CoUninitialize();
    }
    std::map<std::wstring, ClipInfo> cache_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::wstring folder_, status_, loadedFolder_;
    std::vector<std::wstring> updates_;
    std::vector<ClipInfo> result_;
    std::atomic_uint64_t generation_{};
    std::atomic_bool busy_{};
    bool pending_{}, stop_{}, ready_{};
};

