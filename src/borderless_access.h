#pragma once
#include <windows.h>
#include <appmodel.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <memory>
#include <mutex>
#include "logging.h"

constexpr UINT kBorderAccessMessage = WM_APP + 25;
enum class BorderPermission { Unrequested, Pending, Allowed, Denied, Unsupported, Failed, Cancelled };
struct BorderAccessResult {
    BorderPermission permission{BorderPermission::Unrequested};
    std::wstring text{L"Capture outline: permission has not been requested."};
};
// Pure policy is also exercised with simulated grant/deny decisions in the test target.
inline bool mayRequestNoBorder(BorderPermission permission) noexcept { return permission == BorderPermission::Allowed; }
template<class Session> bool applyBorderlessPolicy(BorderPermission permission, const Session& session) {
    if (!mayRequestNoBorder(permission)) return false;
    session.IsBorderRequired(false); return true;
}

class BorderlessAccess {
    struct State { std::mutex mutex; BorderAccessResult result; HWND notify{}; bool alive{true}; uint64_t generation{}; ULONGLONG started{}; };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus> operation_{nullptr};
public:
    ~BorderlessAccess() { cancel(); }
    BorderAccessResult result() const { std::lock_guard lock(state_->mutex); return state_->result; }
    void cancel() noexcept {
        { std::lock_guard lock(state_->mutex); state_->alive = false; ++state_->generation; state_->notify = nullptr; }
        try { if (operation_) operation_.Cancel(); } catch (...) {}
    }
    void cancelPending(const std::wstring& reason) noexcept {
        HWND notify{};
        { std::lock_guard lock(state_->mutex); if(state_->result.permission!=BorderPermission::Pending)return;
          ++state_->generation; state_->result={BorderPermission::Cancelled,reason}; notify=state_->notify; }
        try { if(operation_)operation_.Cancel(); } catch(...) {}
        if(notify)PostMessageW(notify,kBorderAccessMessage,0,0);
    }
    void tick() {
        bool expired{};
        { std::lock_guard lock(state_->mutex); expired=state_->result.permission==BorderPermission::Pending && GetTickCount64()-state_->started>=30000; }
        if(expired)cancelPending(L"Capture outline remains: Windows consent did not finish within 30 seconds. Continuing with the required outline. Retry permission in Capture settings.");
    }
#ifdef REDOT_UI_VERIFICATION
    void verificationResult(HWND notify, BorderPermission permission) {
        { std::lock_guard lock(state_->mutex); ++state_->generation; state_->notify=notify; state_->started=GetTickCount64();
          state_->result={permission,L"Verification-only consent outcome: "+std::to_wstring(static_cast<int>(permission))}; }
        if(permission!=BorderPermission::Pending)PostMessageW(notify,kBorderAccessMessage,0,0);
    }
#endif
    void request(HWND notify) {
        using namespace winrt::Windows::Foundation::Metadata;
        using namespace winrt::Windows::Graphics::Capture;
        using namespace winrt::Windows::Security::Authorization::AppCapabilityAccess;
        if (result().permission == BorderPermission::Pending) return;
        auto state = state_;
        uint64_t generation{};
        { std::lock_guard lock(state->mutex); generation=++state->generation; state->started=GetTickCount64(); state->notify = notify; state->result = {BorderPermission::Pending, L"Capture outline: waiting for Windows borderless consent..."}; }
        auto finish = [state,generation](BorderAccessResult result) {
            std::lock_guard lock(state->mutex);
            if (!state->alive || state->generation!=generation) return;
            state->result = std::move(result);
            logLine(L"Borderless permission: %s", state->result.text.c_str());
            PostMessageW(state->notify, kBorderAccessMessage, 0, 0);
        };
        try {
            if (!ApiInformation::IsMethodPresent(L"Windows.Graphics.Capture.GraphicsCaptureAccess", L"RequestAccessAsync") ||
                !ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
                finish({BorderPermission::Unsupported, L"Capture outline remains: this Windows version lacks borderless WGC support (build 20348+ required)."}); return;
            }
            UINT32 length{};
            const bool packaged = GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
            logLine(L"Requesting real GraphicsCaptureAccess Borderless consent; package identity=%d.", packaged);
            // Must be initiated on this foreground app's UI apartment; never .get() on the UI thread.
            operation_ = GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless);
            operation_.Completed([finish, packaged](auto const& operation, auto status) {
                try {
                    if(status==winrt::Windows::Foundation::AsyncStatus::Canceled){finish({BorderPermission::Cancelled,L"Capture outline remains: Windows borderless consent was cancelled."});return;}
                    const auto access = operation.GetResults();
                    if (access == AppCapabilityAccessStatus::Allowed)
                        finish({BorderPermission::Allowed, L"Borderless permission granted; requesting no outline. Windows may still show an outline for another capture."});
                    else {
                        const auto detail = access == AppCapabilityAccessStatus::DeniedByUser ? L"denied by user" :
                            access == AppCapabilityAccessStatus::DeniedBySystem ? L"denied by Windows/policy" :
                            access == AppCapabilityAccessStatus::NotDeclaredByApp ? L"package capability missing" : L"consent not granted";
                        finish({BorderPermission::Denied, std::wstring(L"Capture outline remains: ") + detail +
                            (packaged ? L". Check package graphicsCaptureWithoutBorder capability/Windows permission." :
                                L". This EXE has no package identity; use the MSIX manifest in packaging (see README).")});
                    }
                } catch (const winrt::hresult_error& e) {
                    wchar_t code[32]{}; swprintf_s(code, L"0x%08X", static_cast<unsigned>(e.code().value));
                    finish({BorderPermission::Failed, std::wstring(L"Capture outline remains: consent failed (") + code + L"). " +
                        (packaged ? L"Check package capability/Windows permission. " : L"Unpackaged EXE: MSIX package identity/capability may be required. ") + e.message().c_str()});
                } catch (...) { finish({BorderPermission::Failed, L"Capture outline remains: Windows borderless permission request failed."}); }
            });
        } catch (const winrt::hresult_error& e) {
            finish({BorderPermission::Failed, L"Capture outline remains: borderless consent could not start. Use the MSIX package/capability described in README. " + std::wstring(e.message())});
        } catch (...) { finish({BorderPermission::Failed, L"Capture outline remains: borderless consent could not start."}); }
    }
};
