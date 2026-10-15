#include "scene_runtime.h"
#include "application_picker.h"
#include "quiet_gpu.h"
#include "scene_audio.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include "resource_probe.h"
using namespace Scenes;
uint64_t cpuTicks(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;}
uint64_t memory(){PROCESS_MEMORY_COUNTERS_EX m{sizeof(m)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));return m.PrivateUsage;}
int main(int argc,char** argv){QApplication app(argc,argv);if(argc<3)return 2;auto root=QString::fromLocal8Bit(argv[1]);auto mode=QString::fromLocal8Bit(argv[2]);
    const unsigned seconds=argc>3?std::clamp(unsigned(std::stoul(argv[3])),10u,36000u):30;
    const bool recording=mode!="replay"&&mode!="replay-saves"&&mode!="replay-soak"&&mode!="capture"&&mode!="idle"&&mode!="generator";
    const bool buffering=mode!="record"&&mode!="capture"&&mode!="idle"&&mode!="generator";
    const bool encoding=recording||buffering;
    const bool audioFixture=argc>7&&std::string(argv[7])=="1";
    const unsigned historySeconds=argc>8?std::clamp(unsigned(std::stoul(argv[8])),5u,600u):15;
    const unsigned exportUntilSeconds=argc>9?std::min(seconds,unsigned(std::stoul(argv[9]))):seconds;
    // Ordinary-recording container (default: the application default, MKV) and audio track layout (all = Mix + scene stems, mix = Mix only).
    const bool matroska=!(argc>10&&std::string(argv[10])=="mp4");
    const bool mixOnly=argc>11&&std::string(argv[11])=="mix";
    // REDOT_BENCH_AUDIO_DROPOUT=1: the fixture microphone lane stops delivering PCM for 25 s of every 300 s (a source that disconnects and
    // later reconnects). Recording must continue with silence for that stem; no device is opened.
    const bool audioDropout=GetEnvironmentVariableW(L"REDOT_BENCH_AUDIO_DROPOUT",nullptr,0)>0;
    // REDOT_BENCH_POWER=off|on|default: verification-only control of Windows process power throttling (EcoQoS / timer-resolution
    // throttling), to measure whether throttling of a hidden/background process explains scheduling jitter.
    // REDOT_BENCH_NO_FLUSH=1: verification-only; the fixture generator normally Flushes while holding the shared context mutex.
    const bool generatorNoFlush=GetEnvironmentVariableW(L"REDOT_BENCH_NO_FLUSH",nullptr,0)>0;
    std::wstring powerMode=L"default";{wchar_t text[16]{};if(GetEnvironmentVariableW(L"REDOT_BENCH_POWER",text,16))powerMode=text;}
    if(powerMode==L"off"||powerMode==L"on"){PROCESS_POWER_THROTTLING_STATE state{PROCESS_POWER_THROTTLING_CURRENT_VERSION,PROCESS_POWER_THROTTLING_EXECUTION_SPEED|PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,powerMode==L"on"?(PROCESS_POWER_THROTTLING_EXECUTION_SPEED|PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION):0};SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&state,sizeof(state));}
    if(mode=="discover"){
        // Verification-only: wall time of the desktop-wide application discovery the compositor thread runs every 2 s for Game/Window sources.
        std::vector<double> ms;quintptr desktop=quintptr(GetThreadDesktop(GetCurrentThreadId()));size_t windows=0;
        const bool withIcons=std::getenv("REDOT_BENCH_DISCOVER_NO_ICONS")==nullptr;
        for(int i=0;i<30;++i){auto t0=std::chrono::steady_clock::now();auto d=discoverApplications(desktop,GetCurrentProcessId(),withIcons);ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());windows=size_t(d.windows.size());std::this_thread::sleep_for(std::chrono::milliseconds(200));}
        std::sort(ms.begin(),ms.end());std::cout<<"discoverApplications(icons="<<withIcons<<") windows="<<windows<<" min="<<ms.front()<<"ms median="<<ms[ms.size()/2]<<"ms p90="<<ms[size_t(ms.size()*.9)]<<"ms max="<<ms.back()<<"ms (one frame at 60 FPS = 16.7 ms, at 120 FPS = 8.3 ms)"<<std::endl;return 0;
    }
    if(mode=="timer"){
        // Verification-only: wake latency of the same private high-resolution waitable timer the scene compositor uses, no GPU work.
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);if(!timer)return 3;
        const unsigned fps=argc>6?unsigned(std::stoul(argv[6])):60;const auto interval=std::chrono::nanoseconds(1'000'000'000/fps);
        std::vector<double> latencyUs;auto next=std::chrono::steady_clock::now()+interval;uint64_t late=0;const auto end=next+std::chrono::seconds(seconds);
        while(next<end){const auto remaining=next-std::chrono::steady_clock::now();if(remaining>std::chrono::steady_clock::duration::zero()){LARGE_INTEGER due{};due.QuadPart=-std::max<int64_t>(1,(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()+99)/100);SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0);WaitForSingleObject(timer,1000);}
            const auto woke=std::chrono::steady_clock::now();const double us=std::chrono::duration<double,std::micro>(woke-next).count();latencyUs.push_back(us);if(woke>=next+interval)++late;next+=interval;if(next<woke-std::chrono::milliseconds(100))next=woke;}
        std::sort(latencyUs.begin(),latencyUs.end());auto pct=[&](double q){return latencyUs.empty()?0.0:latencyUs[std::min(latencyUs.size()-1,size_t(q*latencyUs.size()))];};
        QDir().mkpath(root);std::ofstream out((root+"/metrics.txt").toStdWString());out<<"mode=timer\nticks="<<latencyUs.size()<<"\nwake_p50_us="<<pct(.5)<<"\nwake_p99_us="<<pct(.99)<<"\nwake_p999_us="<<pct(.999)<<"\nwake_max_us="<<(latencyUs.empty()?0:latencyUs.back())<<"\nwakes_a_full_interval_late="<<late<<"\n";
        std::cout<<"timer ticks="<<latencyUs.size()<<" p50="<<pct(.5)<<"us p99="<<pct(.99)<<"us p99.9="<<pct(.999)<<"us max="<<latencyUs.back()<<"us full-interval-late="<<late<<std::endl;return 0;
    }
    QDir().mkpath(root+"/local");SetEnvironmentVariableW(L"LOCALAPPDATA",(root+"/local").toStdWString().c_str());try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);auto gpu=makeQuietGpu();if(!gpu)return 3;
    VideoConfig video;video.width=1920;video.height=1080;video.fps=60;video.container=matroska?ContainerFormat::Mkv:ContainerFormat::Mp4;if(mixOnly){video.trackGame=false;video.trackMicrophone=false;}
    if(argc>6){video.width=unsigned(std::stoul(argv[4]));video.height=unsigned(std::stoul(argv[5]));video.fps=unsigned(std::stoul(argv[6]));}
    std::wstring validation;if(!video.validate(validation))return 2;
    Snapshot snapshot;Scene scene;scene.id="benchmark-scene";scene.name="Generated scene";snapshot.selected=scene.id;
    Definition source{"moving-source","Owned generated source",Kind::Game,{{"executable","__never_live__"},{"target","__never_live__"}},{1920,1080}};snapshot.definitions={source};Visual layer;layer.id="base-layer";layer.sourceId=source.id;layer.width=1920;layer.height=1080;scene.visuals={layer};
    if(mode=="layers"||mode=="switch"||(mode=="soak"||mode=="replay-soak")){QImage badge(300,180,QImage::Format_ARGB32);badge.fill(QColor(190,140,255,150));badge.save(root+"/badge.png");Definition d{"badge","Alpha image",Kind::Image,{{"path",root+"/badge.png"}},{300,180}},text{"text","Text",Kind::Text,{{"text","Generated scene benchmark"},{"fontSize",56}},{800,130}};snapshot.definitions.append(d);snapshot.definitions.append(text);auto overlay=layer;overlay.id="overlay";overlay.sourceId=d.id;overlay.x=1500;overlay.y=800;overlay.width=300;overlay.height=180;scene.visuals.append(overlay);overlay.id="title";overlay.sourceId=text.id;overlay.x=50;overlay.y=50;overlay.width=800;overlay.height=130;scene.visuals.append(overlay);}
    snapshot.scenes={scene};auto alternate=scene;alternate.id="alternate";alternate.visuals[0].rotation=8;snapshot.scenes.append(alternate);
    if(audioFixture){Definition game{"fixture-game","Fixture Desktop",Kind::DesktopAudio,{{"deviceId","fixture-desktop"}},{640,360}},mic{"fixture-mic","Fixture Microphone",Kind::Microphone,{{"deviceId","fixture-mic"}},{640,360}};snapshot.definitions.append(game);snapshot.definitions.append(mic);for(auto& s:snapshot.scenes)s.audio={{"game-assignment",game.id,.5,0,false,false},{"mic-assignment",mic.id,.5,0,false,false}};}
    std::mutex snapshotMutex;
    std::unique_ptr<Runtime> runtime;if(mode!="direct"&&mode!="idle"&&mode!="generator"){runtime=std::make_unique<Runtime>(gpu,nullptr,0);runtime->configure(snapshot,video);}
    struct Buffer{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;std::atomic_bool leased{};};std::array<std::shared_ptr<Buffer>,12> pool;
    for(auto& b:pool){b=std::make_shared<Buffer>();D3D11_TEXTURE2D_DESC d{};d.Width=1920;d.Height=1080;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;winrt::check_hresult(gpu->device->CreateTexture2D(&d,nullptr,&b->texture));winrt::check_hresult(gpu->device->CreateRenderTargetView(b->texture.Get(),nullptr,&b->target));}
    auto replay=std::make_shared<ReplayBuffer>();replay->setEnabled(buffering);replay->setHistorySeconds(historySeconds);auto encoder=std::make_unique<RecordingPipeline>();RecordingAudioOptions audio;audio.gameMode=GameAudioMode::Off;std::wstring error;
    if(audioFixture)audio.scene=makeVerificationAudioSession(snapshot,[&]{std::lock_guard lock(snapshotMutex);return snapshot;});
    auto controlStart=std::chrono::steady_clock::now();
    if(encoding&&!encoder->start(nullptr,gpu,root.toStdWString(),RecordingQuality::Balanced,audio,error,!recording,replay,video)){std::wcerr<<error;return 4;}
    auto ingress=encoding?encoder->ingress():std::make_shared<RecordingIngress>();if(runtime&&encoding)runtime->attach(ingress);
    double startMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-controlStart).count();
    std::ofstream samples((root+"/resources.csv").toStdWString());samples<<"seconds,private_bytes,working_set_bytes,handles,cpu_ticks,write_bytes,composed,encoded,repeated,replay_bytes,pending,exports,commit_bytes,peak_commit_bytes,threads,gpu_owned_bytes,replay_rolling_bytes,replay_retained_bytes,video_queue,audio_queue,private_region_commit,mapped_region_commit,image_region_commit,region_count,phase\n";
    auto resourceSample=[&](double elapsed,const char* phase,bool census){
        auto m=probeResources(GetCurrentProcess(),GetCurrentProcessId(),census);IO_COUNTERS io{};GetProcessIoCounters(GetCurrentProcess(),&io);auto st=runtime?runtime->status():RuntimeStatus{};
        size_t videoQueue{},audioQueue{};if(ingress){std::lock_guard lock(ingress->mutex);videoQueue=ingress->frames.size();audioQueue=ingress->audio.size();}
        samples<<elapsed<<","<<m.privateBytes<<","<<m.workingSet<<","<<m.handles<<","<<cpuTicks()<<","<<io.WriteTransferCount<<","<<st.frames<<","<<(ingress?ingress->encodedFrames.load():0)<<","<<(ingress?ingress->repeatedFrames.load():0)<<","<<replay->memoryBytes()<<","<<replay->pendingSaves()<<","<<replay->exportsCompleted<<","<<m.pagefileCommit<<","<<m.peakCommit<<","<<m.threads<<","<<st.gpuBytes<<","<<replay->rollingBytes()<<","<<replay->retainedBytes()<<","<<videoQueue<<","<<audioQueue<<","<<m.privateRegions<<","<<m.mappedRegions<<","<<m.imageRegions<<","<<m.regions<<","<<phase<<"\n";samples.flush();
    };
    uint64_t sampleSlot=0,peakPending=0,expectedRejections=0,controlledWriteFailures=0;double firstEncodeMs=-1;
    std::ofstream actions((root+"/actions.csv").toStdWString());actions<<"seconds,action,pending,encoded\n";
    auto action=[&](unsigned at,const char* name){actions<<at<<","<<name<<","<<replay->pendingSaves()<<","<<ingress->encodedFrames<<"\n";actions.flush();};
    ComPtr<ID3D11DeviceContext1> context;gpu->context.As(&context);auto start=std::chrono::steady_clock::now(),next=start;uint64_t cpuStart=0,memEarly=0,peak=0,frame=0,producerDrops=0;auto measureStart=start+std::chrono::seconds(5);double wall=0;
    // Model the independent WASAPI producers. The old fixture generated PCM
    // on the GPU/video thread, so GPU waits delayed audio and manufactured
    // late/missing samples under stress. This is test-only, not a DSP change.
    std::atomic_bool pcmFailed{};std::atomic_uint64_t fixtureAudioFrames{};
    std::jthread pcmProducer;
    if(audioFixture&&encoding)pcmProducer=std::jthread([&](std::stop_token stop){
        try{
            constexpr uint64_t count=960;uint64_t first{};
            while(!stop.stop_requested()){
                for(unsigned lane=0;lane<2;++lane){if(audioDropout&&lane==1){const auto second=first/48000;if(second%300>=100&&second%300<125)continue;}RecordedAudio packet;packet.source=lane?AudioSourceKind::Microphone:AudioSourceKind::Game;packet.continuousClock=true;packet.clockAnchor=start;packet.firstFrame48k=first;packet.stereo48k.resize(count*2);
                    for(uint64_t f=0;f<count;++f){auto absolute=first+f;bool cue=absolute%480000<4800;float value=cue?float(.5*std::sin(absolute*2*3.141592653589793*(lane?1200:1000)/48000)):0;packet.stereo48k[f*2]=packet.stereo48k[f*2+1]=value;}
                    if(!submitVerificationAudio(audio.scene,lane?"fixture-mic":"fixture-game",std::move(packet))){pcmFailed=true;return;}}
                first+=count;fixtureAudioFrames=first;std::this_thread::sleep_until(start+std::chrono::nanoseconds(first*1'000'000'000/48000));
            }
        }catch(...){pcmFailed=true;}
    });
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds)){
        auto now=std::chrono::steady_clock::now();if(!cpuStart&&now>=measureStart){cpuStart=cpuTicks();measureStart=now;memEarly=memory();}peak=std::max(peak,memory());
        if(firstEncodeMs<0&&ingress->encodedFrames)firstEncodeMs=std::chrono::duration<double,std::milli>(now-controlStart).count();
        auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::seconds>(now-start).count());
        if(elapsed>=sampleSlot){resourceSample(double(elapsed),"running",elapsed%60==0);
            if(mode=="switch"&&elapsed&&elapsed%5==0){std::lock_guard lock(snapshotMutex);snapshot.selected=snapshot.selected==scene.id?alternate.id:scene.id;runtime->configure(snapshot,video);}
            const bool exportWindow=elapsed<exportUntilSeconds||(exportUntilSeconds<seconds&&elapsed+300>=seconds);
            if((mode=="saves"||mode=="replay-saves")&&elapsed>=5&&exportWindow&&elapsed%5==0)replay->saveAsync(nullptr,root.toStdWString(),historySeconds);
            if((mode=="soak"||mode=="replay-soak")&&elapsed){
                if(elapsed%60==0){std::lock_guard lock(snapshotMutex);snapshot.selected=snapshot.selected==scene.id?alternate.id:scene.id;
                    for(auto& item:snapshot.scenes)if(item.audio.size()==2){item.audio[0].muted=(elapsed%300==0);item.audio[1].muted=(elapsed%420==0);}runtime->configure(snapshot,video);action(unsigned(elapsed),"scene-switch-and-live-mute");}
                if(elapsed%900==0){std::lock_guard lock(snapshotMutex);snapshot.definitions.removeIf([&](auto& d){return d.id==source.id;});runtime->configure(snapshot,video);action(unsigned(elapsed),"withdraw-owned-source-definition");}
                if(elapsed%900==5&&elapsed>900){std::lock_guard lock(snapshotMutex);snapshot.definitions.append(source);runtime->configure(snapshot,video);action(unsigned(elapsed),"restore-owned-source-definition");}
                if(elapsed>=5&&exportWindow&&elapsed%10==5){
                    if(elapsed%600==5){SetEnvironmentVariableW(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET",L"1");++controlledWriteFailures;action(unsigned(elapsed),"controlled-export-write-failure");}
                    const auto failuresBefore=replay->exportFailures.load();
                    replay->saveAsync(nullptr,root.toStdWString(),historySeconds);peakPending=std::max<uint64_t>(peakPending,replay->pendingSaves());
                    if(elapsed%120==5){replay->saveAsync(nullptr,root.toStdWString(),historySeconds);peakPending=std::max<uint64_t>(peakPending,replay->pendingSaves());replay->saveAsync(nullptr,root.toStdWString(),historySeconds);peakPending=std::max<uint64_t>(peakPending,replay->pendingSaves());expectedRejections+=replay->exportFailures.load()-failuresBefore;}
                    action(unsigned(elapsed),"back-to-back-save-requests");
                }
                if(elapsed%600==7){SetEnvironmentVariableW(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET",nullptr);action(unsigned(elapsed),"clear-owned-write-fault");}
            }
            sampleSlot=elapsed+1;
        }
        if(mode=="idle"){std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;}
        const bool generatorOnly=mode=="generator";
        std::shared_ptr<Buffer> selected;for(auto& b:pool){bool free=false;if(b->leased.compare_exchange_strong(free,true)){selected=b;break;}}if(selected){auto lease=std::shared_ptr<void>(selected.get(),[selected](void*){selected->leased=false;});
            {std::lock_guard lock(*gpu->contextMutex);bool flash=audioFixture&&frame%(video.fps*10)<video.fps/10;float color[]={flash?1.f:.09f,flash?1.f:.18f,flash?1.f:.25f,1};gpu->context->ClearRenderTargetView(selected->target.Get(),color);if(context&&!flash){int x=int(frame*9%1820);D3D11_RECT r{x,180,x+100,900};float yellow[]={.9f,.7f,.2f,1};context->ClearView(selected->target.Get(),yellow,&r,1);}if(!generatorNoFlush)gpu->context->Flush();}
            RecordedFrame image{selected->texture,now,lease};if(generatorOnly){}else if(runtime){if((mode!="soak"&&mode!="replay-soak")||!(elapsed%300>=10&&elapsed%300<13))runtime->verificationFrame(source.id,std::move(image));}else{ingress->noteAvailableFrame(now);ingress->push(std::move(image));}
        }else ++producerDrops;
        ++frame;next=start+std::chrono::nanoseconds(int64_t(frame)*1'000'000'000/video.fps);std::this_thread::sleep_until(next);
    }
    auto end=std::chrono::steady_clock::now();wall=std::chrono::duration<double>(end-measureStart).count();auto cpuMs=(cpuTicks()-cpuStart)/10000.;auto lateMemory=memory();auto status=runtime?runtime->status():RuntimeStatus{};pcmProducer.request_stop();if(pcmProducer.joinable())pcmProducer.join();if(runtime)runtime->attach({});auto stopStart=std::chrono::steady_clock::now();encoder->requestStop();encoder->join();double stopMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stopStart).count();if(runtime)runtime->requestStop();
    auto exportLimit=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(replay->pendingSaves()&&std::chrono::steady_clock::now()<exportLimit)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t ordinaryBytes=0;for(auto& entry:std::filesystem::directory_iterator(root.toStdWString())){auto name=entry.path().filename().wstring();auto ext=entry.path().extension().wstring();if(name.rfind(L"ReDot_2",0)==0&&(ext==L".mkv"||ext==L".mp4"))ordinaryBytes+=entry.file_size();}
    std::ofstream report((root+"/metrics.txt").toStdWString());report<<"mode="<<mode.toStdString()<<"\nprimary_baseline=1920x1080_60\nmeasurement_seconds="<<wall<<"\nprocess_cpu_ms="<<cpuMs<<"\none_core_cpu_percent="<<cpuMs/(wall*10)<<"\nprivate_early_bytes="<<memEarly<<"\nprivate_late_bytes="<<lateMemory<<"\nprivate_peak_bytes="<<peak<<"\ncomposed_delivery_periods="<<(runtime?1:0)<<"\ncomposed_frames="<<status.frames<<"\ncompositor_cpu_mean_ms="<<status.composeMeanMs<<"\ncompositor_cpu_max_ms="<<status.composeMaxMs<<"\ncompositor_gpu_mean_ms="<<status.gpuMeanMs<<"\ncompositor_gpu_max_ms="<<status.gpuMaxMs<<"\ngpu_query_samples="<<status.gpuSamples<<"\ncompositor_gpu_owned_bytes="<<status.gpuBytes<<"\ncompositor_late_slots="<<status.lateSlots<<"\ncompositor_pool_drops="<<status.poolDrops<<"\nproducer_drops="<<producerDrops<<"\nencoded_frames="<<ingress->encodedFrames<<"\nencoder_queue_drops="<<ingress->queueDrops<<"\nencoder_repeated_composed_frames="<<ingress->repeatedFrames<<"\nencoder_wall_mean_ms="<<double(ingress->totalEncodeMicros)/std::max<uint64_t>(1,ingress->encodedFrames)/1000<<"\nreplay_bytes="<<replay->memoryBytes()<<"\nGenerated moving GPU texture, optional generated PCM (audio_fixture), no WGC/audio hardware/UI. GPU query intervals include scheduling/preemption; not GPU utilization or GD frame times. Other running apps were preserved.\n";
    report<<"run_seconds="<<seconds<<"\nconfigured_width="<<video.width<<"\nconfigured_height="<<video.height<<"\nconfigured_fps="<<video.fps<<"\nrecording="<<recording<<"\nreplay_enabled="<<buffering<<"\nsource_fixture_frames="<<frame<<"\nstart_call_ms="<<startMs<<"\nfirst_encode_ms="<<firstEncodeMs<<"\nstop_finalize_ms="<<stopMs<<"\nexports_completed="<<replay->exportsCompleted<<"\nexport_failures="<<replay->exportFailures<<"\nexport_last_ms="<<replay->lastExportMilliseconds<<"\nexport_max_ms="<<replay->maxExportMilliseconds<<"\nvideo_queue_high_water="<<ingress->videoQueueHighWater<<"\nmax_delivery_us="<<ingress->maxCaptureDeliveryMicros<<"\nencoder_views_created="<<ingress->encoderViewsCreated<<"\nencoder_views_reused="<<ingress->encoderViewsReused<<"\nslow_encode_frames="<<ingress->slowEncodeFrames<<"\n";
    report<<"fixture_loop_start_steady_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count()<<"\ngenerator_no_flush="<<generatorNoFlush<<"\npower_throttling="<<std::string(powerMode.begin(),powerMode.end())<<"\naudio_dropout="<<audioDropout<<"\ncontainer="<<(matroska?"mkv":"mp4")<<"\naudio_tracks="<<(mixOnly?"mix_only":"mix_and_stems")<<"\nordinary_file_bytes="<<ordinaryBytes<<"\naudio_fixture="<<audioFixture<<"\nexport_until_seconds="<<exportUntilSeconds<<"\nhistory_seconds="<<historySeconds<<"\nhistory_available_seconds="<<replay->availableSeconds()<<"\naudio_queue_drops="<<ingress->audioQueueDrops<<"\naudio_late_frames="<<ingress->lateAudioFrames<<"\naudio_timeline_missing="<<ingress->audioTimelineMissing<<"\naudio_timeline_overwrites="<<ingress->audioTimelineOverwrites<<"\naudio_mix_clipped="<<ingress->audioMixClipped<<"\nSynthetic silent output; no sound device opened.\n";
    report<<"peak_pending_observed_at_requests="<<peakPending<<"\nexpected_capacity_rejections="<<expectedRejections<<"\ncontrolled_write_fault_windows="<<controlledWriteFailures<<"\n";
    report<<"independent_fixture_audio_producer=1\nfixture_audio_frames="<<fixtureAudioFrames<<"\nfixture_audio_producer_failed="<<pcmFailed<<"\n";
    bool pass=!pcmFailed&&(!encoding||ingress->encodedFrames>=uint64_t(seconds*video.fps*.95))&&ingress->queueDrops<10&&replay->pendingSaves()==0&&((mode!="saves"&&mode!="replay-saves"&&(mode!="soak"&&mode!="replay-soak"))||replay->exportsCompleted>0)&&peakPending<=2;
    std::ofstream distribution((root+"/timing-histograms.csv").toStdWString());distribution<<"bucket_low_us,encode_count,compose_count\n";
    for(size_t n=0;n<401;++n)distribution<<n*50<<","<<ingress->verificationEncodeHistogram[n]<<","<<status.verificationComposeHistogram[n]<<"\n";
    report.flush();resourceSample(seconds,"encoder-stopped",true);
    runtime.reset();encoder.reset();audio.scene.reset();ingress.reset();
    resourceSample(seconds+.1,"workers-released-history-retained",true);
    for(unsigned n=1;n<=5;++n){std::this_thread::sleep_for(std::chrono::seconds(1));resourceSample(seconds+n,"stopped-history-retained",n==5);}
    replay->clearHistory();context.Reset();for(auto& b:pool)b.reset();gpu.reset();
    for(unsigned n=6;n<=15;++n){std::this_thread::sleep_for(std::chrono::seconds(1));resourceSample(seconds+n,"all-owned-resources-released",n==15);}
    std::cout<<(pass?"PASS ":"FAIL ")<<mode.toStdString()<<" encoded="<<"(see metrics.txt)"<<" queue drops="<<"(see metrics.txt)"<<std::endl;return pass?0:5;
}catch(winrt::hresult_error const& e){std::cerr<<"HRESULT "<<std::hex<<e.code().value;return 1;}catch(std::exception const& e){std::cerr<<e.what();return 1;}}
