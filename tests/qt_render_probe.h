#pragma once
// Verification only: bounded, nonblocking GPU queries. No pixel readback.
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <atomic>
#include <array>
#include <chrono>
#include <mutex>
#include <d3d11.h>
#include <wrl/client.h>
class QtRenderProbe : public QObject {
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, start, end;
        bool pending{}; int stage{};
    };
    std::array<Slot,8> slots_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    QQuickWindow *window_{};
    std::mutex mutex_;
    std::array<std::vector<double>,3> cpu_, gpu_;
    std::atomic_int stage_{};
    int current_{-1}, frameStage_{};
    unsigned skipped_{}, disjoint_{};
    std::chrono::steady_clock::time_point started_;
    void before() {
        started_=std::chrono::steady_clock::now(); frameStage_=stage_.load(); current_=-1;
        if(!context_) {
            auto ri=window_->rendererInterface();
            auto device=static_cast<ID3D11Device*>(ri->getResource(window_,QSGRendererInterface::DeviceResource));
            if(!device)return;
            device->GetImmediateContext(&context_);
            for(auto &s:slots_){
                D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
                if(FAILED(device->CreateQuery(&q,&s.disjoint)))return;
                q.Query=D3D11_QUERY_TIMESTAMP;
                if(FAILED(device->CreateQuery(&q,&s.start))||FAILED(device->CreateQuery(&q,&s.end)))return;
            }
        }
        for(auto &s:slots_) if(s.pending){
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{}; UINT64 a{},b{};
            if(context_->GetData(s.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
               context_->GetData(s.start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
               context_->GetData(s.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
            std::lock_guard lock(mutex_);
            if(!d.Disjoint&&d.Frequency&&b>=a&&gpu_[s.stage].size()<4096)gpu_[s.stage].push_back((b-a)*1000.0/d.Frequency);
            else ++disjoint_;
            s.pending=false;
        }
        if(!frameStage_)return;
        for(int i=0;i<8;++i)if(!slots_[i].pending&&slots_[i].disjoint){current_=i;break;}
        if(current_<0){std::lock_guard lock(mutex_);++skipped_;return;}
        auto &s=slots_[current_];s.stage=frameStage_;
        context_->Begin(s.disjoint.Get());context_->End(s.start.Get());
    }
    void after() {
        // Qt 6.8 D3D11 beginExternal executes its pending RHI command buffer.
        // End timestamps after those draws, before swap-chain Present. This
        // includes queued offscreen glass passes without a vsync/latency wait.
        window_->beginExternalCommands();
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started_).count();
        if(frameStage_){std::lock_guard lock(mutex_);if(cpu_[frameStage_].size()<4096)cpu_[frameStage_].push_back(elapsed);}
        if(current_>=0){auto &s=slots_[current_];context_->End(s.end.Get());context_->End(s.disjoint.Get());s.pending=true;}
        window_->endExternalCommands();
    }
public:
    QtRenderProbe(QQuickWindow *window):QObject(window),window_(window){
        QObject::connect(window,&QQuickWindow::beforeRendering,this,[this]{before();},Qt::DirectConnection);
        QObject::connect(window,&QQuickWindow::afterRendering,this,[this]{after();},Qt::DirectConnection);
        QObject::connect(window,&QQuickWindow::sceneGraphInvalidated,this,[this]{context_.Reset();slots_={};},Qt::DirectConnection);
    }
    void stage(int stage){stage_.store(stage);}
    QString report(int stage,const QString &name){
        std::lock_guard lock(mutex_);
        auto describe=[](std::vector<double> values){
            if(values.empty())return QString("unavailable");
            double sum{};for(double v:values)sum+=v;
            std::sort(values.begin(),values.end());
            return QString("n=%1 median=%2 p95=%3 max=%4 sum=%5 ms").arg(values.size())
                .arg(values[values.size()/2],0,'f',4).arg(values[values.size()*95/100],0,'f',4).arg(values.back(),0,'f',4).arg(sum,0,'f',4);
        };
        return QString("RENDER %1 CPU[%2] GPU[%3] skipped=%4 disjoint=%5\n").arg(name,describe(cpu_[stage]),describe(gpu_[stage])).arg(skipped_).arg(disjoint_);
    }
};
