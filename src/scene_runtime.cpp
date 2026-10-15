#include "scene_runtime.h"
#include "application_picker.h"
#include "timeline.h"
#include "logging.h"
#include "design_system.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <QImageReader>
#include <QPainter>
#include <QFont>
#include <QJsonDocument>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

namespace Scenes {
namespace WGC=winrt::Windows::Graphics::Capture;
namespace DX=winrt::Windows::Graphics::DirectX;
namespace D3D=winrt::Windows::Graphics::DirectX::Direct3D11;
namespace {
void check(HRESULT hr) { winrt::check_hresult(hr); }
QString failure(HRESULT hr) { return QString("HRESULT 0x%1").arg(quint32(hr),8,16,QChar('0')); }
struct Budget {std::atomic_uint64_t bytes{};static constexpr uint64_t Limit=1024ull*1024*1024;};
struct Buffer {
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> target; ComPtr<ID3D11ShaderResourceView> view;
    std::shared_ptr<Budget> budget;uint64_t bytes{};
    ~Buffer(){if(budget)budget->bytes.fetch_sub(bytes);}
    bool reserve(uint64_t requested,const std::shared_ptr<Budget>& value){
        if(!budget)budget=value;if(requested<=bytes){budget->bytes.fetch_sub(bytes-requested);bytes=requested;return true;}
        auto extra=requested-bytes;auto used=budget->bytes.load();do{if(used+extra>Budget::Limit)return false;}while(!budget->bytes.compare_exchange_weak(used,used+extra));bytes=requested;return true;
    }
};
struct Vert { float x,y,u,v; };
class Draw {
    uint64_t serial_{};
    struct CachedView{ComPtr<ID3D11ShaderResourceView> view;uint64_t used{};};
    std::map<ID3D11Texture2D*,CachedView> views_;
public:
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout; ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11BlendState> blend;
    void begin(){++serial_;for(auto it=views_.begin();it!=views_.end();)if(it->second.used+2<serial_)it=views_.erase(it);else ++it;}
    void init(ID3D11Device* d) {
        if(vs)return;
        const char* shader=R"(
            struct V { float2 xy:POSITION; float2 uv:TEXCOORD; };
            struct P { float4 xy:SV_Position; float2 uv:TEXCOORD; };
            P vertex(V v){P p;p.xy=float4(v.xy,0,1);p.uv=v.uv;return p;}
            Texture2D image:register(t0); SamplerState sampling:register(s0);
            float4 pixel(P p):SV_Target{return image.Sample(sampling,p.uv);}
        )";
        ComPtr<ID3DBlob> vb,pb,errors;
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&vb,&errors));
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&pb,&errors));
        check(d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs));
        check(d->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps));
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(d->CreateInputLayout(elements,2,vb->GetBufferPointer(),vb->GetBufferSize(),&layout));
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Vert)*1024;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(d->CreateBuffer(&bd,nullptr,&vertices));
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(d->CreateSamplerState(&sd,&sampler));
        D3D11_BLEND_DESC b{};auto& r=b.RenderTarget[0];r.BlendEnable=TRUE;r.SrcBlend=D3D11_BLEND_ONE;r.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOp=D3D11_BLEND_OP_ADD;r.SrcBlendAlpha=D3D11_BLEND_ONE;r.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;r.BlendOpAlpha=D3D11_BLEND_OP_ADD;r.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(d->CreateBlendState(&b,&blend));
    }
    void draw(GpuResources& gpu, ID3D11Texture2D* image, const Vert* data, UINT count, D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) {
        auto c=gpu.context.Get();D3D11_MAPPED_SUBRESOURCE map{};check(c->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map));memcpy(map.pData,data,count*sizeof(Vert));c->Unmap(vertices.Get(),0);
        auto& cached=views_[image];if(!cached.view)check(gpu.device->CreateShaderResourceView(image,nullptr,&cached.view));cached.used=serial_;
        UINT stride=sizeof(Vert),offset=0;auto vb=vertices.Get();c->IASetInputLayout(layout.Get());c->IASetVertexBuffers(0,1,&vb,&stride,&offset);c->IASetPrimitiveTopology(topology);
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);auto s=sampler.Get();c->PSSetSamplers(0,1,&s);auto v=cached.view.Get();c->PSSetShaderResources(0,1,&v);c->OMSetBlendState(blend.Get(),nullptr,~0u);c->Draw(count,0);v=nullptr;c->PSSetShaderResources(0,1,&v);
    }
};
ComPtr<ID3D11Texture2D> upload(GpuResources& gpu,const QImage& input) {
    auto image=input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    D3D11_TEXTURE2D_DESC d{};d.Width=image.width();d.Height=image.height();d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.constBits(),UINT(image.bytesPerLine()),0};ComPtr<ID3D11Texture2D> texture;check(gpu.device->CreateTexture2D(&d,&data,&texture));return texture;
}
struct SourceState {
    std::mutex mutex; std::shared_ptr<GpuResources> gpu;
    std::vector<std::shared_ptr<Buffer>> buffers;
    RecordedFrame latest; std::atomic_bool closed{}, stopping{};
    std::atomic_uint64_t frames{}, drops{};
    QString error; winrt::Windows::Graphics::SizeInt32 size{};
    std::weak_ptr<RecordingIngress> ingress;
    std::shared_ptr<Budget> budget;
};
void copyFrame(const std::shared_ptr<SourceState>& state,ID3D11Texture2D* texture,std::chrono::steady_clock::time_point timestamp) {
    if(state->stopping)return;
    std::lock_guard lock(state->mutex);
    auto ingress=state->ingress.lock();if(ingress)ingress->noteAvailableFrame(timestamp);
    std::shared_ptr<Buffer> buffer;
    for(auto& b:state->buffers)if(b.use_count()==1){buffer=b;break;}
    if(!buffer){++state->drops;if(ingress)ingress->gpuPoolDrops++;return;}
    D3D11_TEXTURE2D_DESC d{},old{};texture->GetDesc(&d);if(buffer->texture)buffer->texture->GetDesc(&old);
    std::lock_guard context(*state->gpu->contextMutex);
    if(!buffer->texture||old.Width!=d.Width||old.Height!=d.Height||old.Format!=d.Format){if(!buffer->reserve(uint64_t(d.Width)*d.Height*4,state->budget)){state->error="Source GPU memory budget reached (1 GiB); reduce source count/resolution.";++state->drops;return;}d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;buffer->texture.Reset();check(state->gpu->device->CreateTexture2D(&d,nullptr,&buffer->texture));}
    state->gpu->context->CopyResource(buffer->texture.Get(),texture);
    state->latest={buffer->texture,timestamp,buffer};++state->frames;
}
class WindowSource {
public:
    std::shared_ptr<SourceState> state;
    WGC::GraphicsCaptureItem item{nullptr};WGC::Direct3D11CaptureFramePool pool{nullptr};WGC::GraphicsCaptureSession session{nullptr};
    D3D::IDirect3DDevice device{nullptr};winrt::event_token arrived{},closed{};
    ~WindowSource(){stop();}
    void stop(){if(state)state->stopping=true;try{if(pool)pool.FrameArrived(arrived);if(item)item.Closed(closed);if(session)session.Close();if(pool)pool.Close();}catch(...){}session=nullptr;pool=nullptr;item=nullptr;}
    void start(std::shared_ptr<GpuResources> gpu,HWND hwnd,HMONITOR monitor,BorderPermission permission,const std::shared_ptr<Budget>& budget){
        state=std::make_shared<SourceState>();state->gpu=gpu;state->budget=budget;for(int i=0;i<4;++i)state->buffers.push_back(std::make_shared<Buffer>());
        auto interop=winrt::get_activation_factory<WGC::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        if(monitor)check(interop->CreateForMonitor(monitor,winrt::guid_of<WGC::GraphicsCaptureItem>(),winrt::put_abi(item)));
        else check(interop->CreateForWindow(hwnd,winrt::guid_of<WGC::GraphicsCaptureItem>(),winrt::put_abi(item)));
        ComPtr<IDXGIDevice> dxgi;check(gpu->device.As(&dxgi));winrt::com_ptr<IInspectable> inspect;check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspect.put()));device=inspect.as<D3D::IDirect3DDevice>();
        state->size=item.Size();if(state->size.Width<=0||state->size.Height<=0)throw winrt::hresult_error(E_INVALIDARG);
        pool=WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(device,DX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,state->size);session=pool.CreateCaptureSession(item);
        try{applyBorderlessPolicy(permission,session);}catch(winrt::hresult_error const& e){logLine(L"Scene WGC border property failed: 0x%08X. Outlined capture continues.",unsigned(e.code().value));}
        closed=item.Closed([s=state](auto&,auto&){s->closed=true;});
        arrived=pool.FrameArrived([s=state](auto& sender,auto&){
            try{if(s->stopping)return;auto frame=sender.TryGetNextFrame();if(!frame)return;
                auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();ComPtr<ID3D11Texture2D> texture;check(access->GetInterface(IID_PPV_ARGS(&texture)));
                auto timestamp=qpcTime(frame.SystemRelativeTime().count());auto now=std::chrono::steady_clock::now();if(timestamp>now+std::chrono::milliseconds(100)||timestamp<now-std::chrono::seconds(5))timestamp=now;
                copyFrame(s,texture.Get(),timestamp);
            }catch(winrt::hresult_error const& e){std::lock_guard lock(s->mutex);s->error=failure(e.code());s->closed=true;}
        });session.StartCapture();
    }
    void resize(){auto size=item.Size();if(size.Width>0&&size.Height>0&&(size.Width!=state->size.Width||size.Height!=state->size.Height)){pool.Recreate(device,DX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);state->size=size;}}
    void permission(BorderPermission p){applyBorderlessPolicy(p,session);}
};
// Asynchronous MF reader: device stalls cannot block the compositor or UI.
class CameraCallback final:public IMFSourceReaderCallback {
    std::atomic_ulong refs_{1};
public:
    std::shared_ptr<SourceState> state; ComPtr<IMFSourceReader> reader;
    UINT width{},height{};LONG stride{};std::mutex mutex;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override {if(id==__uuidof(IUnknown)||id==__uuidof(IMFSourceReaderCallback)){*out=static_cast<IMFSourceReaderCallback*>(this);AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD,IMFMediaEvent*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr,DWORD,DWORD flags,LONGLONG,IMFSample* sample)override{
        std::lock_guard lock(mutex);if(state->stopping)return S_OK;
        try{
            check(hr);if(flags&MF_SOURCE_READERF_ENDOFSTREAM)throw winrt::hresult_error(MF_E_END_OF_STREAM);
            if(sample){ComPtr<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer));
                ComPtr<IMF2DBuffer> twoD;buffer.As(&twoD);BYTE* bytes{};DWORD count{};LONG pitch=stride;
                bool locked2D=twoD&&SUCCEEDED(twoD->Lock2D(&bytes,&pitch));
                if(!locked2D){check(buffer->Lock(&bytes,nullptr,&count));if(count<uint64_t(std::abs(pitch))*height){buffer->Unlock();throw winrt::hresult_error(E_INVALIDARG);}if(pitch<0)bytes+=(height-1)*(-pitch);}
                QImage image(int(width),int(height),QImage::Format_ARGB32);
                // Lock2D supplies the top row and actual signed pitch. RGB32 is
                // BGRX, so its unused byte must not become scene transparency.
                if(std::abs(pitch)<LONG(width*4)){if(locked2D)twoD->Unlock2D();else buffer->Unlock();throw winrt::hresult_error(E_INVALIDARG);}
                for(UINT y=0;y<height;++y){auto row=bytes+int64_t(y)*pitch;auto dst=reinterpret_cast<QRgb*>(image.scanLine(int(y)));for(UINT x=0;x<width;++x)dst[x]=qRgb(row[x*4+2],row[x*4+1],row[x*4]);}
                if(locked2D)twoD->Unlock2D();else buffer->Unlock();
                auto texture=upload(*state->gpu,image);copyFrame(state,texture.Get(),std::chrono::steady_clock::now());
            }
            if(reader&&!state->stopping)check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,nullptr,nullptr,nullptr));
        }catch(winrt::hresult_error const& e){std::lock_guard status(state->mutex);state->error=failure(e.code());state->closed=true;}
        return S_OK;
    }
};
class CameraSource {
public:
    std::shared_ptr<SourceState> state;ComPtr<IMFMediaSource> source;ComPtr<CameraCallback> callback;
    ~CameraSource(){if(state)state->stopping=true;ComPtr<IMFSourceReader> reader;if(callback){std::lock_guard lock(callback->mutex);reader=callback->reader;callback->reader.Reset();}if(reader)reader->Flush(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM));if(source)source->Shutdown();}
    void start(std::shared_ptr<GpuResources> gpu,const QString& id,const std::shared_ptr<Budget>& budget){
        state=std::make_shared<SourceState>();state->gpu=gpu;state->budget=budget;for(int i=0;i<4;++i)state->buffers.push_back(std::make_shared<Buffer>());
        ComPtr<IMFAttributes> attributes;check(MFCreateAttributes(&attributes,3));check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));check(attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,id.toStdWString().c_str()));
        check(MFCreateDeviceSource(attributes.Get(),&source));callback.Attach(new CameraCallback);callback->state=state;
        check(MFCreateAttributes(&attributes,3));check(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get()));check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
        check(MFCreateSourceReaderFromMediaSource(source.Get(),attributes.Get(),&callback->reader));
        ComPtr<IMFMediaType> type;check(MFCreateMediaType(&type));check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));check(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));check(callback->reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,type.Get()));check(callback->reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&type));
        check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&callback->width,&callback->height));UINT32 stride{};if(SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride)))callback->stride=LONG(stride);else check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,callback->width,&callback->stride));
        check(callback->reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,nullptr,nullptr,nullptr));
    }
};
struct Source {
    Definition definition;std::unique_ptr<WindowSource> window;std::unique_ptr<CameraSource> camera;
    RecordedFrame asset;QString status;unsigned attempts{};std::chrono::steady_clock::time_point retry{};HWND target{};
    std::shared_ptr<SourceState> live()const{return window?window->state:camera?camera->state:nullptr;}
};
QImage asset(const Definition& d){
    if(d.kind==Kind::Image){QImageReader reader(d.settings["path"].toString());reader.setAutoTransform(true);auto size=reader.size();if(size.width()>4096||size.height()>4096)reader.setScaledSize(size.scaled(4096,4096,Qt::KeepAspectRatio));return reader.read();}
    if(d.kind==Kind::Text){QImage image(d.intrinsic.toSize().boundedTo({4096,4096}),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::TextAntialiasing);QFont f(d.settings["font"].toString("Segoe UI"));f.setPixelSize(d.settings["fontSize"].toInt(64));p.setFont(f);p.setPen(QColor(d.settings["color"].toString("#F5F0FF")));auto align=d.settings["alignment"].toString();p.drawText(image.rect(),Qt::TextWordWrap|Qt::AlignVCenter|(align=="center"?Qt::AlignHCenter:align=="right"?Qt::AlignRight:Qt::AlignLeft),d.settings["text"].toString());return image;}
    return {};
}
}
QVector<Device> displays(){QVector<Device> result;EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM p)->BOOL{MONITORINFOEXW i{};i.cbSize=sizeof(i);if(GetMonitorInfoW(monitor,&i))reinterpret_cast<QVector<Device>*>(p)->append({QString::fromWCharArray(i.szDevice),QString::fromWCharArray(i.szDevice)+(i.dwFlags&MONITORINFOF_PRIMARY?" · Primary":""),QSize(i.rcMonitor.right-i.rcMonitor.left,i.rcMonitor.bottom-i.rcMonitor.top)});return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
QVector<Device> cameras(QString& error){QVector<Device> result;auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool co=SUCCEEDED(hr);bool mf=false;try{check(MFStartup(MF_VERSION));mf=true;ComPtr<IMFAttributes> a;check(MFCreateAttributes(&a,1));check(a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));IMFActivate** devices{};UINT count{};check(MFEnumDeviceSources(a.Get(),&devices,&count));for(UINT n=0;n<count;++n){wchar_t *id{},*name{};UINT length{};devices[n]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&id,&length);devices[n]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&length);if(id)result.append({QString::fromWCharArray(id),QString::fromWCharArray(name?name:id),{1280,720}});CoTaskMemFree(id);CoTaskMemFree(name);devices[n]->Release();}CoTaskMemFree(devices);}catch(winrt::hresult_error const& e){error=failure(e.code());}if(mf)MFShutdown();if(co)CoUninitialize();return result;}

struct Runtime::Impl {
    std::shared_ptr<GpuResources> gpu; HWND notify;UINT message;quintptr desktop;
    mutable std::mutex mutex;std::condition_variable wake;Snapshot configured;VideoConfig video;
    std::shared_ptr<RecordingIngress> ingress;RecordedFrame output;RuntimeStatus diagnostics;
    std::atomic_bool stopping{},running{true},suspended{},cursor{};std::atomic<BorderPermission> permission{BorderPermission::Unrequested};
    // Unnamed, process-local cancellation; no global timer-resolution policy.
    winrt::handle stopEvent{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread worker;
    std::shared_ptr<Budget> budget=std::make_shared<Budget>();
#ifdef REDOT_UI_VERIFICATION
    QHash<QString,RecordedFrame> fixtures;
#endif
    Impl(std::shared_ptr<GpuResources> g,HWND n,UINT m):gpu(std::move(g)),notify(n),message(m),desktop(quintptr(GetThreadDesktop(GetCurrentThreadId()))){worker=std::thread([this]{run();});}
    void run()noexcept{
        bool apartment=false,mf=false;try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);apartment=true;check(MFStartup(MF_VERSION));mf=true;
            if(!gpu||!gpu->device||!gpu->context)throw winrt::hresult_error(E_FAIL);
            winrt::handle frameTimer{CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE)};
#ifdef REDOT_UI_VERIFICATION
            if(diagnosticSetting(L"PANDACLIP_TEST_COARSE_COMPOSITOR"))frameTimer.close();
#endif
            const bool preciseWait=bool(frameTimer)&&bool(stopEvent);
            logLine(L"Scene compositor deadline wait: %s; no process-wide timer-resolution change.",preciseWait?L"private high-resolution waitable timer":L"condition-variable fallback (high-resolution timer/cancellation unavailable)");
            Draw draw;draw.init(gpu->device.Get());std::map<QString,Source> sources;
            struct Timing {ComPtr<ID3D11Query> disjoint,start,end;bool pending{};};
            std::array<Timing,8> timings;for(auto& timing:timings){D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};check(gpu->device->CreateQuery(&d,&timing.disjoint));d.Query=D3D11_QUERY_TIMESTAMP;check(gpu->device->CreateQuery(&d,&timing.start));check(gpu->device->CreateQuery(&d,&timing.end));}
            double gpuSum=0;
            std::vector<std::shared_ptr<Buffer>> buffers;for(int i=0;i<12;++i)buffers.push_back(std::make_shared<Buffer>());
            auto next=std::chrono::steady_clock::now();auto discoveryAt=next;ApplicationDiscovery discovery;
            auto lastPermission=permission.load();auto lastCursor=cursor.load();RuntimeStatus status;double sum=0;
            while(!stopping){
                Snapshot snapshot;VideoConfig config;{std::lock_guard lock(mutex);snapshot=configured;config=video;}
                const Scene* scene=nullptr;for(auto& s:snapshot.scenes)if(s.id==snapshot.selected){scene=&s;break;}
                if(!scene||suspended){sources.clear();std::unique_lock lock(mutex);output={};wake.wait_for(lock,std::chrono::milliseconds(100));next=std::chrono::steady_clock::now();continue;}
                auto now=std::chrono::steady_clock::now();bool needsDiscovery=false;QSet<QString> needed;
                for(auto& v:scene->visuals)if(v.visible)needed.insert(v.sourceId);
                // Keep previously opened sources across scene switches, but bound
                // live sessions to eight and retire unused sessions after 30 s.
                // Desktop-wide discovery (every window, every process) costs ~60 ms and runs on this frame-pacing thread, so run it only when a
                // Game/Window source is actually about to be opened or retried, never while its session is live and healthy.
                for(auto& d:snapshot.definitions)if(needed.contains(d.id)&&(d.kind==Kind::Game||d.kind==Kind::Window)){
                    const auto known=sources.find(d.id);
                    if(known==sources.end()){needsDiscovery=true;continue;}
                    auto& existing=known->second;
                    if(existing.definition.id.isEmpty()||existing.definition.kind!=d.kind||existing.definition.settings!=d.settings||existing.definition.intrinsic!=d.intrinsic){needsDiscovery=true;continue;}
                    if(auto live=existing.live();live&&(live->closed||(existing.target&&!IsWindow(existing.target)))){needsDiscovery=true;continue;}
                    if(!existing.live()&&!existing.asset.texture&&existing.attempts<3&&now>=existing.retry)needsDiscovery=true;
                }
                if(needsDiscovery&&now>=discoveryAt){discovery=discoverApplications(desktop,GetCurrentProcessId(),false);discoveryAt=now+std::chrono::seconds(2);}
                for(auto it=sources.begin();it!=sources.end();){if(!needed.contains(it->first)&&now-it->second.retry>std::chrono::seconds(30))it=sources.erase(it);else ++it;}
                size_t opened=0;for(auto& [id,s]:sources)if(s.live())++opened;
                for(auto& d:snapshot.definitions)if(needed.contains(d.id)&&!isAudio(d.kind)){
                    auto& s=sources[d.id];
                    if(s.definition.id.isEmpty()||s.definition.kind!=d.kind||s.definition.settings!=d.settings||s.definition.intrinsic!=d.intrinsic){s=Source{};s.definition=d;}
                    if(auto live=s.live();live&&(live->closed||(s.target&&!IsWindow(s.target)))){s.window.reset();s.camera.reset();s.status="Unavailable · selected source closed/disconnected";s.retry=now+std::chrono::seconds(2);}
                    if(auto live=s.live()){if(s.window){try{s.window->resize();if(lastPermission!=permission.load()){try{s.window->permission(permission.load());}catch(...){logLine(L"Scene border permission property failed; outlined source continues.");}}if(lastCursor!=cursor.load())s.window->session.IsCursorCaptureEnabled(cursor.load());}catch(...){live->closed=true;}}s.retry=now;continue;}
                    if(s.asset.texture)continue;
                    if(s.attempts>=3||now<s.retry)continue;
                    try{
                        if(d.kind==Kind::Image||d.kind==Kind::Text){auto image=asset(d);if(image.isNull())throw winrt::hresult_error(E_INVALIDARG);auto owner=std::make_shared<Buffer>();if(!owner->reserve(uint64_t(image.width())*image.height()*4,budget))throw winrt::hresult_error(E_OUTOFMEMORY);owner->texture=upload(*gpu,image);s.asset={owner->texture,now,owner};s.status="Ready";}
                        else{
                            if(opened>=8){s.status="Unavailable · eight live video sources limit";continue;}
                            if(d.kind==Kind::Camera){s.camera=std::make_unique<CameraSource>();s.camera->start(gpu,d.settings["target"].toString(),budget);s.status="Camera opening · MF RGB32 upload";}
                            else{HWND target=nullptr;HMONITOR monitor=nullptr;
                                if(d.kind==Kind::Display){auto id=d.settings["target"].toString();struct Match{QString id;HMONITOR result{};} match{id};EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR m,HDC,LPRECT,LPARAM p)->BOOL{auto& match=*reinterpret_cast<Match*>(p);MONITORINFOEXW i{};i.cbSize=sizeof(i);GetMonitorInfoW(m,&i);if(match.id==QString::fromWCharArray(i.szDevice))match.result=m;return TRUE;},reinterpret_cast<LPARAM>(&match));monitor=match.result;if(!monitor){s.status="Unavailable · selected display missing";s.retry=now+std::chrono::seconds(2);continue;}}
                                else{QVector<ApplicationWindow> matches;for(auto& w:discovery.windows)if(w.executable.compare(d.settings["executable"].toString(),Qt::CaseInsensitive)==0&&w.title==d.settings["target"].toString()&&(d.settings["windowClass"].toString().isEmpty()||w.windowClass==d.settings["windowClass"].toString()))matches.append(w);
                                    if(matches.size()!=1){s.status=matches.isEmpty()?"Unavailable · reopen the selected executable/window title":"Unavailable · target ambiguous; select a specific window";s.retry=now+std::chrono::seconds(2);continue;}target=HWND(matches[0].handle);}
                                s.target=target;s.window=std::make_unique<WindowSource>();s.window->start(gpu,target,monitor,permission.load(),budget);try{s.window->session.IsCursorCaptureEnabled(cursor.load());}catch(...){}s.status="WGC opening";
                            }++opened;++s.attempts;
                        }
                    }catch(winrt::hresult_error const& e){s.window.reset();s.camera.reset();++s.attempts;s.status="Unavailable · "+failure(e.code())+QString(" · attempt %1/3").arg(s.attempts);s.retry=now+std::chrono::seconds(2);}
                }
                lastPermission=permission.load();lastCursor=cursor.load();
                auto begin=std::chrono::steady_clock::now();auto interval=std::chrono::nanoseconds(1'000'000'000/std::max(1u,config.fps));
                // Label canonical frames by their scheduled composition slot,
                // rather than scheduler wake jitter. Skip missed slots instead
                // of bursting; the independent CFR encoder repeats if needed.
                if(begin>=next+interval){auto missed=(begin-next)/interval;next+=interval*missed;status.lateSlots+=uint64_t(missed);}
                std::shared_ptr<Buffer> buffer;for(auto& b:buffers)if(b.use_count()==1){buffer=b;break;}
                struct Layer {RecordedFrame image;Vert vertices[4];};std::vector<Layer> layers;layers.reserve(size_t(scene->visuals.size()));
                for(auto& v:scene->visuals)if(v.visible){auto found=sources.find(v.sourceId);if(found==sources.end())continue;auto& s=found->second;auto image=s.asset;
                    if(auto live=s.live()){std::shared_ptr<RecordingIngress> destination;{std::lock_guard lock(mutex);destination=ingress;}std::lock_guard sourceLock(live->mutex);image=live->latest;live->ingress=destination;if(live->closed||!live->error.isEmpty()){image={};s.status="Unavailable · "+live->error;}else if(image.texture)s.status="Live";}
#ifdef REDOT_UI_VERIFICATION
                    {std::lock_guard lock(mutex);if(fixtures.contains(v.sourceId))image=fixtures.value(v.sourceId);}
#endif
                    if(!image.texture)continue;auto transform=v.transform();QRectF fitted(0,0,v.width,v.height);auto kind=s.definition.kind;
                    if(v.aspectLocked&&(kind==Kind::Game||kind==Kind::Window||kind==Kind::Display||kind==Kind::Camera)){D3D11_TEXTURE2D_DESC size{};image.texture->GetDesc(&size);auto scale=std::min(v.width/size.Width,v.height/size.Height);auto w=size.Width*scale,h=size.Height*scale;fitted=QRectF((v.width-w)/2,(v.height-h)/2,w,h);}
                    auto visible=fitted.intersected(QRectF(v.cropLeft,v.cropTop,v.width-v.cropLeft-v.cropRight,v.height-v.cropTop-v.cropBottom));if(visible.isEmpty())continue;
                    QPointF local[]={visible.topLeft(),visible.topRight(),visible.bottomLeft(),visible.bottomRight()};
                    auto scale=std::min(double(config.width)/scene->width,double(config.height)/scene->height);auto dx=(config.width-scene->width*scale)/2,dy=(config.height-scene->height*scale)/2;
                    Layer layer;layer.image=std::move(image);for(int i=0;i<4;++i){auto point=transform.map(local[i]);layer.vertices[i]={float((dx+point.x()*scale)/config.width*2-1),float(1-(dy+point.y()*scale)/config.height*2),float((local[i].x()-fitted.x())/fitted.width()),float((local[i].y()-fitted.y())/fitted.height())};}layers.push_back(std::move(layer));
                }
                if(buffer){
                    D3D11_TEXTURE2D_DESC old{};if(buffer->texture)buffer->texture->GetDesc(&old);
                    std::unique_lock context(*gpu->contextMutex);
                    if(!buffer->texture||!buffer->target||!buffer->view||old.Width!=config.width||old.Height!=config.height){if(!buffer->reserve(uint64_t(config.width)*config.height*4,budget))throw winrt::hresult_error(E_OUTOFMEMORY);buffer->view.Reset();buffer->texture.Reset();buffer->target.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=config.width;d.Height=config.height;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;check(gpu->device->CreateTexture2D(&d,nullptr,&buffer->texture));check(gpu->device->CreateRenderTargetView(buffer->texture.Get(),nullptr,&buffer->target));check(gpu->device->CreateShaderResourceView(buffer->texture.Get(),nullptr,&buffer->view));}
                    auto c=gpu->context.Get();auto rt=buffer->target.Get();c->OMSetRenderTargets(1,&rt,nullptr);float black[]={0,0,0,1};c->ClearRenderTargetView(rt,black);D3D11_VIEWPORT vp{0,0,float(config.width),float(config.height),0,1};c->RSSetViewports(1,&vp);
                    Timing* measured=nullptr;for(auto& timing:timings){
                        if(timing.pending){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};UINT64 a{},b{};
                            if(c->GetData(timing.disjoint.Get(),&data,sizeof(data),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&c->GetData(timing.start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&c->GetData(timing.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){timing.pending=false;if(!data.Disjoint&&data.Frequency&&b>=a){auto ms=double(b-a)*1000/data.Frequency;gpuSum+=ms;++status.gpuSamples;status.gpuMeanMs=gpuSum/status.gpuSamples;status.gpuMaxMs=std::max(status.gpuMaxMs,ms);}}
                        }if(!timing.pending&&!measured)measured=&timing;
                    }
                    if(measured){c->Begin(measured->disjoint.Get());c->End(measured->start.Get());}
                    draw.begin();for(auto& layer:layers)draw.draw(*gpu,layer.image.texture.Get(),layer.vertices,4);
                    if(measured){c->End(measured->end.Get());c->End(measured->disjoint.Get());measured->pending=true;}
                    c->OMSetRenderTargets(0,nullptr,nullptr);context.unlock();
                    RecordedFrame frame{buffer->texture,next,buffer,buffer->view};std::shared_ptr<RecordingIngress> destination;
                    {std::lock_guard lock(mutex);output=frame;destination=ingress;}
                    if(destination)destination->push(frame);++status.frames;
                    if(notify)PostMessageW(notify,message,0,0);
                }else ++status.poolDrops;
                auto duration=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();sum+=duration;status.composeMeanMs=sum/std::max<uint64_t>(1,status.frames);status.composeMaxMs=std::max(status.composeMaxMs,duration);
#ifdef REDOT_TEST_FIXTURE
                ++status.verificationComposeHistogram[std::min<size_t>(400,size_t(duration*20))];
#endif
                status.sources.clear();status.sourceFrames=status.sourceDrops=0;for(auto& [id,s]:sources){status.sources[id]=s.status;if(auto live=s.live()){status.sourceFrames+=live->frames;status.sourceDrops+=live->drops;}}
                status.gpuBytes=budget->bytes.load();{std::lock_guard lock(mutex);diagnostics=status;}
                next+=std::chrono::nanoseconds(1'000'000'000/std::max(1u,config.fps));auto end=std::chrono::steady_clock::now();if(next<end-std::chrono::milliseconds(100))next=end;
                bool waited=false;
                if(preciseWait&&!stopping){
                    const auto remaining=next-std::chrono::steady_clock::now();
                    if(remaining>std::chrono::steady_clock::duration::zero()){
                        LARGE_INTEGER due{};
                        due.QuadPart=-std::max<int64_t>(1,(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()+99)/100);
                        if(SetWaitableTimerEx(frameTimer.get(),&due,0,nullptr,nullptr,nullptr,0)){
                            HANDLE handles[]{stopEvent.get(),frameTimer.get()};
                            const auto outcome=WaitForMultipleObjects(2,handles,FALSE,1000);
                            waited=outcome==WAIT_OBJECT_0||outcome==WAIT_OBJECT_0+1;
                        }
                    }else waited=true;
                }
                if(!waited){std::unique_lock lock(mutex);wake.wait_until(lock,next,[this]{return stopping.load();});}
            }
        }catch(winrt::hresult_error const& e){std::lock_guard lock(mutex);diagnostics.error="Scene compositor stopped: "+failure(e.code());}
        catch(...){std::lock_guard lock(mutex);diagnostics.error="Scene compositor stopped unexpectedly.";}
        if(mf)MFShutdown();if(apartment)winrt::uninit_apartment();running=false;
    }
};
Runtime::Runtime(std::shared_ptr<GpuResources> gpu,HWND n,UINT m):impl_(std::make_unique<Impl>(std::move(gpu),n,m)){}
Runtime::~Runtime(){requestStop();if(impl_->worker.joinable())impl_->worker.join();}
void Runtime::configure(Snapshot s,VideoConfig output){std::lock_guard lock(impl_->mutex);impl_->configured=std::move(s);impl_->video=output;impl_->wake.notify_one();}
void Runtime::attach(std::shared_ptr<RecordingIngress> input){if(input)input->composedVideo=true;RecordedFrame frame;{std::lock_guard lock(impl_->mutex);impl_->ingress=input;frame=impl_->output;}if(input&&frame.texture){frame.capturedAt=std::chrono::steady_clock::now();input->startupSnapshots++;input->push(std::move(frame));}}
void Runtime::borderPermission(BorderPermission p){impl_->permission=p;}
void Runtime::suspend(bool value){impl_->suspended=value;impl_->wake.notify_one();}
void Runtime::requestStop(){impl_->stopping=true;if(impl_->stopEvent)SetEvent(impl_->stopEvent.get());impl_->wake.notify_one();}
bool Runtime::busy()const{return impl_->running;}
void Runtime::cursorEnabled(bool enabled){impl_->cursor=enabled;}
bool Runtime::active()const{return impl_->running&&!impl_->suspended;}
RecordedFrame Runtime::latest()const{std::lock_guard lock(impl_->mutex);return impl_->running?impl_->output:RecordedFrame{};}
RuntimeStatus Runtime::status()const{std::lock_guard lock(impl_->mutex);return impl_->diagnostics;}
Snapshot Runtime::snapshot()const{std::lock_guard lock(impl_->mutex);return impl_->configured;}
#ifdef REDOT_UI_VERIFICATION
void Runtime::verificationFrame(const QString& source,RecordedFrame frame){std::lock_guard lock(impl_->mutex);if(impl_->fixtures.size()<8||impl_->fixtures.contains(source))impl_->fixtures[source]=std::move(frame);}
#endif

struct Presentation::Impl{HWND target{};UINT width{},height{};ComPtr<ID3D11Device> device;ComPtr<IDXGISwapChain1> swap;ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11Texture2D> line;Draw draw;};
Presentation::Presentation():impl_(std::make_unique<Impl>()){}Presentation::~Presentation()=default;
bool Presentation::render(HWND target,const std::shared_ptr<GpuResources>& gpu,const RecordedFrame& frame,const QRectF& viewport,const QVector<QPolygonF>& lines,const QVector<QPointF>& handles){
    if(!gpu||!target||!IsWindow(target))return false;
    try{if(impl_->device.Get()!=gpu->device.Get()){impl_=std::make_unique<Impl>();impl_->device=gpu->device;}RECT client{};GetClientRect(target,&client);UINT width=std::max(1L,client.right),height=std::max(1L,client.bottom);auto& p=*impl_;std::lock_guard context(*gpu->contextMutex);p.draw.init(gpu->device.Get());p.draw.begin();
        if(!p.swap||p.target!=target){p.swap.Reset();p.rt.Reset();ComPtr<IDXGIDevice> device;check(gpu->device.As(&device));ComPtr<IDXGIAdapter> adapter;check(device->GetAdapter(&adapter));ComPtr<IDXGIFactory2> factory;check(adapter->GetParent(IID_PPV_ARGS(&factory)));DXGI_SWAP_CHAIN_DESC1 d{};d.Width=width;d.Height=height;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;check(factory->CreateSwapChainForHwnd(gpu->device.Get(),target,&d,nullptr,nullptr,&p.swap));factory->MakeWindowAssociation(target,DXGI_MWA_NO_ALT_ENTER);p.target=target;p.width=width;p.height=height;}
        if(width!=p.width||height!=p.height){p.rt.Reset();gpu->context->OMSetRenderTargets(0,nullptr,nullptr);check(p.swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0));p.width=width;p.height=height;}
        if(!p.rt){ComPtr<ID3D11Texture2D> back;check(p.swap->GetBuffer(0,IID_PPV_ARGS(&back)));check(gpu->device->CreateRenderTargetView(back.Get(),nullptr,&p.rt));}
        auto c=gpu->context.Get();auto rt=p.rt.Get();c->OMSetRenderTargets(1,&rt,nullptr);auto matte=DesignSystem::color("surfaceSolid");float clear[]={float(matte.redF()),float(matte.greenF()),float(matte.blueF()),1};c->ClearRenderTargetView(rt,clear);D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&vp);
        auto vertex=[&](QPointF pt,float u=0,float v=0){return Vert{float(pt.x()/width*2-1),float(1-pt.y()/height*2),u,v};};
        if(frame.texture){auto r=viewport.isEmpty()?QRectF(0,0,width,height):viewport;D3D11_TEXTURE2D_DESC d{};frame.texture->GetDesc(&d);auto bounds=r;float u0=0,v0=0,u1=1,v1=1;
        // The editor viewport represents scene coordinates. Remove only the
        // canonical output's aspect padding when mapping that same texture.
        auto desired=r.width()/r.height(),actual=double(d.Width)/d.Height;if(actual>desired){auto w=desired/actual;u0=float((1-w)/2);u1=1-u0;}else{auto h=actual/desired;v0=float((1-h)/2);v1=1-v0;}
        Vert quad[]={vertex(bounds.topLeft(),u0,v0),vertex(bounds.topRight(),u1,v0),vertex(bounds.bottomLeft(),u0,v1),vertex(bounds.bottomRight(),u1,v1)};p.draw.draw(*gpu,frame.texture.Get(),quad,4);}
        if(!lines.isEmpty()||!handles.isEmpty()){if(!p.line){QImage image(1,1,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#dfc4ff"));p.line=upload(*gpu,image);}for(auto& polygon:lines){QVector<Vert> verts;for(auto point:polygon)verts.append(vertex(point));if(verts.size()>1&&verts.size()<=1024)p.draw.draw(*gpu,p.line.Get(),verts.constData(),UINT(verts.size()),D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);}const auto half=4.*GetDpiForWindow(target)/96.;for(auto point:handles){QRectF h(point-QPointF(half,half),QSizeF(2*half,2*half));Vert v[]={vertex(h.topLeft()),vertex(h.topRight()),vertex(h.bottomLeft()),vertex(h.bottomRight())};p.draw.draw(*gpu,p.line.Get(),v,4);}}
        c->OMSetRenderTargets(0,nullptr,nullptr);check(p.swap->Present(0,0));return true;
    }catch(...){return false;}
}
}
