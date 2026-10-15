#pragma once
#include "desktop_capture.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGTextureProvider>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QTimer>
#include <QVector2D>
#include <QRunnable>
class DesktopTextureProvider final : public QSGTextureProvider {
    Q_OBJECT
public:
    QSGTexture* value{};
    QSGTexture* texture()const override{return value;}
    void replace(QSGTexture* t){delete value;value=t;Q_EMIT textureChanged();}
    ~DesktopTextureProvider(){delete value;}
};
class DesktopTexture : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY changed)
    Q_PROPERTY(bool live READ live NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double frameAge READ frameAge NOTIFY changed)
    Q_PROPERTY(QVector2D sourceSize READ sourceSize NOTIFY changed)
    Q_PROPERTY(QVector2D sourceOrigin READ sourceOrigin NOTIFY changed)
    glassdesktop::Capture capture_;QTimer timer_;bool enabled_{},live_{};unsigned revision_{};
    glassdesktop::Request request_{};QString state_;double age_{-1};QVector2D size_,origin_;std::atomic_bool adapterReady_{};
    LUID adapter_{};QString adapterName_;mutable DesktopTextureProvider* provider_{};glassdesktop::ComPtr<ID3D11Texture2D> output_;QSize outputSize_;std::atomic_bool aligned_{};std::shared_ptr<glassdesktop::Frame> pendingFrame_;
    std::atomic<qint64> renderSequence_{};std::atomic<double> transferMs_{};std::mutex errorMutex_;QString renderError_;
    std::atomic<std::int64_t> displayedTimestamp_{};
    glassdesktop::ComPtr<ID3D11Query> gpuFrequency_,gpuStart_,gpuEnd_;bool gpuPending_{},gpuTiming_{};double gpuSum_{};std::atomic<double> renderGpuMs_{-1};std::atomic<std::uint64_t> gpuSamples_{};
    class Retire final : public QRunnable {
        DesktopTextureProvider* provider_;glassdesktop::ComPtr<ID3D11Texture2D> output_;
    public:Retire(DesktopTextureProvider* p,glassdesktop::ComPtr<ID3D11Texture2D> t):provider_(p),output_(std::move(t)){}~Retire(){delete provider_;}void run()override{}
    };
#ifdef GLASS_LAB_DESKTOP_CHECK
    bool injectCaptureFailure_{};
#endif
    void beginGpuSpan(){
        auto device=static_cast<ID3D11Device*>(window()->rendererInterface()->getResource(window(),QSGRendererInterface::DeviceResource));if(!device)return;
        if(!gpuFrequency_){D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};if(FAILED(device->CreateQuery(&d,&gpuFrequency_)))return;d.Query=D3D11_QUERY_TIMESTAMP;device->CreateQuery(&d,&gpuStart_);device->CreateQuery(&d,&gpuEnd_);}
        glassdesktop::ComPtr<ID3D11DeviceContext> c;device->GetImmediateContext(&c);
        if(gpuPending_){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{};UINT64 a{},b{};if(c->GetData(gpuFrequency_.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&c->GetData(gpuStart_.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&c->GetData(gpuEnd_.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){gpuPending_=false;if(!f.Disjoint){gpuSum_+=(b-a)*1000.0/f.Frequency;renderGpuMs_=gpuSum_/++gpuSamples_;}}}
        gpuTiming_=!gpuPending_;if(gpuTiming_){window()->beginExternalCommands();c->Begin(gpuFrequency_.Get());c->End(gpuStart_.Get());window()->endExternalCommands();}
    }
    void endGpuSpan(){if(!gpuTiming_)return;auto device=static_cast<ID3D11Device*>(window()->rendererInterface()->getResource(window(),QSGRendererInterface::DeviceResource));if(!device)return;glassdesktop::ComPtr<ID3D11DeviceContext> c;device->GetImmediateContext(&c);window()->beginExternalCommands();c->End(gpuEnd_.Get());c->End(gpuFrequency_.Get());window()->endExternalCommands();gpuPending_=true;gpuTiming_=false;}
    void deliver(){
        // Native GPU commands belong to rendering, not updatePaintNode sync.
        auto frame=std::exchange(pendingFrame_,{});if(!frame||!output_)return;
        auto device=static_cast<ID3D11Device*>(window()->rendererInterface()->getResource(window(),QSGRendererInterface::DeviceResource));if(!device)return;
        const auto start=glassdesktop::Clock::now();glassdesktop::ComPtr<ID3D11Texture2D> shared;glassdesktop::ComPtr<IDXGIKeyedMutex> key;HRESULT hr=device->OpenSharedResource(frame->slot->handle,IID_PPV_ARGS(&shared));if(SUCCEEDED(hr))hr=shared.As(&key);if(SUCCEEDED(hr))hr=key->AcquireSync(1,0);
        if(hr==S_OK){glassdesktop::ComPtr<ID3D11DeviceContext> c;device->GetImmediateContext(&c);window()->beginExternalCommands();c->CopyResource(output_.Get(),shared.Get());window()->endExternalCommands();hr=device->GetDeviceRemovedReason();key->ReleaseSync(0);frame->released=true;if(SUCCEEDED(hr)){renderSequence_=frame->sequence;displayedTimestamp_=frame->sourceTime;aligned_=true;if(provider_)Q_EMIT provider_->textureChanged();}}
        if(FAILED(hr)){std::lock_guard g(errorMutex_);renderError_=QString("Custom GPU transfer failed: 0x%1; solid fallback").arg(quint32(hr),8,16,QChar('0'));}
        transferMs_=glassdesktop::millis(start,glassdesktop::Clock::now());
    }
    void pulse(){
        glassdesktop::Request r{};r.adapter=adapter_;r.revision=revision_;
        QString blocked;
        if(window()&&enabled_&&window()->isVisible()&&window()->visibility()!=QWindow::Minimized&&adapterReady_){
            auto h=reinterpret_cast<HWND>(window()->winId());RECT client{};GetClientRect(h,&client);POINT p{};ClientToScreen(h,&p);r.monitor=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(r.monitor,&mi);
            // Physical HWND client origin and dimensions, never Qt logical coordinates.
            if(p.x<mi.rcMonitor.left||p.y<mi.rcMonitor.top||p.x+client.right>mi.rcMonitor.right||p.y+client.bottom>mi.rcMonitor.bottom)blocked="Custom Desktop fallback: window spans monitor bounds; move fully onto one monitor";
            else {r.enabled=true;r.crop={std::max(mi.rcMonitor.left,p.x-48),std::max(mi.rcMonitor.top,p.y-48),std::min(mi.rcMonitor.right,p.x+client.right+48),std::min(mi.rcMonitor.bottom,p.y+client.bottom+48)};size_=QVector2D(r.crop.right-r.crop.left,r.crop.bottom-r.crop.top);origin_=QVector2D(p.x-r.crop.left,p.y-r.crop.top);}
        }
#ifdef GLASS_LAB_DESKTOP_CHECK
        if(injectCaptureFailure_&&r.enabled)r.monitor=nullptr;
#endif
        const bool wasEnabled=request_.enabled;
        if(!EqualRect(&r.crop,&request_.crop))aligned_=false;
        if(r.monitor!=request_.monitor||r.enabled!=request_.enabled){++revision_;r.revision=revision_;}request_=r;capture_.request(r);
        const auto s=capture_.stats();age_=s.last==glassdesktop::Clock::time_point{}?-1:s.sourceAgeMs+glassdesktop::millis(s.last,glassdesktop::Clock::now());
        if(s.last!=glassdesktop::Clock::time_point{}&&displayedTimestamp_){LARGE_INTEGER qpc{},frequency{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&frequency);age_=std::max(age_,qpc.QuadPart*1000.0/frequency.QuadPart-displayedTimestamp_/10000.0);}
        {std::lock_guard g(errorMutex_);if(!renderError_.isEmpty())blocked=renderError_;}
        live_=r.enabled&&age_>=0&&age_<500&&blocked.isEmpty()&&renderSequence_>0&&aligned_;
        state_=!blocked.isEmpty()?blocked:!r.enabled?"Custom Desktop paused / disabled":age_>=500?"Custom Desktop stale (>500 ms); solid fallback":s.last!=glassdesktop::Clock::time_point{}&&!aligned_?"Waiting for current-position GPU frame; solid fallback":QString::fromStdString(s.state);
        if(r.enabled||wasEnabled||(!adapterReady_&&enabled_))update();Q_EMIT changed();
    }
protected:
    QSGNode* updatePaintNode(QSGNode* previous,UpdatePaintNodeData*)override {
        // Sync stage: the GUI thread is blocked; geometry/adapter publication is coherent.
        auto ri=window()->rendererInterface();auto device=static_cast<ID3D11Device*>(ri->getResource(window(),QSGRendererInterface::DeviceResource));if(!device)return previous;
        if(!adapterReady_){glassdesktop::ComPtr<IDXGIDevice> dx;device->QueryInterface(IID_PPV_ARGS(&dx));glassdesktop::ComPtr<IDXGIAdapter> a;dx->GetAdapter(&a);DXGI_ADAPTER_DESC d{};a->GetDesc(&d);adapter_=d.AdapterLuid;adapterName_=QString::fromWCharArray(d.Description);adapterReady_=true;}
        if(!provider_)provider_=new DesktopTextureProvider;
        if(!request_.enabled&&output_){delete previous;provider_->replace(nullptr);output_.Reset();outputSize_={};renderSequence_=0;displayedTimestamp_=0;return nullptr;}
        auto frame=capture_.take();
        if(frame){
            // Never present a frame from the previous position/configuration.
            if(EqualRect(&frame->crop,&request_.crop)&&enabled_){
                const QSize sz(frame->slot->width,frame->slot->height);HRESULT hr=S_OK;
                if(!output_||outputSize_!=sz){D3D11_TEXTURE2D_DESC desc{};frame->slot->texture->GetDesc(&desc);desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;output_.Reset();hr=device->CreateTexture2D(&desc,nullptr,&output_);if(SUCCEEDED(hr)){outputSize_=sz;provider_->replace(QNativeInterface::QSGD3D11Texture::fromNative(output_.Get(),window(),sz));}}
                if(SUCCEEDED(hr)&&provider_->value)pendingFrame_=std::move(frame);
                else {std::lock_guard g(errorMutex_);renderError_=QString("Custom texture import failed: 0x%1; solid fallback").arg(quint32(hr),8,16,QChar('0'));}
            }
        }
        // Off-scene provider node establishes updatePaintNode without visible raw capture.
        auto node=static_cast<QSGSimpleTextureNode*>(previous);if(!node)node=new QSGSimpleTextureNode;
        if(provider_->value){node->setTexture(provider_->value);node->setRect(-100000,-100000,1,1);}return node;
    }
    void releaseResources()override{adapterReady_=false;}
public:
    DesktopTexture(QQuickItem* parent=nullptr):QQuickItem(parent){setFlag(ItemHasContents);timer_.setInterval(33);connect(&timer_,&QTimer::timeout,this,[this]{pulse();});timer_.start();connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow* w){if(w){connect(w,&QQuickWindow::beforeRendering,this,[this]{beginGpuSpan();deliver();},Qt::DirectConnection);connect(w,&QQuickWindow::afterRendering,this,[this]{endGpuSpan();},Qt::DirectConnection);connect(w,&QQuickWindow::sceneGraphInvalidated,this,[this]{pendingFrame_.reset();delete provider_;provider_=nullptr;output_.Reset();outputSize_={};adapterReady_=false;renderSequence_=0;gpuFrequency_.Reset();gpuStart_.Reset();gpuEnd_.Reset();gpuPending_=false;},Qt::DirectConnection);}});}
    ~DesktopTexture(){timer_.stop();if(provider_&&window())window()->scheduleRenderJob(new Retire(provider_,std::move(output_)),QQuickWindow::AfterSynchronizingStage);provider_=nullptr;}
    bool isTextureProvider()const override{return true;}
    QSGTextureProvider* textureProvider()const override{if(!provider_)provider_=new DesktopTextureProvider;return provider_;}
    bool capturing()const{return enabled_;}void setCapturing(bool b){if(b==enabled_)return;enabled_=b;++revision_;{std::lock_guard g(errorMutex_);renderError_.clear();}pulse();}
    bool live()const{return live_;}QString status()const{return state_;}double frameAge()const{return age_;}QVector2D sourceSize()const{return size_;}QVector2D sourceOrigin()const{return origin_;}
    Q_INVOKABLE QVariantMap diagnostics(){auto s=capture_.stats();return {{"adapter",adapterName_},{"source",QString::fromStdString(s.state)},{"available",qulonglong(s.available)},{"published",qulonglong(s.published)},{"discarded",qulonglong(s.discarded)},{"cropBytes",qulonglong(s.bytes)},{"copyCpuMs",s.copyCpuMs},{"cropGpuMeanMs",s.copyGpuMs},{"cropGpuSamples",qulonglong(s.gpuSamples)},{"qtRenderGpuMeanMs",renderGpuMs_.load()},{"qtRenderGpuSamples",qulonglong(gpuSamples_.load())},{"transferCpuMs",transferMs_.load()},{"renderSequence",renderSequence_.load()},{"frameAgeMs",age_},{"generation",qulonglong(s.generation)}};}
#ifdef GLASS_LAB_DESKTOP_CHECK
    void injectCaptureFailure(bool b){injectCaptureFailure_=b;++revision_;pulse();}
    QVariantMap probeRenderedTexture(){
        QVariantMap result;if(!output_)return result;glassdesktop::ComPtr<ID3D11Device> device;output_->GetDevice(&device);glassdesktop::ComPtr<ID3D11DeviceContext> c;device->GetImmediateContext(&c);D3D11_TEXTURE2D_DESC td{};output_->GetDesc(&td);result["texture width"]=td.Width;result["texture height"]=td.Height;result["format"]=int(td.Format);td.Width=td.Height=1;td.BindFlags=td.MiscFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;glassdesktop::ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&td,nullptr,&staging)))return result;D3D11_BOX box{200,360,0,201,361,1};c->CopySubresourceRegion(staging.Get(),0,0,0,0,output_.Get(),0,&box);D3D11_MAPPED_SUBRESOURCE map{};if(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))){auto p=static_cast<const unsigned char*>(map.pData);result["R"]=p[0];result["G"]=p[1];result["B"]=p[2];result["A"]=p[3];c->Unmap(staging.Get(),0);}result["provider texture exists"]=bool(provider_&&provider_->value);return result;
    }
#endif
Q_SIGNALS:void changed();
};
