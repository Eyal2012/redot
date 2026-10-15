// Quiet engineering source gate. Only an owned, fully covered ROI is read/saved.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <windows.h>
#include <dwmapi.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include "../../tests/owned_window_snapshot.h"
static unsigned tick{}; static bool activated{}; static HWND backing{},overlay{};static DWORD qtPid{};
static BOOL CALLBACK findQt(HWND h,LPARAM){DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==qtPid&&IsWindowVisible(h)&&GetWindow(h,GW_OWNER)==nullptr){overlay=h;return FALSE;}return TRUE;}
static void CALLBACK guard(HWINEVENTHOOK,DWORD,HWND h,LONG,LONG,DWORD,DWORD){if(h==backing||h==overlay)activated=true;}
static LRESULT CALLBACK paint(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_TIMER){++tick;InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);
        auto b=CreateSolidBrush(h==overlay?RGB(250,0,250):RGB(24,113,140));FillRect(dc,&r,b);DeleteObject(b);
        if(h==backing){for(int i=0;i<20;++i){RECT s{i*50,0,i*50+12,r.bottom};b=CreateSolidBrush(RGB(18,56,89));FillRect(dc,&s,b);DeleteObject(b);}RECT s{100+int(tick%280),140,145+int(tick%280),290};b=CreateSolidBrush(RGB(241,180,43));FillRect(dc,&s,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(239,244,248));wchar_t text[64];swprintf_s(text,L"OWNED LIVE WINDOW / FRAME %u",tick);TextOutW(dc,110,100,text,int(wcslen(text)));}
        EndPaint(h,&p);return 0;}return DefWindowProcW(h,m,w,l);
}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}
int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>4)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const std::filesystem::path out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
    auto hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,guard,0,0,WINEVENT_OUTOFCONTEXT);
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=paint;c.lpszClassName=L"GlassLabMonitorGate";RegisterClassW(&c);
    backing=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOPMOST,c.lpszClassName,L"Owned monitor proof source",WS_POPUP|WS_DISABLED,80,60,950,670,nullptr,nullptr,c.hInstance,nullptr);
    PROCESS_INFORMATION process{};bool set{};DWORD setError{};
    ShowWindow(backing,SW_SHOWNOACTIVATE);
    if(argc>=3){SetEnvironmentVariableW(L"PANDACLIP_GLASS_LAB_DATA",(out/L"profile").c_str());const auto exe=std::filesystem::absolute(argv[2]);std::wstring cmd=L"\""+exe.wstring()+L"\" --passive"+(argc==4?L"":L" --solid");STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_SHOWNOACTIVATE;if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&si,&process))return 3;qtPid=process.dwProcessId;CloseHandle(process.hThread);const auto until=GetTickCount64()+12000;while(!overlay&&GetTickCount64()<until){EnumWindows(findQt,0);pump();}if(!overlay){TerminateProcess(process.hProcess,98);CloseHandle(process.hProcess);DestroyWindow(backing);return 4;}SetWindowPos(overlay,HWND_TOPMOST,180,140,0,0,SWP_NOSIZE|SWP_NOACTIVATE);set=true;}
    else {overlay=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOPMOST,c.lpszClassName,L"Owned excluded magenta probe",WS_POPUP|WS_DISABLED,180,140,520,300,nullptr,nullptr,c.hInstance,nullptr);SetLastError(0);set=SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE)!=FALSE;setError=GetLastError();ShowWindow(overlay,SW_SHOWNOACTIVATE);}
    DWORD affinity{};const bool get=GetWindowDisplayAffinity(overlay,&affinity)!=FALSE;
    RECT overlayRect{};GetWindowRect(overlay,&overlayRect);
    SetWindowPos(backing,overlay,80,60,std::max(950L,overlayRect.right-80+48),std::max(670L,overlayRect.bottom-60+48),SWP_SHOWWINDOW|SWP_NOACTIVATE);SetTimer(backing,1,33,nullptr);
    bool passed=false;unsigned count{},magenta{},black{},scene{},changes{};std::uint64_t previous{};
    HRESULT error=S_OK;
    try{using namespace winrt;using namespace winrt::Windows::Graphics;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        auto interop=get_activation_factory<Capture::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();Capture::GraphicsCaptureItem item{nullptr};const auto monitor=MonitorFromWindow(overlay,MONITOR_DEFAULTTONEAREST);check_hresult(interop->CreateForMonitor(monitor,guid_of<Capture::GraphicsCaptureItem>(),put_abi(item)));MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(monitor,&mi);
        ComPtr<IDXGIDevice> dxgi;device.As(&dxgi);ComPtr<::IInspectable> ins;check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),&ins));winrt::Windows::Foundation::IInspectable projected{ins.Detach(),take_ownership_from_abi};auto direct=projected.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        auto pool=Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());auto session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);session.StartCapture();
        D3D11_TEXTURE2D_DESC d{};d.Width=520;d.Height=300;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check_hresult(device->CreateTexture2D(&d,nullptr,&staging));
        const auto end=GetTickCount64()+6500;ULONGLONG next=GetTickCount64()+300;
        while(GetTickCount64()<end){pump();auto frame=pool.TryGetNextFrame();if(!frame||GetTickCount64()<next)continue;next=GetTickCount64()+100;
            auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();ComPtr<ID3D11Texture2D> src;check_hresult(access->GetInterface(IID_PPV_ARGS(&src)));const UINT x=180-mi.rcMonitor.left,y=140-mi.rcMonitor.top;D3D11_BOX box{x,y,0,x+520,y+300,1};context->CopySubresourceRegion(staging.Get(),0,0,0,0,src.Get(),0,&box);D3D11_MAPPED_SUBRESOURCE map{};check_hresult(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));std::vector<BYTE> pixels(520*300*4);std::uint64_t hash=1469598103934665603ull;
            unsigned mp{},bp{},sp{};for(unsigned row=0;row<300;++row){auto p=static_cast<const BYTE*>(map.pData)+row*map.RowPitch;memcpy(pixels.data()+row*520*4,p,520*4);for(unsigned col=0;col<520;++col){const auto v=p+col*4;mp+=v[0]>220&&v[1]<20&&v[2]>220;bp+=v[0]<3&&v[1]<3&&v[2]<3;sp+=abs(int(v[0])-140)<4&&abs(int(v[1])-113)<4&&abs(int(v[2])-24)<4;hash=(hash^v[0])*1099511628211ull;}}
            context->Unmap(staging.Get(),0);if(count&&hash!=previous)++changes;previous=hash;magenta+=mp;black+=bp;scene+=sp;
            if(count==0||count==25||count==50){for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;writeOwnedPng(out/(L"under-exclusion-"+std::to_wstring(count)+L".png"),520,300,pixels);}++count;
        }session.Close();pool.Close();passed=set&&get&&affinity==WDA_EXCLUDEFROMCAPTURE&&count>30&&changes>20&&magenta==0&&black==0&&scene>count*90000;
    }catch(const winrt::hresult_error& e){error=e.code();}
    bool orderly=true;DWORD childExit{};
    KillTimer(backing,1);if(process.hProcess){PostMessageW(overlay,WM_CLOSE,0,0);if(WaitForSingleObject(process.hProcess,6000)!=WAIT_OBJECT_0){orderly=false;TerminateProcess(process.hProcess,98);}else {GetExitCodeProcess(process.hProcess,&childExit);orderly=childExit==0;}CloseHandle(process.hProcess);}else DestroyWindow(overlay);DestroyWindow(backing);UnhookWinEvent(hook);passed=passed&&orderly;
    std::ofstream r(out/L"source-gate.txt");r<<"method=WGC CreateForMonitor, excluded "<<(argc>=3?"shipping Qt HWND":"owned magenta HWND")<<"\nmode="<<(argc==4?"Custom Desktop":argc==3?"Solid":"opaque magenta proof")<<"\nROI=520x300 completely inside owned moving background\nset="<<set<<" setLastError="<<setError<<" get="<<get<<" effectiveAffinity="<<affinity<<"\nframes="<<count<<" changedFrames="<<changes<<" magentaPixels="<<magenta<<" blackPixels="<<black<<" expectedBackgroundPixels="<<scene<<"\nHRESULT="<<std::hex<<unsigned(error)<<std::dec<<"\nowned foreground activations="<<activated<<"\norderly child shutdown="<<orderly<<" child exit="<<childExit<<"\nsourceGate="<<(passed&&!activated?"PASS":"BLOCKED")<<"\n";
    std::cout<<"sourceGate="<<(passed&&!activated?"PASS":"BLOCKED")<<" frames="<<count<<" changes="<<changes<<" affinity="<<affinity<<"\n";return passed&&!activated?0:1;
}
