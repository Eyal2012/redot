#pragma once
// Historical service verifier only. Never included by PandaClip.
#include "clip_library.h"
#include <mfplay.h>
// MFPlay owns decoder/playback workers. It never creates a recording encoder.
class ClipPlayer : public IMFPMediaPlayerCallback
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMFPMediaPlayerCallback))
        {
            *out = static_cast<IMFPMediaPlayerCallback *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER *event) override
    {
        if (event->pMediaPlayer != player_.Get())
            return;
        if (FAILED(event->hrEvent))
        {
            error_.store(event->hrEvent);
            return;
        }
        if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED)
        {
            auto *e = reinterpret_cast<MFP_MEDIAITEM_CREATED_EVENT *>(event);
            for (auto stream : knownStreams_)
                e->pMediaItem->SetStreamSelection(stream, stream == defaultStream_);
            event->pMediaPlayer->SetMediaItem(e->pMediaItem);
        }
        if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET)
        {
            ready_ = true;
            SIZE native{}, ar{};
            if (SUCCEEDED(player_->GetNativeVideoSize(&native, &ar)) && ar.cx > 0 && ar.cy > 0)
                aspect_ = double(ar.cx) / ar.cy;
            player_->SetAspectRatioMode(MFVideoARMode_None);
            resize();
            if (resumeAt_.load() > 0)
            {
                PROPVARIANT p{};
                p.vt = VT_I8;
                p.hVal.QuadPart = resumeAt_.exchange(0);
                event->pMediaPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &p);
            }
            if (resumePlay_)
                event->pMediaPlayer->Play();
        }
    }
    HRESULT open(HWND pane, const std::wstring &path)
    {
        close();
        holder_ = pane;
        ready_ = false;
        error_ = S_OK;
        resumePlay_ = true;
        resumeAt_ = 0;
        aspect_ = 16.0 / 9;
        surface_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BLACKRECT, 0, 0, 16, 9, pane, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
        resize();
        HRESULT hr = MFPCreateMediaPlayer(nullptr, FALSE, 0, this, surface_, &player_);
        if (SUCCEEDED(hr)) {
            player_->SetAspectRatioMode(MFVideoARMode_None);
            wchar_t quiet[8]{};GetEnvironmentVariableW(L"PANDACLIP_TEST_QUIET_UI",quiet,8);
            if(quiet[0]==L'1')player_->SetMute(TRUE);
            hr=player_->CreateMediaItemFromURL(path.c_str(),FALSE,0,nullptr);
        }
        return hr;
    }
    void close()
    {
        if (player_)
        {
            auto old = std::move(player_);
            old->Shutdown();
        }
        if (surface_)
        {
            DestroyWindow(surface_);
            surface_ = nullptr;
        }
        ready_ = false;
        error_ = S_OK;
    }
    std::wstring geometry()
    {
        SIZE native{}, aspect{};
        if (player_)
            player_->GetNativeVideoSize(&native, &aspect);
        return std::to_wstring(native.cx) + L"x" + std::to_wstring(native.cy) + L" / display " +
               std::to_wstring(aspect.cx) + L"x" + std::to_wstring(aspect.cy);
    }
    void resize()
    {
        if (surface_ && holder_)
        {
            RECT r{};
            GetClientRect(holder_, &r);
            int w = std::max<int>(1, r.right), h = std::max<int>(1, r.bottom);
            if (double(w) / h > aspect_)
                w = static_cast<int>(h * aspect_);
            else
                h = static_cast<int>(w / aspect_);
            MoveWindow(surface_, (r.right - w) / 2, (r.bottom - h) / 2, std::max(1, w), std::max(1, h), TRUE);
        }
        if (player_)
            player_->UpdateVideo();
    }
    void toggle()
    {
        if (!player_)
            return;
        MFP_MEDIAPLAYER_STATE state{};
        player_->GetState(&state);
        if (state == MFP_MEDIAPLAYER_STATE_PLAYING)
            player_->Pause();
        else
            player_->Play();
    }
    void seek(double seconds)
    {
        if (!player_)
            return;
        PROPVARIANT p{};
        p.vt = VT_I8;
        p.hVal.QuadPart = static_cast<LONGLONG>(seconds * 10000000);
        player_->SetPosition(MFP_POSITIONTYPE_100NS, &p);
    }
    void volume(float v, bool mute)
    {
        if (player_)
        {
            player_->SetVolume(v);
            wchar_t quiet[8]{};GetEnvironmentVariableW(L"PANDACLIP_TEST_QUIET_UI",quiet,8);
            player_->SetMute(mute || quiet[0]==L'1');
        }
    }
    double position(bool duration = false)
    {
        PROPVARIANT p{};
        if (!player_)
            return 0;
        HRESULT hr = duration ? player_->GetDuration(MFP_POSITIONTYPE_100NS, &p)
                              : player_->GetPosition(MFP_POSITIONTYPE_100NS, &p);
        double result = SUCCEEDED(hr) ? p.hVal.QuadPart / 10000000.0 : 0;
        PropVariantClear(&p);
        return result;
    }
    std::vector<DWORD> tracks()
    {
        return knownStreams_;
    }
    void streamInfo(std::vector<DWORD> streams, const std::vector<std::wstring> &labels)
    {
        knownStreams_ = std::move(streams);
        defaultStream_ = knownStreams_.empty() ? DWORD(-1) : knownStreams_.front();
        for (size_t i = 0; i < labels.size(); i++)
            if (labels[i] == L"Mix")
                defaultStream_ = knownStreams_[i];
    }
    bool select(DWORD stream)
    {
        ComPtr<IMFPMediaItem> item;
        if (!player_ || FAILED(player_->GetMediaItem(&item)))
            return false;
        auto list = tracks();
        if (std::find(list.begin(), list.end(), stream) == list.end())
            return false;
        resumeAt_ = static_cast<LONGLONG>(position() * 10000000);
        MFP_MEDIAPLAYER_STATE state{};
        player_->GetState(&state);
        resumePlay_ = state == MFP_MEDIAPLAYER_STATE_PLAYING;
        player_->Pause();
        for (auto i : list)
            if (FAILED(item->SetStreamSelection(i, i == stream)))
                return false;
        return SUCCEEDED(player_->SetMediaItem(item.Get()));
    }
    bool ready() const
    {
        return ready_;
    }
    HRESULT error() const
    {
        return error_;
    }

  private:
    HWND holder_{}, surface_{};
    double aspect_{16.0 / 9};
    std::vector<DWORD> knownStreams_;
    DWORD defaultStream_{DWORD(-1)};
    std::atomic_ulong refs_{1};
    ComPtr<IMFPMediaPlayer> player_;
    std::atomic_bool ready_{}, resumePlay_{true};
    std::atomic_long error_{S_OK};
    std::atomic_llong resumeAt_{};
};
