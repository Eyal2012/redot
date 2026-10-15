#include "quiet_gpu.h"
#include <winrt/base.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>

struct Notifications { bool finished{},success{}; uint64_t finishedAt{}; std::wstring error; };
static LRESULT CALLBACK notify(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==kRecordingEventMessage){std::unique_ptr<RecordingEvent> event(reinterpret_cast<RecordingEvent*>(l));auto state=reinterpret_cast<Notifications*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(state&&event->kind==RecordingEventKind::Finished){if(!state->finished)state->finishedAt=GetTickCount64();state->finished=true;state->success=event->success;state->error=event->text;}return 0;}
    return DefWindowProcW(window,message,w,l);
}
static bool until(const std::function<bool()>& done,unsigned milliseconds){auto end=GetTickCount64()+milliseconds;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(done())return true;Sleep(5);}while(GetTickCount64()<end);return done();}
int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>3)return 2;const bool matroska=argc==3&&std::wstring(argv[2])==L"mkv";auto root=std::filesystem::absolute(argv[1]);auto allowed=std::filesystem::current_path()/L"build";if(root.wstring().rfind(allowed.wstring()+L"\\",0)!=0||std::filesystem::exists(root))return 2;
    std::filesystem::create_directories(root/L"local");SetEnvironmentVariableW(L"LOCALAPPDATA",(root/L"local").c_str());
    winrt::init_apartment(winrt::apartment_type::multi_threaded);auto gpu=makeQuietGpu();if(!gpu)return 3;
    Notifications state;WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=notify;cls.lpszClassName=L"ReDotOwnedLifecycleNotifications";RegisterClassW(&cls);
    auto window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=320;desc.Height=180;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Usage=D3D11_USAGE_IMMUTABLE;
    std::vector<uint32_t> pixels(320*180,0xff603020);D3D11_SUBRESOURCE_DATA data{pixels.data(),320*4,0};ComPtr<ID3D11Texture2D> texture;winrt::check_hresult(gpu->device->CreateTexture2D(&desc,&data,&texture));
    VideoConfig video;video.width=320;video.height=180;video.fps=30;video.container=matroska?ContainerFormat::Mkv:ContainerFormat::Mp4;RecordingAudioOptions audio;audio.gameMode=GameAudioMode::Off;RecordingPipeline pipeline;std::wstring error;unsigned failures{};
    std::ofstream report(root/L"lifecycle.txt");auto check=[&](bool value,const char* label){report<<(value?"PASS ":"FAIL ")<<label<<"\n";if(!value)++failures;};
    check(pipeline.start(window,gpu,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video),"initial recording starts");auto ingress=pipeline.ingress();if(!ingress)return 4;ingress->push({texture,std::chrono::steady_clock::now(),{}});
    check(until([&]{return ingress->encodedFrames>=10||state.finished;},5000)&&!state.finished,"real NVENC encodes owned frames");
    SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_CLEANUP_DELAY_MS",L"1500");SetEnvironmentVariableW(L"PANDACLIP_TEST_COMPLETION_POST_DELAY_MS",L"1000");auto stop=GetTickCount64();pipeline.requestStop();until([]{return false;},250);
    check(pipeline.busy()&&!state.finished,"cleanup keeps worker busy and defers Finished");auto attempt=GetTickCount64();bool restarted=pipeline.start(window,gpu,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video);auto rejectMs=GetTickCount64()-attempt;
    check(!restarted&&rejectMs<100,"Start during cleanup rejects immediately without joining on UI caller");
    if(restarted)pipeline.requestStop();
    // Do not pump the owned notification queue: hold open the old busy=false /
    // Finished-not-consumed gap which allowed a fresh worker to be joined.
    Sleep(1550);check(!state.finished&&pipeline.busy(),"pending terminal notification keeps session busy after worker cleanup");
    attempt=GetTickCount64();restarted=pipeline.start(window,gpu,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video);
    check(!restarted&&GetTickCount64()-attempt<100,"Start in completion-notification gap rejects without blocking");if(restarted)pipeline.requestStop();
    check(until([&]{return state.finished;},7000),"terminal notification arrives within bound");auto finishMs=state.finishedAt-stop;
    check(finishMs>=2500&&pipeline.busy()&&state.success,"Finished follows cleanup and publication delay; owner still must join");auto join=GetTickCount64();pipeline.join();auto joinMs=GetTickCount64()-join;check(joinMs<100&&!pipeline.busy(),"join after notification performs no blocking drain");
    SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_CLEANUP_DELAY_MS",nullptr);SetEnvironmentVariableW(L"PANDACLIP_TEST_COMPLETION_POST_DELAY_MS",nullptr);
    for(unsigned n=0;n<3;++n){state={};check(pipeline.start(window,gpu,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video),"repeat session starts");auto input=pipeline.ingress();input->push({texture,std::chrono::steady_clock::now(),{}});check(until([&]{return input->encodedFrames>=5||state.finished;},5000)&&!state.finished,"repeat session encodes");pipeline.requestStop();check(until([&]{return state.finished;},5000)&&state.success&&pipeline.busy(),"repeat session finalizes and releases worker");pipeline.join();}
    state={};auto unavailable=std::make_shared<GpuResources>();unavailable->adapterName=L"Owned unavailable encoder fixture";
    check(pipeline.start(window,unavailable,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video),"unavailable encoder runs asynchronous error path");check(until([&]{return state.finished;},5000)&&!state.success&&pipeline.busy(),"encoder init failure reports terminal failure and releases worker");pipeline.join();
    state={};SetEnvironmentVariableW(L"PANDACLIP_TEST_COMPLETION_POST_FAIL",L"1");
    check(pipeline.start(window,gpu,root.wstring(),RecordingQuality::Balanced,audio,error,false,{},video),"owned notification-failure session starts");auto failedNotice=pipeline.ingress();failedNotice->push({texture,std::chrono::steady_clock::now(),{}});
    check(until([&]{return failedNotice->encodedFrames>=5;},5000),"notification-failure session encodes");pipeline.requestStop();
    std::unique_ptr<RecordingEvent> recovered;check(until([&]{recovered=pipeline.takeFailedCompletion();return bool(recovered);},5000)&&!state.finished&&pipeline.busy(),"failed notification retains one terminal result for bounded owner polling");
    check(recovered&&recovered->success,"retained terminal result preserves successful finalization");if(recovered)SendMessageW(window,kRecordingEventMessage,0,reinterpret_cast<LPARAM>(recovered.release()));
    pipeline.join();check(state.finished&&state.success&&!pipeline.busy()&&!pipeline.takeFailedCompletion(),"polled completion dispatch releases worker and is consumed exactly once");SetEnvironmentVariableW(L"PANDACLIP_TEST_COMPLETION_POST_FAIL",nullptr);
    report<<"finish_ms="<<finishMs<<" restart_reject_ms="<<rejectMs<<" post_finished_join_ms="<<joinMs<<" failures="<<failures<<"\n";report.close();DestroyWindow(window);std::cout<<(failures?"FAIL":"PASS")<<" lifecycle failures="<<failures<<std::endl;return failures?1:0;
}
