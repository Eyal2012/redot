#pragma once
// External verification only. WGC reads a positively identified owned prototype
// HWND, never a personal application or desktop. Presentation never sees this.
#include <condition_variable>
#include <deque>
#include <thread>
static bool recordOwnedLens(HWND window,DWORD pid,const std::filesystem::path& directory){
    DWORD actual{};GetWindowThreadProcessId(window,&actual);if(actual!=pid)return false;
    using namespace winrt;using namespace winrt::Windows::Graphics;
    std::filesystem::create_directories(directory);
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<ID3D11Multithread> multithread;context.As(&multithread);multithread->SetMultithreadProtected(TRUE);
        auto interop=get_activation_factory<Capture::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();Capture::GraphicsCaptureItem item{nullptr};check_hresult(interop->CreateForWindow(window,guid_of<Capture::GraphicsCaptureItem>(),put_abi(item)));
        ComPtr<IDXGIDevice> dxgi;device.As(&dxgi);ComPtr<::IInspectable> inspectable;check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),&inspectable));
        winrt::Windows::Foundation::IInspectable projected{inspectable.Detach(),take_ownership_from_abi};auto direct=projected.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        auto pool=Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        auto session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);
        struct Image { UINT w{},h{};std::vector<BYTE> bytes;};
        std::mutex mutex,callbackMutex;std::condition_variable wake;std::deque<Image> queue;bool done=false;std::atomic<bool> accepting{true};
        std::atomic<bool> failure{};std::atomic<int> submitted{},written{};ComPtr<ID3D11Texture2D> staging;
        std::jthread writer([&]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);for(;;){Image image;{std::unique_lock lock(mutex);wake.wait(lock,[&]{return done||!queue.empty();});if(queue.empty())break;image=std::move(queue.front());queue.pop_front();}wchar_t name[48]{};swprintf_s(name,L"frame-%04d.png",written.load());if(!writeOwnedPng(directory/name,image.w,image.h,image.bytes)){failure=true;break;}++written;}CoUninitialize();});
        const auto origin=GetTickCount64();ULONGLONG next=origin;
        auto token=pool.FrameArrived([&](auto const& sender,auto const&){
            std::lock_guard callbackGuard(callbackMutex);if(!accepting)return;
            auto frame=sender.TryGetNextFrame();if(!frame||submitted>=150||failure)return;
            const auto now=GetTickCount64();if(now<next)return;next=origin+ULONGLONG(submitted+1)*1000/30;
            try{
                auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();ComPtr<ID3D11Texture2D> source;check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
                if(!staging){auto copy=desc;copy.Usage=D3D11_USAGE_STAGING;copy.BindFlags=0;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;copy.MiscFlags=0;check_hresult(device->CreateTexture2D(&copy,nullptr,&staging));}
                context->CopyResource(staging.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check_hresult(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
                // Keep only the bounded optical study (not desktop/background).
                // Locate the distinctive opaque sample fill in this owned
                // window. No layout constants for its vertical placement.
                UINT y=desc.Height;const UINT probe=desc.Width/3;
                for(UINT row=desc.Height/2;row+170<=desc.Height;++row){auto pixel=static_cast<const BYTE*>(mapped.pData)+size_t(row)*mapped.RowPitch+probe*4;if(std::abs(int(pixel[0])-44)<3&&std::abs(int(pixel[1])-35)<3&&std::abs(int(pixel[2])-23)<3){y=row;break;}}
                if(y+170>desc.Height){context->Unmap(staging.Get(),0);failure=true;return;}
                const UINT x=28,w=std::min<UINT>(672,desc.Width-x),h=170;
                Image image{w,h,std::vector<BYTE>(size_t(w)*h*4)};for(UINT row=0;row<h;++row)memcpy(image.bytes.data()+size_t(row)*w*4,static_cast<const BYTE*>(mapped.pData)+size_t(y+row)*mapped.RowPitch+x*4,w*4);context->Unmap(staging.Get(),0);
                {std::lock_guard lock(mutex);if(queue.size()>=8){failure=true;return;}queue.push_back(std::move(image));++submitted;}wake.notify_one();
            }catch(...){failure=true;}
        });
        session.StartCapture();const auto end=origin+12000;
        while(submitted<150&&!failure&&GetTickCount64()<end)pump(15);
        accepting=false;session.Close();pool.FrameArrived(token);pool.Close();
        {std::lock_guard callbackGuard(callbackMutex);} // all GPU-copy leases released before writer drain
        {std::lock_guard lock(mutex);done=true;}wake.notify_one();writer.join();
        std::ofstream report(directory.parent_path()/L"native-lens-recording.txt");report<<"owned PID="<<pid<<"\nframes="<<written<<"\nqueue bytes bound="<<8*672*170*4<<"\nactual elapsed ms="<<GetTickCount64()-origin<<"\nonly in-app optical sample; not desktop transmission evidence\n";
        return !failure&&written==150;
    }catch(...){return false;}
}
