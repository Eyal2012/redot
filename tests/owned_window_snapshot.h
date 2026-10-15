#pragma once
// External verification only: capture one positively identified owned HWND.
// Never used as a backdrop or preview presentation path.
#include <atomic>
#include <mutex>
#include <vector>
#include <d3d11_4.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wincodec.h>
static bool writeOwnedPng(const std::filesystem::path& file,UINT width,UINT height,const std::vector<BYTE>& pixels){
    try{ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> output;winrt::check_hresult(encoder->CreateNewFrame(&output,nullptr));winrt::check_hresult(output->Initialize(nullptr));winrt::check_hresult(output->SetSize(width,height));
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(output->SetPixelFormat(&format));winrt::check_hresult(output->WritePixels(height,width*4,UINT(pixels.size()),const_cast<BYTE*>(pixels.data())));winrt::check_hresult(output->Commit());winrt::check_hresult(encoder->Commit());return true;
    }catch(...){return false;}
}
static bool ownedCompositorSnapshot(HWND window,DWORD ownedPid,HWND background,const std::filesystem::path& file){
    DWORD pid{};GetWindowThreadProcessId(window,&pid);if(pid!=ownedPid||!IsWindowVisible(window))return false;
    RECT bounds{};GetWindowRect(window,&bounds);
    // Bring only a confirmed mouse-transparent, nonactivating test window
    // above ordinary windows. It cannot take input or activate on click.
    if((GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    // Cover the entire backing scene with our own window. Never reposition an
    // unrelated HWND; never take a full-desktop screenshot.
    // Include sampling margins so edge blur cannot include other windows.
    constexpr int margin=96;
    SetWindowPos(background,window,bounds.left-margin,bounds.top-margin,bounds.right-bounds.left+2*margin,bounds.bottom-bounds.top+2*margin,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    for(HWND h=GetWindow(window,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV)){
        if(!IsWindowVisible(h))continue;BOOL cloaked{};DwmGetWindowAttribute(h,14,&cloaked,sizeof(cloaked));if(cloaked)continue;
        RECT other{},intersection{};GetWindowRect(h,&other);if(IntersectRect(&intersection,&bounds,&other))return false;
    }
    UpdateWindow(background);DwmFlush();Sleep(180);DwmFlush();
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};void* pixels{};
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(memory);ReleaseDC(nullptr,screen);return false;}
    HGDIOBJ previous=SelectObject(memory,bitmap);bool copied=BitBlt(memory,0,0,width,height,screen,bounds.left,bounds.top,SRCCOPY|CAPTUREBLT)!=FALSE;
    std::vector<BYTE> data(size_t(width)*height*4);if(copied){memcpy(data.data(),pixels,data.size());for(size_t i=3;i<data.size();i+=4)data[i]=255;}
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
    return copied&&writeOwnedPng(file,width,height,data);
}
static bool ownedWindowSnapshot(HWND window,DWORD ownedPid,const std::filesystem::path& file){
    DWORD pid{};GetWindowThreadProcessId(window,&pid);if(pid!=ownedPid||!IsWindowVisible(window))return false;
    using namespace winrt;using namespace winrt::Windows::Graphics;
    try{
        init_apartment(apartment_type::multi_threaded);
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return false;
        ComPtr<ID3D11Multithread> multithread;context.As(&multithread);if(multithread)multithread->SetMultithreadProtected(TRUE);
        auto interop=get_activation_factory<Capture::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        Capture::GraphicsCaptureItem item{nullptr};check_hresult(interop->CreateForWindow(window,guid_of<Capture::GraphicsCaptureItem>(),put_abi(item)));
        if(item.Size().Width<=0||item.Size().Height<=0)return false;
        ComPtr<IDXGIDevice> dxgi;device.As(&dxgi);ComPtr<::IInspectable> inspectable;check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),&inspectable));
        winrt::Windows::Foundation::IInspectable projected{inspectable.Detach(),take_ownership_from_abi};auto direct=projected.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        auto pool=Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        auto session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);
        std::atomic_bool ready{};ComPtr<ID3D11Texture2D> staging;std::mutex lock;
        auto token=pool.FrameArrived([&](Capture::Direct3D11CaptureFramePool const& sender,winrt::Windows::Foundation::IInspectable const&){auto frame=sender.TryGetNextFrame();if(!frame)return;
            std::lock_guard guard(lock);if(ready)return;auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();ComPtr<ID3D11Texture2D> input;
            if(FAILED(access->GetInterface(IID_PPV_ARGS(&input))))return;D3D11_TEXTURE2D_DESC desc{};input->GetDesc(&desc);
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))return;context->CopyResource(staging.Get(),input.Get());ready=true;});
        session.StartCapture();auto end=GetTickCount64()+2000;while(!ready&&GetTickCount64()<end)Sleep(10);
        session.Close();pool.FrameArrived(token);pool.Close();std::lock_guard guard(lock);if(!ready||!staging)return false;
        D3D11_TEXTURE2D_DESC desc{};staging->GetDesc(&desc);D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
        std::vector<BYTE> pixels(size_t(desc.Width)*desc.Height*4);for(UINT y=0;y<desc.Height;++y)memcpy(pixels.data()+size_t(y)*desc.Width*4,static_cast<const BYTE*>(mapped.pData)+size_t(y)*mapped.RowPitch,desc.Width*4);context->Unmap(staging.Get(),0);
        return writeOwnedPng(file,desc.Width,desc.Height,pixels);
    }catch(...){return false;}
}
