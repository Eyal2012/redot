// Only ReDotQuietApp contains this seam. Generated GPU/PCM sources enter
// the same scene compositor/timelines as production; they cannot feed NVENC
// directly or act as a shipping fallback.
std::jthread quietProducer_;
bool quietHotkeyRegistrationProbe_{},quietFrontendSource_{};
std::atomic_bool quietProducerBusy_{};
std::atomic_uint quietPacketsPending_{};
void startQuietSource(unsigned mode) {
    if(mode>=6&&mode<=13){
        const BorderPermission decisions[]={BorderPermission::Pending,BorderPermission::Denied,BorderPermission::Allowed,BorderPermission::Failed,BorderPermission::Cancelled,BorderPermission::Unsupported,BorderPermission::Pending,BorderPermission::Denied};
        borderAccess_.verificationResult(hwnd_,decisions[mode-6]);
        if(mode==12)borderAccess_.cancelPending(L"Verification-only timeout: ordinary outlined capture remains available.");return;
    }
    if(mode==4){quietProducer_.request_stop();return;}
    if(mode==5){quietFrontendSource_=true;return;}
    if(!diagnosticSetting(L"PANDACLIP_TEST_QUIET_UI")||!sceneRuntime_||quietProducerBusy_)return;
    auto gpu=engine_.gpuResources();if(!gpu)return;
    if(quietProducer_.joinable())quietProducer_.join();
    quietFrontendSource_=true;quietProducerBusy_=true;
    auto runtime=sceneRuntime_.get();
    quietProducer_=std::jthread([this,gpu,runtime,mode](std::stop_token stop){
        struct Buffer{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;std::atomic_bool leased{};};
        std::array<std::shared_ptr<Buffer>,12> pool;
        for(auto& slot:pool){slot=std::make_shared<Buffer>();D3D11_TEXTURE2D_DESC d{};d.Width=640;d.Height=360;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;if(FAILED(gpu->device->CreateTexture2D(&d,nullptr,&slot->texture))||FAILED(gpu->device->CreateRenderTargetView(slot->texture.Get(),nullptr,&slot->target))){quietProducerBusy_=false;return;}}
        ComPtr<ID3D11DeviceContext1> context1;gpu->context.As(&context1);
        auto anchor=std::chrono::steady_clock::now(),next=anchor;unsigned frame=0;
        while(!stop.stop_requested()){
            std::shared_ptr<Buffer> selected;for(auto& slot:pool){bool free=false;if(slot->leased.compare_exchange_strong(free,true)){selected=slot;break;}}
            if(!selected){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
            auto lease=std::shared_ptr<void>(selected.get(),[selected](void*){selected->leased=false;});
            {std::lock_guard lock(*gpu->contextMutex);float color[]={float(frame%60)/90.f,.15f,.3f,1};gpu->context->ClearRenderTargetView(selected->target.Get(),color);if(context1){int x=int(frame*7%560);D3D11_RECT marker{x,120,x+70,300};float bright[]={.97f,.75f,.23f,1};context1->ClearView(selected->target.Get(),bright,&marker,1);}gpu->context->Flush();}
            auto snapshot=runtime->snapshot();for(auto& definition:snapshot.definitions)if(definition.settings["verificationFixture"].toBool())runtime->verificationFrame(definition.id,{selected->texture,std::chrono::steady_clock::now(),lease});
            // Provider access is copied under the UI-independent ingress lock.
            // Only the recording worker creates/finalizes the provider; the
            // producer sends its PCM via the UI message seam below instead.
            auto packets=new std::vector<std::pair<QString,RecordedAudio>>;
            for(auto& definition:snapshot.definitions)if(Scenes::isAudio(definition.kind)&&definition.settings["verificationFixture"].toBool()){
                RecordedAudio packet;packet.source=definition.kind==Scenes::Kind::Microphone?AudioSourceKind::Microphone:AudioSourceKind::Game;packet.continuousClock=true;packet.clockAnchor=anchor;packet.firstFrame48k=uint64_t(frame)*1600;packet.stereo48k.resize(3200);
                for(unsigned n=0;n<1600;++n){float v=.03f*std::sin(double(packet.firstFrame48k+n)*6.283185307*(packet.source==AudioSourceKind::Microphone?450:300)/48000);packet.stereo48k[n*2]=packet.stereo48k[n*2+1]=v;}packets->emplace_back(definition.id,std::move(packet));
            }
            if(quietPacketsPending_.fetch_add(1)>=4){--quietPacketsPending_;delete packets;}
            else if(!PostMessageW(hwnd_,WM_APP+51,0,reinterpret_cast<LPARAM>(packets))){--quietPacketsPending_;delete packets;}
            ++frame;if(mode==14||mode==15)break;next=anchor+std::chrono::nanoseconds(int64_t(frame)*1'000'000'000/30);std::this_thread::sleep_until(next);
        }
        quietProducerBusy_=false;
    });
}
