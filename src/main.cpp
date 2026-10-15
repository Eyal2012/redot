#include <windows.h>
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windowsx.h>
#include <commctrl.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include "recording.h"
#include "timeline.h"
#include "diagnostics.h"
#include <wtsapi32.h>
#include <sstream>
#include <fstream>
#include "logging.h"
#include "audio_capture.h"
#include "audio_device_catalog.h"
#include "borderless_access.h"
#include "settings_store.h"
#include "clip_library.h"
#include "clip_sound.h"
#include "frontend_bridge.h"
#include "scene_runtime.h"
#include "scene_audio.h"
#include <uxtheme.h>
#include <dwmapi.h>
#ifdef REDOT_UI_VERIFICATION
#include "../tests/quiet_gpu.h"
#endif
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace WGC = winrt::Windows::Graphics::Capture;
namespace WDX = winrt::Windows::Graphics::DirectX;
namespace WDXD3D = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
constexpr wchar_t kClassName[] = L"PandaClipMainWindow";
constexpr UINT kFrameMessage = WM_APP + 10;
constexpr UINT kStartCaptureMessage = WM_APP + 11;
constexpr UINT kStopCaptureMessage = WM_APP + 12;
constexpr UINT kStartRecordingMessage = WM_APP + 13;
constexpr UINT kStopRecordingMessage = WM_APP + 14;
constexpr UINT_PTR kHealthTimer = 100;
constexpr int kWindowList = 1001;
constexpr int kStartButton = 1002;
constexpr int kStopButton = 1003;
constexpr int kRefreshButton = 1004;
constexpr int kStatusLabel = 1005;
constexpr int kPreviewPane = 1006;
constexpr int kChooseFolderButton = 1007;
constexpr int kOutputFolderLabel = 1008;
constexpr int kQualityCombo = 1009;
constexpr int kRecordButton = 1010;
constexpr int kStopRecordingButton = 1011;
constexpr int kRecordStatusLabel = 1012;
constexpr int kElapsedLabel = 1013;
constexpr int kGameAudioCombo = 1014;
constexpr int kMicrophoneEnable = 1015;
constexpr int kMicrophoneCombo = 1016;
constexpr int kGameMute = 1017;
constexpr int kMicrophoneMute = 1018;
constexpr int kGameVolume = 1019;
constexpr int kMicrophoneVolume = 1020;
constexpr int kGameMeter = 1021;
constexpr int kMicrophoneMeter = 1022;
constexpr int kReplayToggleButton = 1023;
constexpr int kReplaySaveButton = 1024;
constexpr int kReplayDurationCombo = 1025;
constexpr int kReplayStatusLabel = 1027;
constexpr int kReplayMemoryCombo = 1028;
constexpr int kDiagnosticsButton = 1029;
constexpr int kReportButton = 1030;
constexpr int kGameSyncEdit = 1031, kMicSyncEdit = 1032, kApplySync = 1033, kPreviewVisible = 1034;
constexpr UINT kReportEventMessage = WM_APP + 23;
constexpr int kBorderStatus = 1035, kBorderRequest = 1036, kMicCheck = 1037, kMicStatus = 1038, kGameStatus = 1039, kGameEnable = 1040;

std::wstring hresultText(HRESULT hr) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0,
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = count && buffer ? std::wstring(buffer, count) : L"Unknown error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

struct WindowEntry {
    HWND handle{};
    std::wstring title;
};

BOOL CALLBACK enumerateWindow(HWND hwnd, LPARAM parameter) {
    auto* windows = reinterpret_cast<std::vector<WindowEntry>*>(parameter);
    DWORD ownerPid{};GetWindowThreadProcessId(hwnd,&ownerPid);if(ownerPid==GetCurrentProcessId())return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (style & WS_EX_TOOLWINDOW) return TRUE;
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return TRUE;
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(wcsnlen_s(title.c_str(), title.size()));
    if (!title.empty()) windows->push_back({hwnd, std::move(title)});
    return TRUE;
}

class CaptureEngine {
public:
    ~CaptureEngine() { stop(); }
    bool cursorEnabled{};

    bool setBorderPermission(BorderPermission, std::wstring&) {return true;}
    const std::wstring& borderWarning() const noexcept {return borderWarning_;}

    bool initialize(HWND preview, std::wstring& error) {
        preview_ = preview;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = E_FAIL;
        ComPtr<IDXGIFactory1> enumFactory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&enumFactory)))) {
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                if (enumFactory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                if (FAILED(candidate->GetDesc1(&desc)) || desc.VendorId != 0x10DE) continue;
                hr = D3D11CreateDevice(candidate.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                    levels, _countof(levels), D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
                if (hr == E_INVALIDARG) hr = D3D11CreateDevice(candidate.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                    levels + 1, 1, D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
                if (SUCCEEDED(hr)) { logLine(L"Selected NVIDIA adapter for capture and NVENC."); break; }
            }
        }
        if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, _countof(levels), D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels + 1, 1, D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
        }
#if defined(_DEBUG)
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
            logLine(L"D3D11 debug layer unavailable; retrying hardware device without debug instrumentation.");
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels + 1, 1, D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
        }
#endif
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                levels + 1, 1, D3D11_SDK_VERSION, &d3dDevice_, &level, &immediate_);
            if (SUCCEEDED(hr)) logLine(L"Hardware D3D11 unavailable; using WARP software device.");
        }
        if (FAILED(hr)) return fail(error, L"Could not create a Direct3D 11 device", hr);

        ComPtr<ID3D11Multithread> safe; if(SUCCEEDED(immediate_.As(&safe)))safe->SetMultithreadProtected(TRUE);
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) return fail(error, L"Could not query the Direct3D DXGI device", hr);
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) return fail(error, L"Could not query the graphics adapter", hr);
        DXGI_ADAPTER_DESC adapterDescription{};
        hr = adapter->GetDesc(&adapterDescription);
        if (FAILED(hr)) return fail(error, L"Could not read the graphics adapter name", hr);
        gpuResources_ = std::make_shared<GpuResources>();
        gpuResources_->device = d3dDevice_;
        gpuResources_->context = immediate_;
        gpuResources_->adapterName = adapterDescription.Description;
        gpuResources_->adapterVendorId = adapterDescription.VendorId;
        logLine(L"Capture Direct3D adapter: %s (vendor 0x%04X).", adapterDescription.Description, adapterDescription.VendorId);
        ComPtr<IDXGIFactory2> factory;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return fail(error, L"Could not query the DXGI factory", hr);
        RECT rect{};
        GetClientRect(preview_, &rect);
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = std::max<LONG>(1, rect.right - rect.left);
        description.Height = std::max<LONG>(1, rect.bottom - rect.top);
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(d3dDevice_.Get(), preview_, &description, nullptr, nullptr, &swapChain_);
        if (FAILED(hr)) return fail(error, L"Could not create the preview swap chain", hr);
        static constexpr char shader[] = R"(
            cbuffer PreviewInfo : register(b0) { float4 dimensions; };
            Texture2D frameTexture : register(t0);
            SamplerState frameSampler : register(s0);
            struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
            VSOut VSMain(uint id : SV_VertexID) {
                VSOut output;
                output.uv = float2((id == 1) ? 2.0 : 0.0, (id == 2) ? 2.0 : 0.0);
                output.position = float4(output.uv.x * 2.0 - 1.0, 1.0 - output.uv.y * 2.0, 0.0, 1.0);
                return output;
            }
            float4 PSMain(VSOut input) : SV_TARGET {
                float sourceAspect = dimensions.x / dimensions.y;
                float targetAspect = dimensions.z / dimensions.w;
                float2 uv = input.uv;
                if (sourceAspect > targetAspect) {
                    float visible = targetAspect / sourceAspect;
                    if (uv.y < (1.0 - visible) * 0.5 || uv.y > 1.0 - (1.0 - visible) * 0.5) return float4(0.055, 0.063, 0.078, 1.0);
                    uv.y = (uv.y - (1.0 - visible) * 0.5) / visible;
                } else {
                    float visible = sourceAspect / targetAspect;
                    if (uv.x < (1.0 - visible) * 0.5 || uv.x > 1.0 - (1.0 - visible) * 0.5) return float4(0.055, 0.063, 0.078, 1.0);
                    uv.x = (uv.x - (1.0 - visible) * 0.5) / visible;
                }
                return frameTexture.Sample(frameSampler, uv);
            }
        )";
        ComPtr<ID3DBlob> vertexCode, pixelCode, compileErrors;
        hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vertexCode, &compileErrors);
        if (FAILED(hr)) return fail(error, L"Could not compile the preview vertex shader", hr);
        hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &pixelCode, &compileErrors);
        if (FAILED(hr)) return fail(error, L"Could not compile the preview pixel shader", hr);
        hr = d3dDevice_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_);
        if (FAILED(hr)) return fail(error, L"Could not create the preview vertex shader", hr);
        hr = d3dDevice_->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_);
        if (FAILED(hr)) return fail(error, L"Could not create the preview pixel shader", hr);
        D3D11_BUFFER_DESC constants{};
        constants.ByteWidth = sizeof(float) * 4;
        constants.Usage = D3D11_USAGE_DEFAULT;
        constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = d3dDevice_->CreateBuffer(&constants, nullptr, &previewInfo_);
        if (FAILED(hr)) return fail(error, L"Could not create preview shader parameters", hr);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        hr = d3dDevice_->CreateSamplerState(&sampler, &sampler_);
        if (FAILED(hr)) return fail(error, L"Could not create the preview texture sampler", hr);
        gpuResources_->vertexShader = vertexShader_;
        gpuResources_->pixelShader = pixelShader_;
        gpuResources_->constants = previewInfo_;
        gpuResources_->sampler = sampler_;
        if (!resizePreview(error)) return false;
        graphicsReady_ = true;
        return true;
    }

    void stop() noexcept {}
    bool active() const noexcept { return false; }
    bool targetClosed() const noexcept { return false; }
    bool faulted() const noexcept {return d3dDevice_&&FAILED(d3dDevice_->GetDeviceRemovedReason());}

    bool rebuildGraphics(std::wstring& error) {
        stop(); graphicsReady_ = false;
        renderTarget_.Reset(); swapChain_.Reset(); vertexShader_.Reset(); pixelShader_.Reset();
        previewInfo_.Reset(); sampler_.Reset(); immediate_.Reset(); d3dDevice_.Reset(); gpuResources_.reset();
        return initialize(preview_, error);
    }
    bool imageReady() {return sceneFrame&&bool(sceneFrame().texture);}
    HWND target() const noexcept {return nullptr;}
    uint64_t publishedFrames() const noexcept {return 0;}
    void setNotificationWindow(HWND window) {notificationWindow_=window;}
    std::shared_ptr<GpuResources> gpuResources() const {return gpuResources_;}

    bool render(std::wstring& error) {
        if (!swapChain_ || !renderTarget_) return true;
        ComPtr<ID3D11Texture2D> source;
        
        RecordedFrame composed;
        if (sceneFrame) { composed=sceneFrame();source=composed.texture; }
        std::unique_lock contextLock(*gpuResources_->contextMutex);
        D3D11_TEXTURE2D_DESC targetDescription{};
        ComPtr<ID3D11Texture2D> target;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&target));
        if (FAILED(hr)) return fail(error, L"Could not access the preview surface", hr);
        target->GetDesc(&targetDescription);
        const FLOAT background[] = {0.055f, 0.063f, 0.078f, 1.0f};
        immediate_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        immediate_->ClearRenderTargetView(renderTarget_.Get(), background);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(targetDescription.Width), static_cast<float>(targetDescription.Height), 0.0f, 1.0f};
        immediate_->RSSetViewports(1, &viewport);
        if (source) {
            D3D11_TEXTURE2D_DESC sourceDescription{};
            source->GetDesc(&sourceDescription);
            ComPtr<ID3D11ShaderResourceView> sourceView;
            hr = d3dDevice_->CreateShaderResourceView(source.Get(), nullptr, &sourceView);
            if (FAILED(hr)) return fail(error, L"Could not create a GPU view of the captured frame", hr);
            const float dimensions[] = {static_cast<float>(sourceDescription.Width), static_cast<float>(sourceDescription.Height),
                static_cast<float>(targetDescription.Width), static_cast<float>(targetDescription.Height)};
            immediate_->UpdateSubresource(previewInfo_.Get(), 0, nullptr, dimensions, 0, 0);
            ID3D11Buffer* constantBuffer = previewInfo_.Get();
            ID3D11ShaderResourceView* textureView = sourceView.Get();
            ID3D11SamplerState* textureSampler = sampler_.Get();
            immediate_->VSSetShader(vertexShader_.Get(), nullptr, 0);
            immediate_->PSSetShader(pixelShader_.Get(), nullptr, 0);
            immediate_->PSSetConstantBuffers(0, 1, &constantBuffer);
            immediate_->PSSetShaderResources(0, 1, &textureView);
            immediate_->PSSetSamplers(0, 1, &textureSampler);
            immediate_->IASetInputLayout(nullptr);immediate_->OMSetBlendState(nullptr,nullptr,~0u);immediate_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            immediate_->Draw(3, 0);
            ID3D11ShaderResourceView* noView = nullptr;
            immediate_->PSSetShaderResources(0, 1, &noView);
        }
        // Present can wait for vblank. The shared immediate context is no longer
        // being mutated here; do not hold capture/encoder work behind that wait.
        contextLock.unlock();
        hr = swapChain_->Present(1, 0);
        if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) return fail(error, L"Could not present the live preview", hr);
        return true;
    }

    bool resizePreview(std::wstring& error) {
        if (!swapChain_) return true;
        std::lock_guard contextLock(*gpuResources_->contextMutex);
        renderTarget_.Reset();
        RECT rect{};
        GetClientRect(preview_, &rect);
        const UINT width = static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top));
        HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return fail(error, L"Could not resize the preview", hr);
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) return fail(error, L"Could not access the resized preview surface", hr);
        hr = d3dDevice_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
        if (FAILED(hr)) return fail(error, L"Could not create the resized preview target", hr);
        return true;
    }

public:
    std::function<RecordedFrame()> sceneFrame;
private:
    bool fail(std::wstring& error, const wchar_t* action, HRESULT hr) {
        error = std::wstring(action) + L": " + hresultText(hr) + L" (0x";
        wchar_t code[16]{};
        swprintf_s(code, L"%08X", static_cast<unsigned>(hr));
        error += code; error += L")";
        logLine(L"%s, HRESULT 0x%08X: %s", action, static_cast<unsigned>(hr), error.c_str());
        return false;
    }

    HWND preview_{};
    bool graphicsReady_{};
    BorderPermission borderPermission_{BorderPermission::Unrequested};
    std::wstring borderWarning_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> immediate_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11Buffer> previewInfo_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    #ifdef REDOT_UI_VERIFICATION
    #include "../tests/quiet_preview.inl"
    #endif
    HWND notificationWindow_{};
    std::shared_ptr<GpuResources> gpuResources_;
};

enum class RecoveryPhase { None, Draining, Suspended, WaitingTarget, RetryDelay, Restarting, Exhausted };
enum class CaptureAction { None, Preview, Recording, Replay };
class App {
    friend class FrontendController;
    HWND externalPreview_{};
    uint64_t frontendLibraryRevision_{};
    std::function<void()> frontendVisibility_;
    std::function<void(const std::wstring &)> frontendNotice_;
public:
    App() : replayStore_(std::make_shared<ReplayBuffer>()) {}
    ~App() { mkvRecoveryCancel_ = true; if (mkvRecoveryThread_.joinable()) mkvRecoveryThread_.join(); for(auto icon:stateIcons_)if(icon)DestroyIcon(icon); }
    bool create(HINSTANCE instance) {
        instance_ = instance;
        WNDCLASSW paneClass{};
        paneClass.lpfnWndProc = paneProcedure;
        paneClass.hInstance = instance;
        paneClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        paneClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        paneClass.lpszClassName = L"PandaClipPreviewPane";
        RegisterClassW(&paneClass);
        WNDCLASSW mainClass{};
        mainClass.lpfnWndProc = mainProcedure;
        mainClass.hInstance = instance;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        mainClass.lpszClassName = kClassName;
        mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassW(&mainClass);
        WNDCLASSW diagnosticsClass = mainClass;
        diagnosticsClass.lpfnWndProc = diagnosticsProcedure;
        diagnosticsClass.lpszClassName = L"PandaClipDiagnostics";
        RegisterClassW(&diagnosticsClass);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
        InitCommonControlsEx(&controls);
        hwnd_ = CreateWindowExW(0, kClassName, L"ReDot - Scene Preview", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 1100, 820, nullptr, nullptr, instance, this);
        if (!hwnd_) return false;
        return true;
    }

private:
    static LRESULT CALLBACK mainProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        }
        return app ? app->handle(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK paneProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) { PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); if (app) app->draw(); return 0; }
        if (message == WM_SIZE && app) { app->resize(); return 0; }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK diagnosticsProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app && message == WM_COMMAND) {
            if (LOWORD(wParam) == kReportButton) { app->exportDiagnostics(); return 0; }
            if (LOWORD(wParam) == kApplySync) { app->applySync(); return 0; }
            if (LOWORD(wParam) == kPreviewVisible) {
                app->previewVisible_ = SendMessageW(GetDlgItem(hwnd, kPreviewVisible), BM_GETCHECK, 0, 0) == BST_CHECKED;
                ShowWindow(app->preview_, app->previewVisible_ ? SW_SHOW : SW_HIDE); app->draw(); return 0;
            }
        }
        if (message == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void showDiagnostics() {
        if (!diagnostics_) {
            diagnostics_ = CreateWindowExW(0, L"PandaClipDiagnostics", L"ReDot Diagnostics and Sync", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 860, 540, hwnd_, nullptr, instance_, this);
            diagnosticsText_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                12, 12, 816, 365, diagnostics_, nullptr, instance_, nullptr);
            auto control = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int id) {
                return CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, 25, diagnostics_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
            };
            control(L"STATIC", L"Game sync (ms)", 0, 12, 389, 110, 0);
            control(L"STATIC", L"Mic sync (ms)", 0, 225, 389, 105, 0);
            control(L"EDIT", std::to_wstring(gameSyncMilliseconds_).c_str(), WS_BORDER | ES_AUTOHSCROLL, 128, 385, 80, kGameSyncEdit);
            control(L"EDIT", std::to_wstring(microphoneSyncMilliseconds_).c_str(), WS_BORDER | ES_AUTOHSCROLL, 335, 385, 80, kMicSyncEdit);
            control(L"BUTTON", L"Apply sync", BS_PUSHBUTTON, 431, 385, 100, kApplySync);
            control(L"BUTTON", L"Render preview", BS_AUTOCHECKBOX, 553, 385, 145, kPreviewVisible);
            SendMessageW(GetDlgItem(diagnostics_, kPreviewVisible), BM_SETCHECK, previewVisible_ ? BST_CHECKED : BST_UNCHECKED, 0);
            control(L"STATIC", L"Positive delays audio; negative advances. Range -500 to +500 ms. Apply between sessions.", 0, 12, 420, 812, 0);
            control(L"BUTTON", L"Export report", BS_PUSHBUTTON, 12, 455, 145, kReportButton);
            diagnosticsStatus_ = control(L"STATIC", L"Reports go to the selected output folder.", 0, 175, 459, 650, 0);
        }
        // Hidden diagnostics state is read by the Qt view; never show a native frontend.
        updateDiagnostics();
    }

    std::wstring diagnosticsReport() {
        auto input = recorder_.ingress();
        if (!input) input = micMonitor_.busy() ? micMonitor_.ingress() : lastDiagnostics_;
        const wchar_t* phases[] = {L"none", L"draining", L"suspended", L"waiting target", L"retry delay", L"restarting", L"exhausted"};
        std::wstring sceneReport;if(sceneRuntime_){auto d=sceneRuntime_->status();sceneReport=L"Scene compositor: frames="+std::to_wstring(d.frames)+L", missed composition slots="+std::to_wstring(d.lateSlots)+L", pool drops="+std::to_wstring(d.poolDrops)+L", CPU mean/max ms="+std::to_wstring(d.composeMeanMs)+L"/"+std::to_wstring(d.composeMaxMs)+L", GPU mean/max ms="+std::to_wstring(d.gpuMeanMs)+L"/"+std::to_wstring(d.gpuMaxMs)+L", owned GPU bytes="+std::to_wstring(d.gpuBytes)+L"\r\nLive source frames/drops="+std::to_wstring(d.sourceFrames)+L"/"+std::to_wstring(d.sourceDrops)+L"\r\n";for(auto it=d.sources.cbegin();it!=d.sources.cend();++it)sceneReport+=it.key().toStdWString()+L": "+it.value().toStdWString()+L"\r\n";}
        return sceneReport+pipelineReport(input, *replayStore_, recorder_.elapsedMilliseconds(), previewVisible_ && !IsIconic(hwnd_),
            (recoveryPending_ || recoveryPhase_ == RecoveryPhase::Exhausted) ? std::wstring(phases[static_cast<unsigned>(recoveryPhase_)]) + L": " + recoveryText_ : recorder_.busy() ? (replayWorkerOnly_ ? L"Replay alone" : replayEnabled_ ? L"Recording + replay" : L"Recording alone") : L"Idle / last session",
            failures_, failureHistory_) + L"Game/mic user sync adjustment: " + std::to_wstring(gameSyncMilliseconds_) + L"/" + std::to_wstring(microphoneSyncMilliseconds_) + L" ms\r\n";
    }
    void updateDiagnostics() {
        if (!diagnostics_ || !IsWindowVisible(diagnostics_)) return;
        SetWindowTextW(diagnosticsText_, diagnosticsReport().c_str());
        for (int id : {kGameSyncEdit, kMicSyncEdit, kApplySync}) EnableWindow(GetDlgItem(diagnostics_, id), !recorder_.busy() && !recoveryPending_);
    }
    void applySync() {
        if (recorder_.busy() || recoveryPending_) return;
        auto value = [&](int id, int& result) {
            wchar_t text[32]{}; GetWindowTextW(GetDlgItem(diagnostics_, id), text, 32); wchar_t* end{};
            const long n = wcstol(text, &end, 10);
            if (end == text || *end || n < -500 || n > 500) return false;
            result = static_cast<int>(n); return true;
        };
        int game{}, mic{};
        if (!value(kGameSyncEdit, game) || !value(kMicSyncEdit, mic)) { SetWindowTextW(diagnosticsStatus_, L"Enter whole milliseconds from -500 to +500."); return; }
        gameSyncMilliseconds_ = game; microphoneSyncMilliseconds_ = mic;
        replayStore_->clearHistory();
        SetWindowTextW(diagnosticsStatus_, L"Sync applied to the next session; replay history reset. Default is zero.");
        logLine(L"User audio-sync adjustment: game=%d ms, microphone=%d ms.", game, mic);
    }
    void exportDiagnostics() {
        if (reportPending_) { SetWindowTextW(diagnosticsStatus_, L"Report export already pending."); return; }
        reportPending_ = true;
        const auto report = diagnosticsReport(); const auto folder = outputFolder_; const HWND notify = hwnd_;
        try {
            std::thread([report, folder, notify] {
                bool saved = false;
                std::wstring result = L"Report export failed. Check the output folder and permissions.";
                try {
                    std::error_code ec;
                    auto path = std::filesystem::path(folder) / (L"ReDot_Diagnostics_" + std::to_wstring(GetTickCount64()) + L".txt");
                    const auto base = path.stem().wstring();
                    for (unsigned suffix = 1; std::filesystem::exists(path); ++suffix)
                        path = std::filesystem::path(folder) / (base + L"_" + std::to_wstring(suffix) + L".txt");
                    std::filesystem::create_directories(path.parent_path(), ec);
                    if (!ec) {
                        std::ofstream file(path, std::ios::binary);
                        const int count = WideCharToMultiByte(CP_UTF8, 0, report.data(), static_cast<int>(report.size()), nullptr, 0, nullptr, nullptr);
                        std::string utf8(count, '\0'); WideCharToMultiByte(CP_UTF8, 0, report.data(), static_cast<int>(report.size()), utf8.data(), count, nullptr, nullptr);
                        file.write(utf8.data(), count); file.close();
                        saved = count > 0 && !file.fail();
                        if (saved) result = L"Report saved: " + path.wstring();
                    }
                } catch (...) { result = L"Report export failed while allocating or writing. Capture continues."; }
                auto* event = new ReplayEvent{saved, std::move(result)};
                if (!PostMessageW(notify, kReportEventMessage, 0, reinterpret_cast<LPARAM>(event))) delete event;
            }).detach();
        } catch (...) {
            reportPending_ = false;
            SetWindowTextW(diagnosticsStatus_, L"Could not start the diagnostics export worker. Capture continues.");
        }
    }
    void finishClosing() {
        #ifdef REDOT_UI_VERIFICATION
        if(quietProducerBusy_)return;
        #endif
        mkvRecoveryCancel_ = true; // a long repair copy stops at the next frame; the original stays untouched
        if (mkvRecoveryThread_.joinable() && mkvRecoveryFinished_) mkvRecoveryThread_.join();
        if (!recorder_.busy() && !micMonitor_.busy() && !mkvRecoveryThread_.joinable() && (!sceneRuntime_||!sceneRuntime_->busy()) && !replayStore_->pendingSaves() && !reportPending_ && !library_.busy() && !settingsStore_.busy() && !deviceCatalog_.busy()) DestroyWindow(hwnd_);
        else {
            setStatus(L"Closing after the recording, clip exports, and diagnostics finish finalizing...");
            if (closing_ && std::chrono::steady_clock::now()-shutdownStarted_>std::chrono::seconds(30)) {
                shutdownStarted_=std::chrono::steady_clock::now();
                if(MessageBoxW(hwnd_,L"Windows or a device is taking over 30 seconds to finish. Exit now? The current file/export may remain incomplete; it will not be reported Saved. Previously saved files are kept. Choose No to keep waiting.",L"ReDot shutdown stalled",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES)TerminateProcess(GetCurrentProcess(),2);
            }
        }
    }
    void noteFailure(const std::wstring& error) {
        ++failures_; if (failureHistory_.size() == 16) failureHistory_.pop_front(); failureHistory_.push_back(error); logLine(L"Lifecycle failure: %s", error.c_str());
    }

    void createControls() {
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        combo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 420, 400, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowList)), instance_, nullptr);
        start_ = CreateWindowW(L"BUTTON", L"Start Preview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 110, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartButton)), instance_, nullptr);
        stop_ = CreateWindowW(L"BUTTON", L"Stop Preview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 74, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopButton)), instance_, nullptr);
        refresh_ = CreateWindowW(L"BUTTON", L"Refresh Windows", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 130, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButton)), instance_, nullptr);
        diagnosticsButton_ = CreateWindowW(L"BUTTON", L"Diagnostics...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 130, 26, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsButton)), instance_, nullptr);
        status_ = CreateWindowW(L"STATIC", L"Ready. Select a window and start preview.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 500, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)), instance_, nullptr);
        borderStatus_ = CreateWindowW(L"STATIC", L"Capture outline: Windows consent will be requested before first capture.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 800, 38, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBorderStatus)), instance_, nullptr);
        borderRequest_ = CreateWindowW(L"BUTTON", L"Request borderless", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 146, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBorderRequest)), instance_, nullptr);
        chooseFolder_ = CreateWindowW(L"BUTTON", L"Output Folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 120, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChooseFolderButton)), instance_, nullptr);
        folderLabel_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
            0, 0, 600, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOutputFolderLabel)), instance_, nullptr);
        quality_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            0, 0, 170, 120, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQualityCombo)), instance_, nullptr);
        record_ = CreateWindowW(L"BUTTON", L"Start Recording", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 132, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecordButton)), instance_, nullptr);
        stopRecording_ = CreateWindowW(L"BUTTON", L"Stop Recording", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 132, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopRecordingButton)), instance_, nullptr);
        elapsed_ = CreateWindowW(L"STATIC", L"00:00:00", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 90, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kElapsedLabel)), instance_, nullptr);
        recordStatus_ = CreateWindowW(L"STATIC", L"Audio: choose app/system loopback and/or microphone. Audio is not monitored.", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL,
            0, 0, 520, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecordStatusLabel)), instance_, nullptr);
        replayToggle_ = CreateWindowW(L"BUTTON", L"Enable Replay Buffer", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 150, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplayToggleButton)), instance_, nullptr);
        replaySave_ = CreateWindowW(L"BUTTON", L"Save Clip", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 90, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplaySaveButton)), instance_, nullptr);
        replayDuration_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            0, 0, 100, 120, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplayDurationCombo)), instance_, nullptr);

        replayStatus_ = CreateWindowW(L"STATIC", L"Replay buffer disabled.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 400, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplayStatusLabel)), instance_, nullptr);
        replayMemory_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            0, 0, 100, 120, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplayMemoryCombo)), instance_, nullptr);
        gameAudio_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            0, 0, 220, 130, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameAudioCombo)), instance_, nullptr);
        gameEnable_ = CreateWindowW(L"BUTTON", L"Game audio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 110, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameEnable)), instance_, nullptr);
        SendMessageW(gameEnable_, BM_SETCHECK, BST_CHECKED, 0);
        gameSourceStatus_ = CreateWindowW(L"STATIC", L"Game: selected app audio; choose the capture window above.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 800, 36, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameStatus)), instance_, nullptr);
        micSourceStatus_ = CreateWindowW(L"STATIC", L"Mic disabled for recording. Choose an input and click Check mic before recording.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 800, 44, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicStatus)), instance_, nullptr);
        micCheck_ = CreateWindowW(L"BUTTON", L"Check mic", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 120, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicCheck)), instance_, nullptr);
        microphoneEnable_ = CreateWindowW(L"BUTTON", L"Microphone", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 105, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicrophoneEnable)), instance_, nullptr);
        microphone_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 260, 160, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicrophoneCombo)), instance_, nullptr);
        gameMute_ = CreateWindowW(L"BUTTON", L"Mute game", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 90, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameMute)), instance_, nullptr);
        microphoneMute_ = CreateWindowW(L"BUTTON", L"Mute mic", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 82, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicrophoneMute)), instance_, nullptr);
        gameVolumeLabel_ = CreateWindowW(L"STATIC", L"Game vol.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 60, 24, hwnd_, nullptr, instance_, nullptr);
        microphoneVolumeLabel_ = CreateWindowW(L"STATIC", L"Mic vol.", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 60, 24, hwnd_, nullptr, instance_, nullptr);
        gameMeterLabel_ = CreateWindowW(L"STATIC", L"Game level", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 74, 20, hwnd_, nullptr, instance_, nullptr);
        microphoneMeterLabel_ = CreateWindowW(L"STATIC", L"Mic level", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 74, 20, hwnd_, nullptr, instance_, nullptr);
        gameVolume_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
            0, 0, 140, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameVolume)), instance_, nullptr);
        microphoneVolume_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
            0, 0, 140, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicrophoneVolume)), instance_, nullptr);
        gameMeter_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
            0, 0, 120, 16, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameMeter)), instance_, nullptr);
        microphoneMeter_ = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
            0, 0, 120, 16, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMicrophoneMeter)), instance_, nullptr);
        preview_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"PandaClipPreviewPane", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewPane)), instance_, this);
        for (HWND control : {combo_, start_, stop_, refresh_, status_, chooseFolder_, folderLabel_, quality_, record_, stopRecording_, elapsed_, recordStatus_, replayToggle_, replaySave_, replayDuration_, replayStatus_, replayMemory_,
                borderStatus_, borderRequest_, gameEnable_, gameSourceStatus_, micSourceStatus_, micCheck_,
                gameAudio_, microphoneEnable_, microphone_, gameMute_, microphoneMute_, gameVolume_, microphoneVolume_,
                gameVolumeLabel_, microphoneVolumeLabel_, gameMeterLabel_, microphoneMeterLabel_})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(quality_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Balanced - P4"));
        SendMessageW(quality_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"High Quality - P6"));
        SendMessageW(quality_, CB_SETCURSEL, 0, 0);
        SendMessageW(gameAudio_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Game audio: Off"));
        SendMessageW(gameAudio_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Game audio: Selected app"));
        SendMessageW(gameAudio_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Game audio: Whole system (explicit)"));
        SendMessageW(gameAudio_, CB_SETCURSEL, 1, 0);
        for (const wchar_t* label : {L"15 seconds", L"30 seconds", L"60 seconds", L"120 seconds"}) SendMessageW(replayDuration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(replayDuration_, CB_SETCURSEL, 2, 0);

        for (const wchar_t* label : {L"256 MiB", L"512 MiB (default)", L"1024 MiB"}) SendMessageW(replayMemory_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(replayMemory_, CB_SETCURSEL, 1, 0);
        refreshMicrophones();
        SendMessageW(gameVolume_, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); SendMessageW(gameVolume_, TBM_SETPOS, TRUE, 100);
        SendMessageW(microphoneVolume_, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); SendMessageW(microphoneVolume_, TBM_SETPOS, TRUE, 100);
        SendMessageW(gameMeter_, PBM_SETRANGE32, 0, 1000); SendMessageW(microphoneMeter_, PBM_SETRANGE32, 0, 1000);
        EnableWindow(stop_, FALSE); EnableWindow(stopRecording_, FALSE);
        wchar_t profile[MAX_PATH]{};
        const DWORD profileLength = GetEnvironmentVariableW(L"USERPROFILE", profile, _countof(profile));
        if (profileLength && profileLength < _countof(profile)) {
            outputFolder_ = (std::filesystem::path(profile) / L"Videos" / L"PandaClip").wstring();
        }
        SetWindowTextW(folderLabel_, outputFolder_.c_str());
        refreshWindows();
        std::wstring error;
        if (!engine_.initialize(
            externalPreview_,
            error)) { setStatus(L"Graphics initialization failed.");  }
        createWorkspaceUi();
        layout();
    }

    void refreshWindows() {
        HWND previous = selectedTarget();
        std::vector<WindowEntry> fresh;
        EnumWindows(enumerateWindow, reinterpret_cast<LPARAM>(&fresh));
        std::erase_if(fresh, [this](const WindowEntry& entry) { return entry.handle == hwnd_; });
        SendMessageW(combo_, CB_RESETCONTENT, 0, 0);
        windows_ = std::move(fresh);
        for (const auto& entry : windows_) {
            const LRESULT index = SendMessageW(combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.title.c_str()));
            SendMessageW(combo_, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(entry.handle));
        }
        int choice=previous?-1:(uiReady_?-1:0);
        for(size_t i=0;i<windows_.size();++i)if(windows_[i].handle==previous)choice=static_cast<int>(i);
        if(choice<0&&loadedSettings_.contains("sourceExe")&&loadedSettings_.contains("sourceTitle"))for(size_t i=0;i<windows_.size();++i)if(windows_[i].title==loadedSettings_["sourceTitle"]&&executableFor(windows_[i].handle)==loadedSettings_["sourceExe"])choice=static_cast<int>(i);
        SendMessageW(combo_, CB_SETCURSEL, choice, 0);
        setStatus(windows_.empty() ? L"No eligible windows found." : L"Ready. Select a window and start preview.");
        logLine(L"Window list refreshed: %zu eligible windows.", windows_.size());
    }

    void refreshMicrophones() {
        if (microphone_ && SendMessageW(microphone_, CB_GETCOUNT, 0, 0) == 0) {
            SendMessageW(microphone_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default device (Windows default recording input)"));
            SendMessageW(microphone_, CB_SETCURSEL, 0, 0);
        }
        deviceCatalog_.request();
    }
    void applyMicrophones(std::vector<AudioDeviceInfo> devices, const std::wstring& error) {
        if (!microphone_ || recorder_.busy()) return;
        std::wstring selectedId;
        const LRESULT oldSelection = SendMessageW(microphone_, CB_GETCURSEL, 0, 0);
        if (oldSelection != CB_ERR) {
            const LRESULT data = SendMessageW(microphone_, CB_GETITEMDATA, oldSelection, 0);
            if (data > 0 && static_cast<size_t>(data - 1) < microphones_.size()) selectedId = microphones_[data - 1].id;
        }
        SendMessageW(microphone_, CB_RESETCONTENT, 0, 0);
        SendMessageW(microphone_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default device (Windows default recording input)"));
        microphones_ = std::move(devices);
        LRESULT selection = 0;
        for (size_t i = 0; i < microphones_.size(); ++i) {
            const auto& device = microphones_[i];
            const LRESULT index = SendMessageW(microphone_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.name.c_str()));
            SendMessageW(microphone_, CB_SETITEMDATA, index, static_cast<LPARAM>(i + 1));
            if (!selectedId.empty() && device.id == selectedId) selection = index;
        }
        if (!selectedId.empty() && selection == 0) {
            microphones_.push_back({selectedId, L"Selected microphone (disconnected)"});
            selection = SendMessageW(microphone_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(microphones_.back().name.c_str()));
            SendMessageW(microphone_, CB_SETITEMDATA, selection, static_cast<LPARAM>(microphones_.size()));
        }
        SendMessageW(microphone_, CB_SETCURSEL, selection, 0);
        if (!error.empty()) { micSelectionError_ = error; logLine(L"Microphone enumeration warning: %s", error.c_str()); }
        else micSelectionError_.clear();
    }

    void layout() { if(uiReady_)shellLayout(); }

    std::wstring executableFor(HWND target) const {
        DWORD pid{}; GetWindowThreadProcessId(target, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[32768]{}; DWORD count = 32768;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &count) != FALSE;
        CloseHandle(process); return ok ? std::wstring(path, count) : std::wstring{};
    }
    void interruptCapture(const std::wstring& reason, bool gpu = false) {
        if(sceneRuntime_){
            #ifdef REDOT_UI_VERIFICATION
            quietProducer_.request_stop();
            #endif
            if(!recoveryPending_){resumeReplay_=replayEnabled_;resumeRecording_=recorder_.busy()&&!replayWorkerOnly_;recoveryAttempts_=0;}
            recoveryPending_=true;recoverGpu_|=gpu;recoveryPhase_=RecoveryPhase::Draining;recoveryText_=reason;
            sceneRuntime_->attach({});sceneRuntime_->requestStop();recorder_.requestStop();replayEnabled_=false;replayStore_->setEnabled(false);
            recoveryNext_=std::chrono::steady_clock::now()+std::chrono::seconds(2);setStatus(reason+L" Draining scene session; sources preserved.");return;
        }
    }
    void endRecovery(const std::wstring& reason) {
        recoveryPhase_ = RecoveryPhase::Exhausted;
        recoveryPending_ = false; resumeReplay_ = resumeRecording_ = false;
        recoveryText_ = reason;
        replayEnabled_ = false; replayStore_->setEnabled(false); replayStore_->clearHistory();
        SetWindowTextW(replayToggle_, L"Enable Replay Buffer");
        EnableWindow(start_, TRUE); EnableWindow(combo_, TRUE); EnableWindow(refresh_, TRUE); EnableWindow(record_, TRUE); EnableWindow(replayToggle_, TRUE);
        EnableWindow(stop_, FALSE); EnableWindow(stopRecording_, FALSE);
        setEncodingControls(true); setStatus(reason); setReplayStatus(L"Replay stopped. Select the source and enable it to warm again.");
    }
    void recoveryTick() {
        if (!recoveryPending_ || recorder_.busy() || closing_) return;
#ifdef REDOT_UI_VERIFICATION
        if(quietProducerBusy_)return;
#endif
        if(sceneRuntime_){
            auto now=std::chrono::steady_clock::now();
            if(sceneRuntime_->busy()){
                if(recoveryPhase_!=RecoveryPhase::Restarting||sceneRuntime_->status().frames<2)return;
                recoveryPending_=false;recoverGpu_=false;recoveryPhase_=RecoveryPhase::None;replayStore_->clearHistory();replayEnabled_=resumeReplay_;replayStore_->setEnabled(replayEnabled_);
                EnableWindow(record_,TRUE);EnableWindow(replayToggle_,TRUE);setEncodingControls(true);if(resumeRecording_)startSceneSession(false);else if(replayEnabled_)startSceneSession(true);
                setStatus(L"Scene recovered. Recording uses a new file; replay warms again.");return;
            }
            if(powerSuspended_||sessionLocked_){recoveryPhase_=RecoveryPhase::Suspended;return;}
            if(now<recoveryNext_)return;
            if(recoveryAttempts_>=3){endRecovery(L"Scene GPU recovery exhausted after three attempts. Restart capture manually.");return;}
            ++recoveryAttempts_;sceneRuntime_.reset();engine_.sceneFrame={};std::wstring error;
            if(forceRecoveryFailure_||(recoverGpu_&&!engine_.rebuildGraphics(error))){
                sceneRuntime_=std::make_unique<Scenes::Runtime>(engine_.gpuResources(),hwnd_,kFrameMessage);sceneRuntime_->requestStop();recoveryNext_=now+std::chrono::seconds(5);setStatus(L"Scene recovery attempt failed: "+error);return;
            }
            sceneRuntime_=std::make_unique<Scenes::Runtime>(engine_.gpuResources(),hwnd_,kFrameMessage);sceneRuntime_->configure(selectedScene_,videoConfig_);sceneRuntime_->borderPermission(borderAccess_.result().permission);sceneRuntime_->cursorEnabled(engine_.cursorEnabled);engine_.sceneFrame=[this]{return sceneRuntime_?sceneRuntime_->latest():RecordedFrame{};};
            recoveryPhase_=RecoveryPhase::Restarting;recoveryNext_=now+std::chrono::seconds(2);setStatus(L"Restarting preserved scene sources…");return;
        }
    }

    void resize() {
        std::wstring error;
        if (!engine_.resizePreview(error)) setStatus(error);
        else draw();
    }

    void draw() {
        if(!externalPreview_ || !IsWindowVisible(externalPreview_) || IsIconic(GetAncestor(externalPreview_,GA_ROOT)))return;
        if (!previewVisible_ || IsIconic(hwnd_) || (uiReady_ && page_ != 0)) return;
        const auto now = std::chrono::steady_clock::now(); if(now-lastPreviewPaint_<std::chrono::milliseconds(1000/std::max(1,previewRate_)))return; lastPreviewPaint_=now;
        std::wstring error;
        if (!engine_.render(error)) { setStatus(error); logLine(L"Preview rendering failed: %s", error.c_str()); }
    }

    void setStatus(const std::wstring& value) { if (status_) SetWindowTextW(status_, value.c_str()); }

    void performAction(CaptureAction action) {
        if (closing_) return;
        if (action == CaptureAction::Preview) startCapture();
        else if (action == CaptureAction::Recording) startRecording();
        else if (action == CaptureAction::Replay && replayEnabled_) startReplayWorker();
    }
    bool ensureBorderConsent(CaptureAction action) {
        const auto permission = borderAccess_.result().permission;
        if (permission != BorderPermission::Unrequested && permission != BorderPermission::Pending) return true;
        if(borderAction_!=CaptureAction::None)return false;
        borderAction_ = action;
        if (permission == BorderPermission::Unrequested) requestBorderless();
        setStatus(L"Waiting for Windows borderless consent; capture starts after the permission result.");
        EnableWindow(start_, FALSE); EnableWindow(record_, FALSE); EnableWindow(replayToggle_, FALSE); EnableWindow(stop_, TRUE);
        return false;
    }
    void requestBorderless() {
        bool valid=false;if(sceneRuntime_){auto state=sceneRuntime_->status();for(auto& d:selectedScene_.definitions)if((d.kind==Scenes::Kind::Game||d.kind==Scenes::Kind::Window||d.kind==Scenes::Kind::Display)&&(state.sources[d.id]=="Live"||state.sources[d.id]=="WGC opening"))valid=true;}
        if(!valid){setTextChanged(borderStatus_,L"Select an available visual capture source in Scenes before requesting borderless access.");return;}
        borderAccess_.request(hwnd_);
        SetWindowTextW(borderStatus_, borderAccess_.result().text.c_str());
        EnableWindow(borderRequest_, borderAccess_.result().permission != BorderPermission::Pending);
    }
    std::wstring selectedMicrophoneId() const {
        const auto index = SendMessageW(microphone_, CB_GETCURSEL, 0, 0);
        const auto data = index == CB_ERR ? 0 : SendMessageW(microphone_, CB_GETITEMDATA, index, 0);
        return data > 0 && static_cast<size_t>(data - 1) < microphones_.size() ? microphones_[data - 1].id : std::wstring{};
    }
    void startMicCheck() {
        if (recorder_.busy() || recoveryPending_ || closing_) return;
        std::wstring id = selectedMicrophoneId();
        if(sceneRuntime_){bool found=false;for(auto& scene:selectedScene_.scenes)if(scene.id==selectedScene_.selected)for(auto& a:scene.audio)for(auto& d:selectedScene_.definitions)if(d.id==a.sourceId&&d.kind==Scenes::Kind::Microphone&&!found){id=d.settings["deviceId"].toString().toStdWString();found=true;}if(!found){setTextChanged(micSourceStatus_,L"Assign a microphone in the selected scene first.");return;}}
        if (id.empty()) { if (pinnedDefaultMicrophone_.empty()) pinnedDefaultMicrophone_ = defaultAudioEndpointId(true); id = pinnedDefaultMicrophone_; }
        std::wstring error;
        if (!micMonitor_.start(id, error)) { micSelectionError_ = error; setTextChanged(micSourceStatus_, error.c_str()); return; }
        micSelectionError_.clear();
        setTextChanged(micCheck_, L"Stop mic check");
        logLine(L"Microphone check requested for exact endpoint=%s.", id.c_str());
    }
    bool closeMicCheckBefore(CaptureAction action) {
        if (!micMonitor_.busy()) return true;
        if(micAction_!=CaptureAction::None)return false;
        micMonitor_.requestStop(); restartMicCheck_ = false; micAction_ = action;
        setRecordStatus(L"Stopping the microphone check before opening recording audio...");
        setTextChanged(micCheck_, L"Stopping check...");
        return false;
    }
    void updateAudioSources() {
        if(sceneRuntime_){
            std::array<std::wstring,2> names;for(auto& scene:selectedScene_.scenes)if(scene.id==selectedScene_.selected)for(auto& a:scene.audio)for(auto& d:selectedScene_.definitions)if(d.id==a.sourceId){auto index=d.kind==Scenes::Kind::Microphone?1:0;if(!names[index].empty())names[index]+=L" + ";names[index]+=d.name.toStdWString()+(a.muted?L" (muted)":L"");}
            auto live=recorder_.ingress();auto check=micMonitor_.ingress();for(int i=0;i<2;++i){auto text=names[i].empty()?L"No source assigned in this scene.":names[i];auto input=live?live:(i==1&&micMonitor_.busy()?check:nullptr);if(input){std::lock_guard lock(input->mutex);text+=L" · "+input->audioSourceStatus[i];if(!input->audioEndpointNames[i].empty())text+=L" · "+input->audioEndpointNames[i];if(input->audioEndpointMuted[i])text+=L" · Windows endpoint muted";}else text+=L" · configure in Scenes";if(i==1&&!recorder_.busy()&&!micMonitor_.busy()){auto failure=micMonitor_.error();if(failure.empty())failure=micSelectionError_;if(!failure.empty())text+=L" · "+failure;}setTextChanged(i?micSourceStatus_:gameSourceStatus_,text.c_str());}
            if(!recorder_.busy()&&!micMonitor_.busy())setTextChanged(micCheck_,L"Check mic");return;
        }
        if (engine_.active() && !engine_.borderWarning().empty()) SetWindowTextW(borderStatus_, engine_.borderWarning().c_str());
        const auto live = recorder_.ingress();
        const auto test = micMonitor_.ingress();
        const bool recording = recorder_.busy();
        const bool micEnabled = SendMessageW(microphoneEnable_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool gameEnabled = SendMessageW(gameEnable_, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageW(gameAudio_, CB_GETCURSEL, 0, 0) != 0;
        const bool gameWhole = SendMessageW(gameAudio_, CB_GETCURSEL, 0, 0) == 2;
        wchar_t title[1024]{}; GetWindowTextW(selectedTarget(), title, 1024);
        std::wstring game = !gameEnabled ? L"Game audio disabled." : gameWhole ? L"Game audio: WHOLE SYSTEM (explicit selection)." : L"Game audio: selected app - " + std::wstring(title);
        game += L" Gain " + std::to_wstring(SendMessageW(gameVolume_, TBM_GETPOS, 0, 0)) + L"%";
        if (SendMessageW(gameMute_, BM_GETCHECK, 0, 0) == BST_CHECKED) game += L"; MUTED in ReDot.";
        if (live && gameEnabled) {
            std::lock_guard lock(live->mutex);
            game += (live->audioSourceOpen[0] ? L"; open: " : L"; opening/unavailable: ") + live->audioEndpointNames[0];
            if (!live->audioSourceStatus[0].empty()) game += L"; " + live->audioSourceStatus[0];
        }
        setTextChanged(gameSourceStatus_, game.c_str());
        wchar_t device[512]{}; GetWindowTextW(microphone_, device, 512);
        std::wstring mic = std::wstring(L"Mic ") + (micEnabled ? L"enabled" : L"disabled") + L" for recording; selected: " + device;
        const auto ingress = live && micEnabled ? live : test;
        const bool checking = !recording && micMonitor_.busy();
        if (ingress && (checking || (recording && micEnabled))) {
            std::lock_guard lock(ingress->mutex);
            if (ingress->audioSourceOpen[1]) {
                mic += L"\n" + std::wstring(checking ? L"CHECK open: " : L"Recording open: ") + ingress->audioEndpointNames[1];
                mic += L"; Windows input volume " + std::to_wstring(static_cast<int>(std::lround(100 * ingress->audioEndpointVolume[1]))) + L"%";
                if (ingress->audioEndpointMuted[1] || ingress->audioEndpointVolume[1] <= 0.0001f) mic += L"; MUTED/zero input volume in Windows";
                const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                const auto last = ingress->audioLastPacketMilliseconds[1].load();
                if (!last || now > last + 1500) mic += L"; no recent WASAPI samples";
                else if (ingress->rawMeters[1].snapshot(GetTickCount64()).displayedPeak > 0.00003f) mic += L"; raw input activity detected (RMS/peak meter; speech not established)";
                else if (ingress->audioNonzeroFrames[1].load()) mic += L"; quiet now (signal was detected)";
                else mic += L"; samples arriving, no signal detected - speak/check device mute";
            } else mic += checking ? L"\nOpening/stopping the selected input..." : L"\nSelected mic is not open.";
            if (!ingress->audioSourceStatus[1].empty()) mic += L"; " + ingress->audioSourceStatus[1];
        } else if (!micMonitor_.error().empty()) mic += L"\n" + micMonitor_.error();
        else if (!micSelectionError_.empty()) mic += L"\n" + micSelectionError_;
        else mic += L"\nClick Check mic to open only this input; testing does not save or monitor audio to speakers.";
        if (SendMessageW(microphoneMute_, BM_GETCHECK, 0, 0) == BST_CHECKED) mic += L"; recording MUTED in ReDot";
        const auto gain = SendMessageW(microphoneVolume_, TBM_GETPOS, 0, 0);
        mic += L"; gain " + std::to_wstring(gain) + L"%";
        if (!gain) mic += L" (recorded mic will be silent)";
        setTextChanged(micSourceStatus_, mic.c_str());
        if (!recording) {
            setMeterChanged(microphoneMeter_, checking && test ? test->microphoneLevel.load() : 0);
            if (!micMonitor_.busy()) setTextChanged(micCheck_, L"Check mic");
        }
    }

    void startCapture() {
        if(selectedScene_.scenes.isEmpty()){setStatus(L"Scenes are loading; no alternate capture mode exists.");return;}
        if(recoveryPending_){setStatus(L"Scene recovery is draining/restarting. Wait for it to finish.");return;}
        recoveryPhase_=RecoveryPhase::None;recoveryAttempts_=0;recoverGpu_=false;
        if((!sceneRuntime_||!sceneRuntime_->busy())&&!recorder_.busy()){
            engine_.sceneFrame={};sceneRuntime_.reset();std::wstring error;if(engine_.faulted()&&!engine_.rebuildGraphics(error)){endRecovery(error);return;}sceneRuntime_=std::make_unique<Scenes::Runtime>(engine_.gpuResources(),hwnd_,kFrameMessage);sceneRuntime_->configure(selectedScene_,videoConfig_);sceneRuntime_->borderPermission(borderAccess_.result().permission);engine_.sceneFrame=[this]{return sceneRuntime_?sceneRuntime_->latest():RecordedFrame{};};
        }
        sceneRuntime_->suspend(false);setStatus(L"Selected scene preview active. Configure content in Scenes.");
    }
    void stopCapture(const wchar_t* reason=L"Scene capture suspended.") {
        borderAction_=micAction_=CaptureAction::None;micMonitor_.requestStop();
        replayEnabled_=false;replayStore_->setEnabled(false);SetWindowTextW(replayToggle_,L"Enable Replay Buffer");
        if(recorder_.busy())stopRecording();if(sceneRuntime_)sceneRuntime_->suspend(true);setStatus(reason);
    }

    HWND selectedTarget() const {
        const LRESULT selected = SendMessageW(combo_, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR) return nullptr;
        return reinterpret_cast<HWND>(SendMessageW(combo_, CB_GETITEMDATA, selected, 0));
    }

    void chooseOutputFolder() {
        BROWSEINFOW browse{};
        browse.hwndOwner = hwnd_;
        browse.lpszTitle = L"Choose the folder for ReDot MP4 recordings";
        browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
        if (!item) return;
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(item, path)) {
            outputFolder_ = path;
            markSettingsDirty();
            SetWindowTextW(folderLabel_, outputFolder_.c_str());
        }
        CoTaskMemFree(item);
    }

    RecordingQuality selectedQuality() const {
        return SendMessageW(quality_, CB_GETCURSEL, 0, 0) == 1 ? RecordingQuality::HighQuality : RecordingQuality::Balanced;
    }

    void startReplayWorker() {
        if(recorder_.busy()||!replayEnabled_)return;
        if(!sceneRuntime_){replayEnabled_=false;replayStore_->setEnabled(false);setReplayStatus(L"Scenes are still loading; replay cannot start yet.");return;}
        startSceneSession(true);
    }

    void toggleReplay() {
        if (replayEnabled_) {
            replayEnabled_ = false;
            replayStore_->setEnabled(false);
            SetWindowTextW(replayToggle_, L"Enable Replay Buffer");
            if (replayWorkerOnly_ && recorder_.busy()) {
                stopReplayAfterWorker_ = true;
                recorder_.requestStop();
                setReplayStatus(L"Disabling replay; stopping the encoder safely...");
            } else setReplayStatus(L"Replay buffer disabled.");
            return;
        }
        replayEnabled_ = true;
        replayStore_->setEnabled(true);
        SetWindowTextW(replayToggle_, L"Disable Replay Buffer");
        if (recorder_.busy() && !replayWorkerOnly_) {
            setReplayStatus(L"Replay active during recording; buffered history is warming from this point.");
            return;
        }
        startReplayWorker();
    }

    void shortcutNotice(const std::wstring& text) {
        logLine(L"Shortcut notice: %s", text.c_str());
        if (!diagnosticSetting(L"PANDACLIP_TEST_QUIET_UI")) MessageBeep(MB_ICONEXCLAMATION);
        if (frontendNotice_) frontendNotice_(text);
    }
    void saveReplayClip() {
        if (!replayEnabled_) { setReplayStatus(L"Enable replay before saving a clip."); return; }
        setReplayStatus(L"Saving compressed history; capture continues..."); replayStore_->saveAsync(hwnd_, outputFolder_, replaySeconds_);
    }
    void updateReplayStatus() {
        if(!replayEnabled_ || std::chrono::steady_clock::now()<replayStatusStickyUntil_)return;
        wchar_t text[512]{};double available=replayStore_->availableSeconds();double rolling=replayStore_->rollingBytes()/1048576.0,retained=replayStore_->retainedBytes()/1048576.0;
        swprintf_s(text,L"Replay %s: %.1f / %u seconds | packets %.1f MiB + exports %.1f MiB / %u MiB | %u pending saves. %s",available+2>=replaySeconds_?L"ready":L"warming",available,replaySeconds_,rolling,retained,replayMiB_,replayStore_->pendingSaves(),available+2<replaySeconds_&&replayStore_->memoryEvictions.load()>0?L"Memory limits history; Save uses available keyframes.":L"Actual clip duration is keyframe/AAC aligned.");setReplayStatus(hotkeyError_.empty()?std::wstring(text):hotkeyError_+L" "+text);
    }

    void setEncodingControls(bool enabled) {
        EnableWindow(gameEnable_, enabled); EnableWindow(micCheck_, enabled);
        EnableWindow(chooseFolder_, enabled); EnableWindow(quality_, enabled);
        EnableWindow(gameAudio_, enabled); EnableWindow(microphoneEnable_, enabled); EnableWindow(microphone_, enabled);
        EnableWindow(gameVolume_, enabled); EnableWindow(microphoneVolume_, enabled);
        // Gain/source changes stay at session boundaries; mute already has a
        // safe atomic worker path and must remain usable while encoding.
        EnableWindow(gameMute_, !closing_); EnableWindow(microphoneMute_, !closing_);
    }

    void updateReplayMemoryLimit() {
        if (replayStore_->pendingSaves()) {
            const auto cap = replayStore_->memoryLimitBytes() / 1048576;
            SendMessageW(replayMemory_, CB_SETCURSEL, cap == 256 ? 0 : cap == 1024 ? 2 : 1, 0);
            setReplayStatus(L"Wait for pending exports before changing the RAM cap."); return;
        }
        const LRESULT selected = SendMessageW(replayMemory_, CB_GETCURSEL, 0, 0);
        const uint64_t limits[] = {256ull, 512ull, 1024ull};
        const unsigned index = selected >= 0 && selected < 3 ? static_cast<unsigned>(selected) : 1;
        replayStore_->setMemoryLimitBytes(limits[index] * 1024 * 1024);
        replayStore_->clearHistory();
        if (replayEnabled_) {
            setReplayStatus(L"Replay history reset because the memory limit changed; buffering again.");
            replayStatusStickyUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
    }

    void setReplayStatus(const std::wstring& text) {
        if (replayStatus_) setTextChanged(replayStatus_, text.c_str());
        if(!replayEnabled_&&!recorder_.busy()&&!recoveryPending_&&recoveryPhase_!=RecoveryPhase::Exhausted)setStatus(text);
    }

    void startRecording() {
        if (recorder_.busy()) {
            if (replayWorkerOnly_) {
                recordAfterReplayStop_ = true;
                recorder_.requestStop();
                setRecordStatus(L"Switching from replay warm-up to recording; replay history resets and warms again after configuration changes.");
                setReplayStatus(L"Replay history reset for the recording encoder session; it will warm again after the switch.");
                replayStatusStickyUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }
            return;
        }
        recordingWarning_.clear();
        if(!sceneRuntime_){setRecordStatus(L"Scenes are still loading; recording cannot start yet.");return;}
        startSceneSession(false);
    }

    void stopRecording(const wchar_t* message = L"Finalizing recording...") {
        if (!recorder_.busy()) return;
        if(sceneRuntime_)sceneRuntime_->attach({});
        recorder_.requestStop();
        EnableWindow(stopRecording_, FALSE);
        setRecordStatus(message);
    }

    void setRecordStatus(const std::wstring& value) {
        if (recordStatus_) setTextChanged(recordStatus_, value.c_str());
        if(!recorder_.busy()&&!recoveryPending_&&recoveryPhase_!=RecoveryPhase::Exhausted)setStatus(value);
    }
    void startSceneSession(bool replay) {
        if(!sceneRuntime_||!sceneRuntime_->busy()){setRecordStatus(L"Scene compositor unavailable. Check Diagnostics and restart capture.");return;}
        if(selectedScene_.scenes.isEmpty()){setRecordStatus(L"Scenes are still loading. Try again shortly.");return;}
        if(!closeMicCheckBefore(replay?CaptureAction::Replay:CaptureAction::Recording))return;
        RecordingAudioOptions audio;audio.gameMode=GameAudioMode::Off;
        sceneAudio_=audio.scene=Scenes::makeAudioSession(selectedScene_,[this]{return sceneRuntime_->snapshot();},quintptr(GetThreadDesktop(GetCurrentThreadId())));
        #ifdef REDOT_UI_VERIFICATION
        if(quietFrontendSource_)sceneAudio_=audio.scene=Scenes::makeVerificationAudioSession(selectedScene_,[this]{return sceneRuntime_->snapshot();});
        #endif
        sceneRuntime_->suspend(false);sceneRuntime_->configure(selectedScene_,videoConfig_);
        std::wstring error;
        if(!recorder_.start(hwnd_,engine_.gpuResources(),outputFolder_,selectedQuality(),audio,error,replay,replayStore_,videoConfig_)){
            if(replay){replayEnabled_=false;replayStore_->setEnabled(false);setReplayStatus(error);}else setRecordStatus(error);return;
        }
        replayWorkerOnly_=replay;sceneRuntime_->attach(recorder_.ingress());setEncodingControls(false);
        EnableWindow(record_,replay);EnableWindow(stopRecording_,!replay);
        setStatus(L"Capturing the selected scene through the GPU compositor.");
        if(replay)setReplayStatus(L"Scene replay is warming up.");else setRecordStatus(L"Starting scene NVENC and scene audio sources…");
    }

    void updateElapsed() {
        const uint64_t total = recorder_.elapsedMilliseconds() / 1000;
        wchar_t value[32]{};
        swprintf_s(value, L"%02llu:%02llu:%02llu", total / 3600, (total / 60) % 60, total % 60);
        if (elapsed_) setTextChanged(elapsed_, value);
        const auto ingress = recorder_.ingress();
        if (ingress) {
            setMeterChanged(gameMeter_, ingress->gameLevel.load(std::memory_order_relaxed));
            setMeterChanged(microphoneMeter_, ingress->microphoneLevel.load(std::memory_order_relaxed));
        } else { setMeterChanged(gameMeter_, 0); setMeterChanged(microphoneMeter_, 0); }
    }

    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_HSCROLL:
            if(uiReady_ && (reinterpret_cast<HWND>(lParam)==mainAudio_[6] || reinterpret_cast<HWND>(lParam)==mainAudio_[7])){int i=reinterpret_cast<HWND>(lParam)==mainAudio_[6]?0:1;auto original=i?microphoneVolume_:gameVolume_;SendMessageW(original,TBM_SETPOS,TRUE,SendMessageW(mainAudio_[6+i],TBM_GETPOS,0,0));markSettingsDirty();return 0;}
            if (IsChild(settingsContent_, reinterpret_cast<HWND>(lParam))) markSettingsDirty();return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {dip(700), dip(620)}; return 0;
        case WM_CREATE: createControls(); WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION); return 0;
        case WM_MOVE: markSettingsDirty(); return 0;
        case WM_SIZE: markSettingsDirty(); layout(); if(uiReady_ && wParam==SIZE_MINIMIZED && closeToTray_)ShowWindow(hwnd_,SW_HIDE); return 0;
        case WM_COMMAND:
            if(workspaceCommand(wParam,lParam))return 0;
            switch (LOWORD(wParam)) {
            case kBorderRequest: requestBorderless(); return 0;
            case kMicCheck:
                if (micMonitor_.busy()) { micMonitor_.requestStop(); restartMicCheck_ = false; setTextChanged(micCheck_, L"Stopping check..."); }
                else startMicCheck();
                return 0;
            case kMicrophoneEnable: updateAudioSources(); return 0;
            case kGameEnable:
                if (SendMessageW(gameEnable_, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageW(gameAudio_, CB_GETCURSEL, 0, 0) == 0)
                    SendMessageW(gameAudio_, CB_SETCURSEL, lastGameMode_, 0);
                updateAudioSources(); return 0;
            case kDiagnosticsButton: showDiagnostics(); return 0;
            case kStartButton: PostMessageW(hwnd_, kStartCaptureMessage, 0, 0); return 0;
            case kStopButton: PostMessageW(hwnd_, kStopCaptureMessage, 0, 0); return 0;
            case kRefreshButton: refreshWindows(); return 0;
            case kChooseFolderButton: chooseOutputFolder(); return 0;
            case kRecordButton: PostMessageW(hwnd_, kStartRecordingMessage, 0, 0); return 0;
            case kStopRecordingButton: PostMessageW(hwnd_, kStopRecordingMessage, 0, 0); return 0;
            case kMicrophoneCombo:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    pinnedDefaultMicrophone_.clear(); micSelectionError_.clear();
                    micMonitor_.clearError();
                    if (micMonitor_.busy()) { restartMicCheck_ = true; micMonitor_.requestStop(); }
                    updateAudioSources();
                }
                return 0;
            case kGameAudioCombo:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    pinnedSystemAudio_.clear(); const auto mode = SendMessageW(gameAudio_, CB_GETCURSEL, 0, 0);
                    if (mode > 0) lastGameMode_ = mode;
                    SendMessageW(gameEnable_, BM_SETCHECK, mode > 0 ? BST_CHECKED : BST_UNCHECKED, 0); updateAudioSources();
                }
                return 0;
            case kReplayToggleButton: PostMessageW(hwnd_, WM_APP + 15, 0, 0); return 0;
            case kReplaySaveButton: PostMessageW(hwnd_, WM_APP + 16, 0, 0); return 0;
            case kReplayMemoryCombo: return 0;
            }
            break;
        case kFrameMessage:
            if(borderAccess_.result().permission==BorderPermission::Unrequested&&sceneRuntime_){auto state=sceneRuntime_->status();bool live=false;for(auto& d:selectedScene_.definitions)if((d.kind==Scenes::Kind::Game||d.kind==Scenes::Kind::Window||d.kind==Scenes::Kind::Display)&&(state.sources[d.id]=="Live"||state.sources[d.id]=="WGC opening"))live=true;if(live)requestBorderless();}
            draw(); return 0;
        case kBorderAccessMessage: {
            if (closing_) return 0;
            const auto result = borderAccess_.result();
            if(result.permission==BorderPermission::Pending)return 0;
            logLine(L"Controller consent completion: permission=%d, pending action=%d; %s",int(result.permission),int(borderAction_),result.text.c_str());
            std::wstring error;
            const bool applied = engine_.setBorderPermission(result.permission, error);
            if(sceneRuntime_)sceneRuntime_->borderPermission(result.permission);
            SetWindowTextW(borderStatus_, (applied ? result.text : error).c_str());
            EnableWindow(borderRequest_, TRUE);
            if (!recorder_.busy()) { EnableWindow(record_, TRUE); EnableWindow(replayToggle_, TRUE); EnableWindow(start_, !engine_.active()); }
            const auto action = borderAction_; borderAction_ = CaptureAction::None; performAction(action);
            return 0;
        }
        case kStartCaptureMessage: startCapture(); return 0;
        case kStopCaptureMessage: stopCapture(); return 0;
        case kStartRecordingMessage: startRecording(); return 0;
        case kStopRecordingMessage: stopRecording(); return 0;
        case WM_APP + 15: toggleReplay(); return 0;
        case WM_APP + 16: saveReplayClip(); return 0;
        case WM_HOTKEY:
            if(wParam>=100&&wParam<106){shortcut(static_cast<unsigned>(wParam-100));return 0;}
            break;
        case kReplayEventMessage: {
            std::unique_ptr<ReplayEvent> event(reinterpret_cast<ReplayEvent*>(lParam));
            if (event) { if(ClipSound::due(clipSound_,*event))ClipSound::play(); if(event->success)gallerySaved(event->finalizedPath); if (!event->success) { noteFailure(event->text); shortcutNotice(event->text); } setReplayStatus(event->text); replayStatusStickyUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(5); }
            return 0;
        }
        case kReportEventMessage: {
            std::unique_ptr<ReplayEvent> event(reinterpret_cast<ReplayEvent*>(lParam)); reportPending_ = false;
            if (event && !event->success) noteFailure(event->text);
            if (event && diagnosticsStatus_) SetWindowTextW(diagnosticsStatus_, event->text.c_str());
            return 0;
        }
        case kRecordingEventMessage: {
            std::unique_ptr<RecordingEvent> event(reinterpret_cast<RecordingEvent*>(lParam));
            if (!event) return 0;
            updateElapsed();
            if (event->kind == RecordingEventKind::Started) {
                settingsPending_ = false;
                if (replayWorkerOnly_) setReplayStatus(event->text); else setRecordStatus(event->text);
            } else if (event->kind == RecordingEventKind::Progress) {
                if (event->text.starts_with(L"Audio source warning:")) {
                    recordingWarning_ = event->text;
                    setRecordStatus(event->text);
                } else {
                    if (!replayWorkerOnly_) setRecordStatus(L"Recording active." + (recordingWarning_.empty() ? std::wstring{} : L"\n" + recordingWarning_));
                }
            } else {
                if(event->success&&!replayWorkerOnly_)gallerySaved(event->finalizedPath);
                lastDiagnostics_ = recorder_.ingress();
                if (!event->success) noteFailure(event->text);
                recorder_.join();
                if(sceneRuntime_)sceneRuntime_->attach({});
                if (recoveryPending_) {
                    setRecordStatus(event->text);
                    replayWorkerOnly_ = false; setEncodingControls(true); recoveryTick(); return 0;
                }
                if (!event->success && engine_.faulted()) {
                    const bool wasReplayOnly = replayWorkerOnly_;
                    replayWorkerOnly_ = false;
                    interruptCapture(event->text, true);
                    resumeRecording_ = !wasReplayOnly; resumeReplay_ = replayEnabled_ || resumeReplay_;
                    return 0;
                }
                if (replayWorkerOnly_) {
                    replayWorkerOnly_ = false;
                    setEncodingControls(true);
                    if (recordAfterReplayStop_) {
                        recordAfterReplayStop_ = false;
                        if (replayStore_) replayStore_->setEnabled(false);
                        startRecording();
                    } else if (closing_) finishClosing();
                    else if (!event->success) {
                        replayEnabled_ = false; replayStore_->setEnabled(false); replayStore_->clearHistory();
                        SetWindowTextW(replayToggle_, L"Enable Replay Buffer"); setReplayStatus(event->text);
                    }
                    else if (!replayEnabled_ || stopReplayAfterWorker_) {
                        stopReplayAfterWorker_ = false;
                        setReplayStatus(L"Replay buffer disabled.");
                    }
                    return 0;
                }
                EnableWindow(record_, TRUE); EnableWindow(chooseFolder_, TRUE); EnableWindow(quality_, TRUE);
                EnableWindow(gameEnable_, TRUE); EnableWindow(micCheck_, TRUE);
                EnableWindow(stopRecording_, FALSE);
                EnableWindow(gameAudio_, TRUE); EnableWindow(microphoneEnable_, TRUE); EnableWindow(microphone_, TRUE);
                EnableWindow(gameVolume_, TRUE); EnableWindow(microphoneVolume_, TRUE); EnableWindow(gameMute_, TRUE); EnableWindow(microphoneMute_, TRUE);
                if (event->success && event->text.starts_with(L"Saved recording: ")) {
                    const auto warningAt = event->text.find(L"\nAudio warning:");
                    const auto path = event->text.substr(17, warningAt == std::wstring::npos ? std::wstring::npos : warningAt - 17);
                    const auto filename = std::filesystem::path(path).filename().wstring();
                    const std::wstring warning = warningAt == std::wstring::npos ? L"" : L"\nAudio warning: " + event->text.substr(warningAt + 16);
                    setRecordStatus((_wcsicmp(std::filesystem::path(filename).extension().c_str(), L".mkv") == 0 ? L"Saved MKV: " : L"Saved MP4: ") + filename + warning);
                    recordingWarning_.clear();
                } else {
                    setRecordStatus(event->text);
                }
                logLine(L"Recording result: success=%d, frames=%llu, queueDrops=%llu, late/replaced=%llu, repeated=%llu.", event->success,
                    event->encodedFrames, event->queueDrops, event->replacedFrames, event->repeatedFrames);
                if (closing_) finishClosing();
                else if (replayEnabled_) {
                    replayStore_->setEnabled(true);
                    startReplayWorker();
                    setReplayStatus(L"Recording ended. Replay restarted; buffered history is warming again.");
                }
            }
            return 0;
        }
        #ifdef REDOT_UI_VERIFICATION
        case WM_APP+50:startQuietSource(static_cast<unsigned>(wParam));return 0;
        case WM_APP+51:{std::unique_ptr<std::vector<std::pair<QString,RecordedAudio>>> packets(reinterpret_cast<std::vector<std::pair<QString,RecordedAudio>>*>(lParam));--quietPacketsPending_;if(sceneAudio_&&recorder_.busy())for(auto& [id,packet]:*packets)Scenes::submitVerificationAudio(sceneAudio_,id,std::move(packet));return 0;}
        #endif
        case WM_TIMER:
            if (wParam == kHealthTimer) {
                if(auto terminal=recorder_.takeFailedCompletion()){
                    logLine(L"Owner health poll recovered the retained recording completion result.");
                    SendMessageW(hwnd_,kRecordingEventMessage,0,reinterpret_cast<LPARAM>(terminal.release()));
                }
                borderAccess_.tick();
                updateElapsed();
                updateAudioSources();
                workspaceTick();
                if (!micMonitor_.busy() && !closing_) {
                    const auto action = micAction_; micAction_ = CaptureAction::None;
                    if (action != CaptureAction::None) performAction(action);
                    else if (restartMicCheck_ && !recorder_.busy() && !recoveryPending_) { restartMicCheck_ = false; startMicCheck(); }
                }
                EnableWindow(gameEnable_, !recorder_.busy() && !recoveryPending_);
                EnableWindow(micCheck_, !recorder_.busy() && !recoveryPending_);
                updateReplayStatus();
                updateDiagnostics();
                if (closing_) { finishClosing(); return 0; }
                if (recoveryPending_) recoveryTick();
                else if(recoveryPhase_!=RecoveryPhase::Exhausted&&sceneRuntime_&&!sceneRuntime_->busy()&&!sceneRuntime_->status().error.isEmpty())interruptCapture(sceneRuntime_->status().error.toStdWString(),true);
                if (!recoveryPending_ && stableAfter_ != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= stableAfter_) { recoveryAttempts_ = 0; stableAfter_ = {}; }

            }
            return 0;
        case WM_APP + 24:
            if (diagnosticSetting(L"PANDACLIP_TEST_HOOKS")) {
                forceRecoveryFailure_ = wParam == 2;
                interruptCapture(L"Controlled capture/GPU interruption (test hook).", true);
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) { micMonitor_.requestStop(); restartMicCheck_ = false; micAction_ = CaptureAction::None; powerSuspended_ = true; interruptCapture(L"Windows is suspending capture for sleep."); }
            else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) { powerSuspended_ = false; recoverGpu_ = true; recoveryLimit_ = std::chrono::steady_clock::now() + std::chrono::seconds(60); recoveryNext_ = std::chrono::steady_clock::now() + std::chrono::seconds(2); }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK) { micMonitor_.requestStop(); restartMicCheck_ = false; micAction_ = CaptureAction::None; sessionLocked_ = true; interruptCapture(L"Windows session locked."); }
            else if (wParam == WTS_SESSION_UNLOCK) { sessionLocked_ = false; recoveryLimit_ = std::chrono::steady_clock::now() + std::chrono::seconds(60); recoveryNext_ = std::chrono::steady_clock::now() + std::chrono::seconds(2); }
            return 0;
        case WM_DEVICECHANGE:
            deviceCatalog_.request();
            return TRUE;
        case WM_CLOSE:
            if(uiReady_&&closeToTray_&&!exitRequested_){ShowWindow(hwnd_,SW_HIDE);return 0;}
            if(uiReady_){if(settingsAutosave_)settingsStore_.save(collectSettings());library_.cancel();deviceCatalog_.cancel();}
            micMonitor_.requestStop(); borderAccess_.cancel(); borderAction_ = micAction_ = CaptureAction::None; restartMicCheck_ = false;
            if (!closing_) shutdownStarted_ = std::chrono::steady_clock::now();
            closing_ = true; recoveryPending_ = false;
            replayEnabled_ = false; replayStore_->setEnabled(false);
            if (recorder_.busy()) stopRecording(L"Closing after the recording is finalized...");
            #ifdef REDOT_UI_VERIFICATION
            quietProducer_.request_stop();
            #endif
            if(sceneRuntime_)sceneRuntime_->requestStop();engine_.stop(); finishClosing(); return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE,&tray_);for(int i=0;i<6;i++)UnregisterHotKey(hwnd_,100+i);
            WTSUnRegisterSessionNotification(hwnd); KillTimer(hwnd, kHealthTimer); engine_.stop();
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    #include "workspace_ui.inl"
    #ifdef REDOT_UI_VERIFICATION
    #include "../tests/quiet_app_source.inl"
    #endif

    HWND diagnostics_{}, diagnosticsText_{}, diagnosticsStatus_{}, diagnosticsButton_{};
    HWND borderStatus_{}, borderRequest_{}, gameEnable_{}, gameSourceStatus_{}, micSourceStatus_{}, micCheck_{};
    BorderlessAccess borderAccess_;
    MicrophoneMonitor micMonitor_;
    CaptureAction borderAction_{CaptureAction::None}, micAction_{CaptureAction::None};
    bool restartMicCheck_{};
    LRESULT lastGameMode_{1};
    std::wstring micSelectionError_;
    bool previewVisible_{true}, reportPending_{};
    int gameSyncMilliseconds_{}, microphoneSyncMilliseconds_{};
    std::shared_ptr<RecordingIngress> lastDiagnostics_;
    uint64_t failures_{};
    std::deque<std::wstring> failureHistory_;
    RecoveryPhase recoveryPhase_{RecoveryPhase::None};
    bool automaticStart_{}, forceRecoveryFailure_{}, recoverWindowClosed_{};
    std::chrono::steady_clock::time_point stableAfter_{};
    bool recoveryPending_{}, powerSuspended_{}, sessionLocked_{}, recoverGpu_{}, resumeReplay_{}, resumeRecording_{};
    unsigned recoveryAttempts_{};
    HWND recoveryTarget_{};
    std::wstring recoveryExecutable_, recoveryTitle_, recoveryText_;
    std::chrono::steady_clock::time_point recoveryNext_{}, recoveryLimit_{};
    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND combo_{};
    HWND start_{};
    HWND stop_{};
    HWND refresh_{};
    HWND status_{};
    HWND chooseFolder_{};
    HWND folderLabel_{};
    HWND quality_{};
    HWND record_{};
    HWND stopRecording_{};
    HWND elapsed_{};
    HWND recordStatus_{};
    HWND gameAudio_{};
    HWND microphoneEnable_{};
    HWND microphone_{};
    HWND gameMute_{};
    HWND microphoneMute_{};
    HWND gameVolume_{};
    HWND microphoneVolume_{};
    HWND gameVolumeLabel_{};
    HWND microphoneVolumeLabel_{};
    HWND gameMeterLabel_{};
    HWND microphoneMeterLabel_{};
    HWND gameMeter_{};
    HWND microphoneMeter_{};
    HWND replayToggle_{};
    HWND replaySave_{};
    HWND replayDuration_{};
    HWND replayStatus_{};
    HWND replayMemory_{};
    HWND preview_{};
    std::vector<WindowEntry> windows_;
    std::vector<AudioDeviceInfo> microphones_;
    CaptureEngine engine_;
    std::unique_ptr<Scenes::Runtime> sceneRuntime_;
    std::shared_ptr<SceneAudioProvider> sceneAudio_;
    Scenes::Presentation scenePresentation_;
    Scenes::Snapshot selectedScene_;
    RecordingPipeline recorder_;
    std::shared_ptr<ReplayBuffer> replayStore_;
    std::wstring outputFolder_;
    std::wstring pinnedDefaultMicrophone_, pinnedSystemAudio_;
    bool closing_{};
    std::wstring recordingWarning_;
    bool replayEnabled_{};
    bool replayWorkerOnly_{};
    bool stopReplayAfterWorker_{};
    bool recordAfterReplayStop_{};
    std::wstring hotkeyError_;
    std::chrono::steady_clock::time_point replayStatusStickyUntil_{};
};
#include "frontend_adapter.inl"
} // namespace

std::unique_ptr<FrontendBackend> makeFrontendBackend(HINSTANCE instance, HWND preview) { return std::make_unique<FrontendController>(instance,preview); }

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        logLine(L"ReDot starting.");
        return runQtFrontend(instance);
    } catch (const winrt::hresult_error& e) {
        logLine(L"Fatal WinRT error 0x%08X: %s", static_cast<unsigned>(e.code().value), e.message().c_str());
        if(!diagnosticSetting(L"PANDACLIP_TEST_QUIET_UI"))MessageBoxW(nullptr, (L"ReDot could not initialize: " + e.message()).c_str(), L"ReDot", MB_ICONERROR);
        return 1;
    }
}


