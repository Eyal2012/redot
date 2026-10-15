#pragma once
// Experimental Glass Lab only. Never used by PandaClip's recording engine.
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
namespace glassdesktop {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
inline double millis(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Request {bool enabled{};HMONITOR monitor{};RECT crop{};LUID adapter{};unsigned revision{};};
struct Slot {
    ComPtr<ID3D11Texture2D> texture;ComPtr<IDXGIKeyedMutex> key;HANDLE handle{};unsigned width{},height{};
};
struct Frame {
    std::shared_ptr<Slot> slot;RECT crop{};Clock::time_point arrival{};std::int64_t sourceTime{};std::uint64_t sequence{};bool released{};
    ~Frame(){if(!released&&slot&&slot->key->AcquireSync(1,20)==S_OK)slot->key->ReleaseSync(0);}
};
struct Stats {std::string state="Custom Desktop stopped";std::uint64_t available{},published{},discarded{},bytes{},generation{},gpuSamples{};double copyCpuMs{},copyGpuMs{-1},sourceAgeMs{};Clock::time_point last{};};
class Capture {
    std::mutex mutex_;Request request_;Stats stats_;std::shared_ptr<Frame> latest_;std::jthread thread_;
    void state(std::string value){std::lock_guard g(mutex_);stats_.state=std::move(value);}
    void run(std::stop_token stop){
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        namespace W=winrt::Windows;namespace C=W::Graphics::Capture;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11Query> disjoint,beginTime,endTime;bool gpuPending{};double gpuSum{};
        C::Direct3D11CaptureFramePool pool{nullptr};C::GraphicsCaptureSession session{nullptr};
        std::array<std::shared_ptr<Slot>,3> slots{};Request active{};unsigned failedRevision=~0u;auto next=Clock::now();
        auto close=[&]{if(session){try{session.Close();}catch(...){}session=nullptr;}if(pool){try{pool.Close();}catch(...){}pool=nullptr;}std::lock_guard g(mutex_);latest_.reset();slots={};stats_.bytes=0;stats_.last={};};
        try{while(!stop.stop_requested()){
            Request r;{std::lock_guard g(mutex_);r=request_;}
            if(!r.enabled){if(pool)close();state("Custom Desktop paused / disabled; GPU frames released");std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;}
            if(r.revision==failedRevision){std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;}
            try {
                if(!pool||r.monitor!=active.monitor||r.adapter.LowPart!=active.adapter.LowPart||r.adapter.HighPart!=active.adapter.HighPart){
                    close();state("Starting WGC monitor source…");
                    ComPtr<IDXGIFactory1> factory;winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
                    for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.AdapterLuid.LowPart==r.adapter.LowPart&&d.AdapterLuid.HighPart==r.adapter.HighPart){adapter=a;break;}}
                    if(!adapter)winrt::throw_hresult(DXGI_ERROR_UNSUPPORTED);
                    device.Reset();context.Reset();winrt::check_hresult(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
                    static const char code[]=R"(Texture2D<float4> inputTexture:register(t0);RWTexture2D<float4> outputTexture:register(u0);cbuffer Params:register(b0){uint2 origin;uint2 extent;};[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(all(id.xy<extent))outputTexture[id.xy]=inputTexture.Load(int3(origin+id.xy,0));})";
                    ComPtr<ID3DBlob> blob,errors;winrt::check_hresult(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors));shader.Reset();winrt::check_hresult(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader));
                    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.Reset();winrt::check_hresult(device->CreateBuffer(&bd,nullptr,&constants));
                    disjoint.Reset();beginTime.Reset();endTime.Reset();D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT,0};winrt::check_hresult(device->CreateQuery(&qd,&disjoint));qd.Query=D3D11_QUERY_TIMESTAMP;winrt::check_hresult(device->CreateQuery(&qd,&beginTime));winrt::check_hresult(device->CreateQuery(&qd,&endTime));gpuPending=false;
                    ComPtr<IDXGIDevice> dxgi;device.As(&dxgi);ComPtr<::IInspectable> ins;winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),&ins));W::Foundation::IInspectable projected{ins.Detach(),winrt::take_ownership_from_abi};auto direct=projected.as<W::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
                    auto factoryItem=winrt::get_activation_factory<C::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();C::GraphicsCaptureItem item{nullptr};winrt::check_hresult(factoryItem->CreateForMonitor(r.monitor,winrt::guid_of<C::GraphicsCaptureItem>(),winrt::put_abi(item)));
                    pool=C::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,W::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);
                    // No asynchronous consent or global permission change. WGC's monitor outline may remain.
                    session.StartCapture();{std::lock_guard g(mutex_);++stats_.generation;}active=r;
                }
                if(Clock::now()<next){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}next=Clock::now()+std::chrono::milliseconds(33);
                auto frame=pool.TryGetNextFrame();if(!frame)continue;for(int i=0;i<3;++i){auto newer=pool.TryGetNextFrame();if(!newer)break;frame=newer;std::lock_guard g(mutex_);++stats_.discarded;}
                {std::lock_guard g(mutex_);++stats_.available;}
                MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(r.monitor,&mi))winrt::throw_last_error();
                const UINT width=r.crop.right-r.crop.left,height=r.crop.bottom-r.crop.top;
                if(!width||!height||width>4096||height>4096)winrt::throw_hresult(E_INVALIDARG);
                auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();ComPtr<ID3D11Texture2D> input;winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&input)));D3D11_TEXTURE2D_DESC inputDesc{};input->GetDesc(&inputDesc);
                const LONG x=r.crop.left-mi.rcMonitor.left,y=r.crop.top-mi.rcMonitor.top;
                if(x<0||y<0||x+width>inputDesc.Width||y+height>inputDesc.Height)winrt::throw_hresult(E_BOUNDS);
                if(!slots[0]||slots[0]->width!=width||slots[0]->height!=height){
                    std::lock_guard g(mutex_);latest_.reset();slots={};stats_.bytes=0;
                    for(auto& s:slots){s=std::make_shared<Slot>();s->width=width;s->height=height;D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;winrt::check_hresult(device->CreateTexture2D(&d,nullptr,&s->texture));winrt::check_hresult(s->texture.As(&s->key));ComPtr<IDXGIResource> res;s->texture.As(&res);winrt::check_hresult(res->GetSharedHandle(&s->handle));stats_.bytes+=std::uint64_t(width)*height*4;}
                }
                std::shared_ptr<Slot> slot;for(auto& s:slots)if(s.use_count()==1){slot=s;break;}if(!slot){std::lock_guard g(mutex_);++stats_.discarded;continue;}
                if(slot->key->AcquireSync(0,0)!=S_OK){std::lock_guard g(mutex_);++stats_.discarded;continue;}
                const auto begin=Clock::now();
                if(gpuPending){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};UINT64 a{},b{};if(context->GetData(disjoint.Get(),&frequency,sizeof(frequency),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&context->GetData(beginTime.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&context->GetData(endTime.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){gpuPending=false;if(!frequency.Disjoint){std::lock_guard g(mutex_);gpuSum+=(b-a)*1000.0/frequency.Frequency;stats_.copyGpuMs=gpuSum/++stats_.gpuSamples;}}}
                const bool timeGpu=!gpuPending;if(timeGpu){context->Begin(disjoint.Get());context->End(beginTime.Get());}
                ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
                const auto hr1=device->CreateShaderResourceView(input.Get(),nullptr,&srv),hr2=device->CreateUnorderedAccessView(slot->texture.Get(),nullptr,&uav);
                if(FAILED(hr1)||FAILED(hr2)){slot->key->ReleaseSync(0);winrt::check_hresult(FAILED(hr1)?hr1:hr2);}
                UINT p[4]{UINT(x),UINT(y),width,height};context->UpdateSubresource(constants.Get(),0,nullptr,p,0,0);context->CSSetShader(shader.Get(),nullptr,0);auto cb=constants.Get();context->CSSetConstantBuffers(0,1,&cb);auto v=srv.Get();context->CSSetShaderResources(0,1,&v);auto u=uav.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
                context->Dispatch((width+7)/8,(height+7)/8,1);v=nullptr;u=nullptr;context->CSSetShaderResources(0,1,&v);context->CSSetUnorderedAccessViews(0,1,&u,nullptr);if(timeGpu){context->End(endTime.Get());context->End(disjoint.Get());gpuPending=true;}slot->key->ReleaseSync(1);context->Flush();
                winrt::check_hresult(device->GetDeviceRemovedReason());
                auto output=std::make_shared<Frame>();output->slot=slot;output->crop=r.crop;output->arrival=Clock::now();output->sourceTime=frame.SystemRelativeTime().count();
                LARGE_INTEGER qpc{},frequency{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&frequency);const double age=std::max(0.0,(qpc.QuadPart*1000.0/frequency.QuadPart-output->sourceTime/10000.0));
                {std::lock_guard g(mutex_);output->sequence=++stats_.published;stats_.last=output->arrival;stats_.sourceAgeMs=age;stats_.copyCpuMs=millis(begin,output->arrival);stats_.state="WGC monitor running; cropped shared GPU frames";latest_=std::move(output);}
            }catch(const winrt::hresult_error& e){close();failedRevision=r.revision;state("WGC stopped: HRESULT 0x"+hex(unsigned(e.code()))+"; select another mode then Custom Desktop to retry");}
        }}catch(...){state("Desktop worker failed unexpectedly");}
        close();winrt::uninit_apartment();
    }
    static std::string hex(unsigned v){char s[16];sprintf_s(s,"%08X",v);return s;}
public:
    Capture():thread_([this](std::stop_token s){run(s);}){}
    ~Capture(){thread_.request_stop();thread_.join();}
    void request(Request r){std::lock_guard g(mutex_);request_=r;}
    Stats stats(){std::lock_guard g(mutex_);return stats_;}
    std::shared_ptr<Frame> take(){std::lock_guard g(mutex_);return std::exchange(latest_,{});}
};
}
