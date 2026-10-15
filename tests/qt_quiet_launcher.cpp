#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")
using Microsoft::WRL::ComPtr;
#include "owned_window_snapshot.h"
#include "stage4_settings_uia.h"
#include "resource_probe.h"
static unsigned ownedIdleSeconds(){wchar_t text[16]{};return GetEnvironmentVariableW(L"PANDACLIP_TEST_IDLE_SECONDS",text,16)?std::clamp(unsigned(wcstoul(text,nullptr,10)),30u,3600u):30u;}
static unsigned ownedHiddenSeconds(){wchar_t text[16]{};return GetEnvironmentVariableW(L"PANDACLIP_TEST_HIDDEN_HOLD_SECONDS",text,16)?std::clamp(unsigned(wcstoul(text,nullptr,10)),0u,600u):0u;}
static int measureOwnedIdle(HWND window,HANDLE process,const std::filesystem::path& root){
    std::ofstream csv(root/L"shipping-idle.csv");csv<<"seconds,private_bytes,working_set_bytes,handles,cpu_100ns,write_bytes,ping_ms,ping_success,commit_bytes,threads,window_visible,is_iconic,phase\n";
    const auto start=GetTickCount64();unsigned failures{};
    for(unsigned n=0;n<ownedIdleSeconds();++n){
        PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));DWORD handles{};GetProcessHandleCount(process,&handles);FILETIME created{},exit{},kernel{},user{};GetProcessTimes(process,&created,&exit,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;IO_COUNTERS io{};GetProcessIoCounters(process,&io);
        LARGE_INTEGER a{},b{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&a);DWORD_PTR result{};
        auto ok=SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&result);QueryPerformanceCounter(&b);if(!ok)++failures;
        csv<<(GetTickCount64()-start)/1000.<<","<<memory.PrivateUsage<<","<<memory.WorkingSetSize<<","<<handles<<","<<k.QuadPart+u.QuadPart<<","<<io.WriteTransferCount<<","<<double(b.QuadPart-a.QuadPart)*1000/frequency.QuadPart<<","<<bool(ok)<<","<<memory.PagefileUsage<<","<<probeResources(process,GetProcessId(process)).threads<<","<<bool(IsWindowVisible(window))<<","<<bool(IsIconic(window))<<","<<(IsIconic(window)?"minimized":IsWindowVisible(window)?"visible":"hidden")<<"\n";csv.flush();
        if(n==ownedIdleSeconds()/2)PostMessageW(window,WM_SYSCOMMAND,SC_MINIMIZE,0);
        if(n==ownedIdleSeconds()*3/4)PostMessageW(window,WM_SYSCOMMAND,SC_RESTORE,0);
        auto next=start+(n+1)*1000;while(GetTickCount64()<next){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}
    }
    return int(failures);
}
static int shippingControls(HWND window,const std::filesystem::path& root,HWND background=nullptr){
    std::ofstream report(root/L"shipping-controls.txt");int failures{};
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> automation;HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
    if(FAILED(hr)){report<<"BLOCKED UI Automation initialization HRESULT="<<std::hex<<hr<<"\n";CoUninitialize();return 1;}
    ComPtr<IUIAutomation2> bounded;if(SUCCEEDED(automation.As(&bounded))){bounded->put_ConnectionTimeout(1500);bounded->put_TransactionTimeout(1500);}
    ComPtr<IUIAutomationElement> main;if(FAILED(automation->ElementFromHandle(window,&main))){CoUninitialize();return 1;}
    auto invoke=[&](IUIAutomationElement* parent,const wchar_t* name){
        VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);ComPtr<IUIAutomationCondition> condition;
        automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);
        ComPtr<IUIAutomationElement> element;ComPtr<IUIAutomationInvokePattern> pattern;
        const auto readyLimit=GetTickCount64()+5000;
        do {
            parent->FindFirst(TreeScope_Descendants,condition.Get(),&element);
            if(element&&SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&pattern))))break;
            // A visible native root can precede Qt provider initialization.
            if(parent==main.Get()){main.Reset();automation->ElementFromHandle(window,&main);parent=main.Get();if(!parent)break;}
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(50);
        } while(GetTickCount64()<readyLimit);
        bool ok=pattern&&SUCCEEDED(pattern->Invoke());
        report<<(ok?"PASS ":"FAIL ")<<"UIA Invoke ";for(auto c:std::wstring(name))report<<char(c<128?c:'?');report<<"\n";if(!ok)++failures;
        return ok;
    };
    auto pump=[](DWORD ms){auto end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}};
    DWORD pid{};GetWindowThreadProcessId(window,&pid);
    if(invoke(main.Get(),L"Scenes")){
        pump(450);
        auto sceneSnapshot=[&](const wchar_t* name){
            auto file=root/name;bool saved=ownedCompositorSnapshot(window,pid,background,file);
            report<<"Scene desktop compositor screenshot="<<saved<<"\n";
            if(!saved&&std::filesystem::exists(root/L"scene-preview-cases.txt")){saved=ownedWindowSnapshot(window,pid,file);report<<"Scene owned HWND WGC screenshot="<<saved<<" (not active-desktop transparency evidence)\n";}
            if(std::filesystem::exists(root/L"scene-preview-cases.txt")&&!saved)++failures;return saved;
        };
        if(background){
            sceneSnapshot(L"scenes-deployed.png");
            RECT before{};GetWindowRect(window,&before);
            SendMessageW(window,WM_SYSCOMMAND,SC_MAXIMIZE,0);pump(300);
            report<<"Scenes maximized="<<IsZoomed(window)<<"\n";sceneSnapshot(L"scenes-maximized.png");
            SendMessageW(window,WM_SYSCOMMAND,SC_RESTORE,0);pump(300);
            SetWindowPos(window,nullptr,0,0,860,620,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump(400);
            RECT smallBounds{};GetClientRect(window,&smallBounds);report<<"Scenes small client="<<smallBounds.right<<"x"<<smallBounds.bottom<<"\n";sceneSnapshot(L"scenes-small.png");
            SetWindowPos(window,nullptr,before.left,before.top,before.right-before.left,before.bottom-before.top,SWP_NOZORDER|SWP_NOACTIVATE);pump(250);
        }
        invoke(main.Get(),L"Capture");pump(350);
    }
    if(std::filesystem::exists(root/L"scene-preview-cases.txt")){CoUninitialize();report<<"RESULT failures="<<failures<<"; scene profile inspected without claiming UIA row selection\n";return failures;}
    const wchar_t* names[]={L"Recording Quick Settings",L"Replay Quick Settings",L"Save Clip Quick Settings",L"Game audio Quick Settings",L"Microphone Quick Settings"};
    if(background)ownedCompositorSnapshot(window,pid,background,root/L"popover-before.png");
    int index=0;
    for(auto name:names){if(!invoke(main.Get(),name))continue;pump(1400);
        struct Find{DWORD pid;HWND result;};Find found{pid,nullptr};
        EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),[](HWND h,LPARAM p)->BOOL{auto f=reinterpret_cast<Find*>(p);DWORD pid{};GetWindowThreadProcessId(h,&pid);wchar_t title[64]{};GetWindowTextW(h,title,64);if(pid==f->pid&&IsWindowVisible(h)&&(wcscmp(title,L"Recording")==0||wcscmp(title,L"Replay")==0||wcscmp(title,L"Save Clip")==0||wcscmp(title,L"Game audio")==0||wcscmp(title,L"Microphone")==0)){f->result=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&found));
        ComPtr<IUIAutomationElement> popup;if(found.result)automation->ElementFromHandle(found.result,&popup);
        if(!popup)popup=main; // Shipping Quick Settings is a child overlay.
        if(background)report<<"External screenshot gear "<<index<<"="<<ownedCompositorSnapshot(window,pid,background,root/(L"gear-"+std::to_wstring(index)+L"-open.png"))<<"\n";
        if(background)EnumChildWindows(window,[](HWND hwnd,LPARAM data)->BOOL{
            auto report=reinterpret_cast<std::ofstream*>(data);wchar_t cls[128]{},title[128]{};GetClassNameW(hwnd,cls,128);GetWindowTextW(hwnd,title,128);RECT bounds{};GetWindowRect(hwnd,&bounds);
            *report<<"CHILD "<<hwnd<<" visible="<<IsWindowVisible(hwnd)<<" bounds="<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom<<" class=";for(auto c:std::wstring(cls))*report<<char(c<128?c:'?');*report<<" title=";for(auto c:std::wstring(title))*report<<char(c<128?c:'?');*report<<"\n";return TRUE;
        },reinterpret_cast<LPARAM>(&report));
        invoke(popup.Get(),L"Close Quick Settings");pump(250);
        if(background)ownedCompositorSnapshot(window,pid,background,root/(L"gear-"+std::to_wstring(index)+L"-closed.png"));
        ++index;
    }
    if(background&&invoke(main.Get(),L"Settings")){
        pump(700);
        struct FindSettings{DWORD pid;HWND window{};} settings{pid};
        EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),[](HWND h,LPARAM p)->BOOL{
            auto f=reinterpret_cast<FindSettings*>(p);DWORD owner{};GetWindowThreadProcessId(h,&owner);wchar_t title[160]{};GetWindowTextW(h,title,160);
            if(owner==f->pid&&IsWindowVisible(h)&&wcsstr(title,L"Settings and source properties")){f->window=h;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&settings));
        if(settings.window){
            const auto foreground=GetForegroundWindow();
            SendMessageW(settings.window,WM_NCACTIVATE,FALSE,0);SendMessageW(settings.window,WM_ACTIVATE,WA_INACTIVE,0);SendMessageW(settings.window,WM_ACTIVATEAPP,FALSE,GetCurrentThreadId());pump(400);
            report<<"Settings inactive external screenshot="<<ownedCompositorSnapshot(settings.window,pid,background,root/L"settings-deactivated.png")<<"\n";
            report<<"Capture with Settings open="<<ownedCompositorSnapshot(window,pid,background,root/L"capture-settings-open.png")<<"\n";
            const bool unchanged=foreground==GetForegroundWindow();report<<(unchanged?"PASS ":"FAIL ")<<"Settings notifications preserve actual foreground\n";if(!unchanged)++failures;
            PostMessageW(settings.window,WM_CLOSE,0,0);pump(250);
        }else{report<<"FAIL owned Settings HWND not found\n";++failures;}
    }
    // Never start capture from the physical desktop: discovery can select a
    // personal game/window. Recording regression uses generated owned inputs.
    if(!background){invoke(main.Get(),L"Start Recording");pump(350);
    // The native close message must follow popup dismissal, just as an outside
    // click first dismisses a Qt popup. Otherwise the parent close is consumed.
    HWND sourcePopup{};for(HWND h=GetWindow(window,GW_HWNDFIRST);h;h=GetWindow(h,GW_HWNDNEXT)){
        DWORD owner{};GetWindowThreadProcessId(h,&owner);wchar_t title[64]{};GetWindowTextW(h,title,64);
        if(owner==pid&&IsWindowVisible(h)&&wcscmp(title,L"Capture source")==0){sourcePopup=h;break;}}
    if(sourcePopup){ComPtr<IUIAutomationElement> popup;automation->ElementFromHandle(sourcePopup,&popup);if(popup)invoke(popup.Get(),L"Close Quick Settings");pump(150);}
    else {pump(1000);invoke(main.Get(),L"Stop Recording");pump(500);}
    report<<"Selected-scene recording/Stop invoked on inactive owned desktop only (empty scene remains valid black).\n";
    }else report<<"Capture actions excluded from passive physical-desktop checks.\n";
    report<<"RESULT failures="<<failures<<"; exact shipping controls, inactive owned desktop, no global input.\n";
    automation.Reset();main.Reset();CoUninitialize();return failures;
}
int wmain(int argc, wchar_t **argv)
{
    // GetWindowRect/SetWindowPos and bounded screen BitBlt must all use
    // physical coordinates. An unaware launcher virtualized moved HWND bounds,
    // cropping screenshots even when the Qt client's geometry was correct.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc != 3 && argc != 4)
    {
        std::wcerr << L"Usage: ReDotQtQuietLauncher owned-app.exe project-build-artifact-folder\n";
        return 2;
    }
    auto root = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(root / L"local");
    // Fresh shipping checks must never inherit the default personal Videos
    // folder. Prepared fixtures/preferences take precedence and are preserved.
    std::filesystem::create_directories(root/L"local"/L"PandaClip");
    std::filesystem::create_directories(root/L"output");
    const auto profile=root/L"local"/L"PandaClip"/L"settings-v1.txt";
    if(!std::filesystem::exists(profile)){
        auto folder=(root/L"output").generic_u8string();std::ofstream settings(profile);
        settings<<"PandaClip settings 1\n\"folder\" \""<<std::string(folder.begin(),folder.end())<<"\"\n\"gameEnabled\" \"0\"\n\"micEnabled\" \"0\"\n\"gameMode\" \"0\"\n\"closeTray\" \"0\"\n\"recordFormat\" \"1\"\n";
        if(!settings){std::wcerr<<L"Owned profile preparation failed\n";return 3;}
    }
    auto name = L"PandaClipQtQuiet_" + std::to_wstring(GetCurrentProcessId());
    const bool longWgc=argc==4 && wcscmp(argv[3],L"--active-hidden-wgc-long")==0;
    const bool activeWgc=longWgc || (argc==4 && wcscmp(argv[3],L"--active-hidden-wgc")==0);
    const bool shippingUi=argc==4 && wcscmp(argv[3],L"--shipping-ui")==0;
    const bool settingsUi=argc==4 && (wcscmp(argv[3],L"--stage4-settings")==0||wcscmp(argv[3],L"--stage4-settings-passive")==0);
    const bool settingsPassive=settingsUi&&wcscmp(argv[3],L"--stage4-settings-passive")==0;
    const bool appearance=argc==4&&wcscmp(argv[3],L"--passive-appearance")==0;
    const bool materials=appearance||(argc==4&&wcscmp(argv[3],L"--active-materials")==0);
    const bool active = settingsPassive || activeWgc || materials || (argc==4 && wcscmp(argv[3],L"--active-smoke")==0);
    // Active smoke is read-only apart from closing our identified child. It
    // never opens a popup, captures another application, or activates a window.
    HDESK desk = active ? GetThreadDesktop(GetCurrentThreadId()) : CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk)
    {
        std::wcerr << L"Inactive desktop unavailable; no foreground fallback. " << GetLastError() << L"\n";
        return 3;
    }
    HDESK previous = GetThreadDesktop(GetCurrentThreadId());
    if (!SetThreadDesktop(desk))
    {
        CloseDesktop(desk);
        return 3;
    }
    WNDCLASSW fixtureClass{};
    fixtureClass.lpfnWndProc = [](HWND window,UINT message,WPARAM w,LPARAM l)->LRESULT {
        if(message==WM_TIMER){InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_PAINT){
            PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);
            auto tint=GetPropW(window,L"OwnedFixtureTint");auto brush=CreateSolidBrush(tint?COLORREF(reinterpret_cast<ULONG_PTR>(tint)):RGB(35,75,105));FillRect(dc,&r,brush);DeleteObject(brush);
            const int x=int((GetTickCount64()/8)%500);RECT moving{x,60,x+80,180};brush=CreateSolidBrush(RGB(235,175,35));FillRect(dc,&moving,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
        }
        return DefWindowProcW(window,message,w,l);
    };
    fixtureClass.hInstance = GetModuleHandleW(nullptr);
    fixtureClass.lpszClassName = L"PandaClipQtOwnedFixture";
    RegisterClassW(&fixtureClass);
    HWND fixture =
        CreateWindowExW(active?WS_EX_TRANSPARENT|WS_EX_NOACTIVATE:0,fixtureClass.lpszClassName, L"Owned GPU fixture (not Geometry Dash / WGC)", active?WS_POPUP:WS_OVERLAPPEDWINDOW,
                      20, 20, 640, 360, nullptr, nullptr, fixtureClass.hInstance, nullptr);
    if((!active&&!shippingUi)||activeWgc)ShowWindow(fixture, SW_SHOWNOACTIVATE);
    const bool wgc = activeWgc || (argc==4 && wcscmp(argv[3],L"--wgc")==0);
    if(wgc)SetTimer(fixture,1,16,nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_QT_REAL_WGC",wgc?L"1":nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_QT_HIDDEN_CHECK",activeWgc?L"1":nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_QT_WGC_LONG",longWgc?L"1":nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"local").c_str());
    SetEnvironmentVariableW(L"PANDACLIP_TEST_QUIET_UI", L"1");
    SetEnvironmentVariableW(L"PANDACLIP_TEST_PASSIVE_WINDOW",active?L"1":nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_QT_TEST_ROOT", root.c_str());
    const bool dashboardOnly=argc==4&&wcscmp(argv[3],L"--dashboard")==0;
    const bool faultOnly=argc==4&&wcscmp(argv[3],L"--fault-only")==0;
    SetEnvironmentVariableW(L"PANDACLIP_QT_DASHBOARD_ONLY",dashboardOnly?L"1":nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_QT_FAULT_ONLY",faultOnly?L"1":nullptr);
    const bool idleBenchmark=argc==4&&wcscmp(argv[3],L"--idle-benchmark")==0;
    bool smoke = idleBenchmark || settingsUi || shippingUi || (active&&!activeWgc) || (argc == 4 && wcscmp(argv[3], L"--smoke") == 0);
    SetEnvironmentVariableW(L"PANDACLIP_QT_RESTORE_ONLY", argc == 4 && !smoke && !wgc && !dashboardOnly && !faultOnly ? L"1" : nullptr);
    // Both shipping smoke and actual Qt playback checks must use the deployed
    // runtime, not fall back to a developer SDK through PATH/QML variables.
    {
        SetEnvironmentVariableW(L"PATH", L"C:\\Windows\\System32;C:\\Windows");
        for (auto name : {L"QT_PLUGIN_PATH", L"QML_IMPORT_PATH", L"QML2_IMPORT_PATH", L"QTDIR"})
            SetEnvironmentVariableW(name, nullptr);
    }
    std::wstring desktop = L"winsta0\\" + name;
    STARTUPINFOW startup{sizeof(startup)};
    startup.lpDesktop = active ? nullptr : desktop.data();
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    auto exe = std::filesystem::absolute(argv[1]);
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                        nullptr, &startup, &process))
    {
        std::wcerr << L"Owned process start failed " << GetLastError() << L"\n";
        CloseDesktop(desk);
        return 1;
    }
    DWORD wait = WAIT_TIMEOUT;
    ULONGLONG started = GetTickCount64(), deadline = started + (idleBenchmark?(ownedIdleSeconds()+30)*1000ull:settingsUi?45000:shippingUi || materials ? 60000 : smoke ? 15000 : (120+3*ownedHiddenSeconds())*1000ull);
    bool sawWorkspace{};
    int shippingFailures{};
    DWORD code{};
    bool foregroundViolation{};
    while (GetTickCount64() < deadline)
    {
        if(active){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);if(pid==process.dwProcessId)foregroundViolation=true;}
        wait = MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, 150, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0)
            break;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (smoke && !sawWorkspace && GetTickCount64() - started > 1500)
        {
            struct Search
            {
                DWORD pid;
                HWND result{};
            } search{process.dwProcessId};
            EnumDesktopWindows(
                desk,
                [](HWND window, LPARAM param) -> BOOL {
                    auto &search = *reinterpret_cast<Search *>(param);
                    DWORD pid{};
                    GetWindowThreadProcessId(window, &pid);
                    wchar_t cls[128]{}, title[128]{};
                    GetClassNameW(window, cls, 128);
                    GetWindowTextW(window, title, 128);
                    if (pid == search.pid && IsWindowVisible(window) && wcsncmp(cls, L"Qt", 2) == 0 &&
                        wcscmp(title, L"ReDot") == 0)
                    {
                        search.result = window;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&search));
            if (search.result)
            {
                sawWorkspace = true;
                if(idleBenchmark)shippingFailures=measureOwnedIdle(search.result,process.hProcess,root);
                if(shippingUi)shippingFailures=shippingControls(search.result,root);
                if(settingsUi)shippingFailures=stage4SettingsUi(search.result,root,settingsPassive?fixture:nullptr);
                if(active&&!settingsUi){
                    // Qt's DoesNotAcceptFocus is not WS_EX_NOACTIVATE on this
                    // platform. Pin both native safety flags on our identified
                    // test child before any owned-only z-order inspection.
                    SetWindowLongPtrW(search.result,GWL_EXSTYLE,GetWindowLongPtrW(search.result,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);
                    BOOL composition{};DwmIsCompositionEnabled(&composition);std::ofstream probe(root/L"active-smoke.txt");
                    probe<<"Owned shipping HWND="<<search.result<<" composition="<<composition
                         <<" exStyle="<<std::hex<<GetWindowLongPtrW(search.result,GWL_EXSTYLE)
                         <<"; no popup or input; API state is not visible-transmission proof.\n";
                    probe<<"Owned shipping HWND WGC screenshot="<<ownedWindowSnapshot(search.result,process.dwProcessId,root/L"shipping-window.png")
                         <<"; current Windows transparency policy, no desktop capture/style substitution.\n";
                    probe<<"Bounded owned-window compositor screenshot="<<ownedCompositorSnapshot(search.result,process.dwProcessId,fixture,root/L"shipping-compositor.png")
                         <<"; owned background only, skipped on overlapping other windows.\n";
                    // Switch real backing windows at identical host geometry.
                    // An occluded GDI repaint may not update its redirected
                    // surface, so it is not a reliable backdrop-change fixture.
                    auto alternate=CreateWindowExW(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,fixtureClass.lpszClassName,L"PandaClip owned alternate background",WS_POPUP,20,20,640,360,nullptr,nullptr,fixtureClass.hInstance,nullptr);
                    SetPropW(alternate,L"OwnedFixtureTint",reinterpret_cast<HANDLE>(ULONG_PTR(RGB(135,45,65))));
                    probe<<"Changed owned-background compositor screenshot="<<ownedCompositorSnapshot(search.result,process.dwProcessId,alternate,root/L"shipping-compositor-changed.png")<<"\n";
                    DestroyWindow(alternate);}
                if(appearance){
                    // Inspect capture before opening Media, whose independently
                    // composed contents can remain in the DWM cache on close.
                    std::ofstream report(root/L"focus-notifications.txt");
                    report<<"Capture blue background="<<ownedCompositorSnapshot(search.result,process.dwProcessId,fixture,root/L"capture-blue.png")<<"\n";
                    const auto foreground=GetForegroundWindow();
                    auto result=SendMessageW(search.result,WM_NCACTIVATE,FALSE,0);
                    SendMessageW(search.result,WM_ACTIVATE,WA_INACTIVE,0);
                    SendMessageW(search.result,WM_ACTIVATEAPP,FALSE,GetCurrentThreadId());
                    Sleep(500);
                    const bool allowed=result==TRUE&&GetForegroundWindow()==foreground;
                    report<<(allowed?"PASS ":"FAIL ")<<"deactivation paint accepts input-focus loss without changing foreground\n";
                    if(!allowed)++shippingFailures;
                    report<<"Deactivated Capture screenshot="<<ownedCompositorSnapshot(search.result,process.dwProcessId,fixture,root/L"capture-deactivated.png")<<"\n";
                    shippingFailures+=shippingControls(search.result,root,fixture);
                }
                if(materials&&!std::filesystem::exists(root/L"scene-preview-cases.txt")){
                    // Invoke only the shipping navigation action. No popup,
                    // focus request, global mouse/key input or personal HWND.
                    CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IUIAutomation> a;ComPtr<IUIAutomationElement> main;
                    if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&a)))){
                        ComPtr<IUIAutomation2> bounded;if(SUCCEEDED(a.As(&bounded))){bounded->put_ConnectionTimeout(1500);bounded->put_TransactionTimeout(1500);}
                        a->ElementFromHandle(search.result,&main);VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(L"Media");ComPtr<IUIAutomationCondition> condition;
                        a->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);ComPtr<IUIAutomationElement> element;ComPtr<IUIAutomationInvokePattern> invoke;
                        if(main)main->FindFirst(TreeScope_Descendants,condition.Get(),&element);
                        if(element&&SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke))))invoke->Invoke();else ++shippingFailures;
                    }else ++shippingFailures;
                    main.Reset();a.Reset();CoUninitialize();
                    auto limit=GetTickCount64()+2500;HWND media{};
                    while(GetTickCount64()<limit&&!media){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                        struct SearchMedia{DWORD pid;HWND hwnd;};SearchMedia find{process.dwProcessId,nullptr};
                        EnumDesktopWindows(desk,[](HWND h,LPARAM p)->BOOL{auto f=reinterpret_cast<SearchMedia*>(p);DWORD pid{};GetWindowThreadProcessId(h,&pid);wchar_t title[128]{};GetWindowTextW(h,title,128);if(pid==f->pid&&IsWindowVisible(h)&&wcsstr(title,L"View Media")){f->hwnd=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&find));media=find.hwnd;if(!media)Sleep(20);
                    }
                    std::ofstream report(root/L"materials.txt");
                    if(media){
                        Sleep(2000); // allow asynchronous owned-library metadata/thumbnails to arrive
                        SetWindowLongPtrW(media,GWL_EXSTYLE,GetWindowLongPtrW(media,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);
                        report<<"Exact shipping Media navigation invoked; owned HWND="<<media<<"\n";
                        report<<"Media WGC snapshot="<<ownedWindowSnapshot(media,process.dwProcessId,root/L"media-window.png")<<"\n";
                        // Both backdrop scenes are owned. This second HWND has a
                        // distinct red fill; no scene enters shipping rendering.
                        auto other=CreateWindowExW(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,fixtureClass.lpszClassName,L"PandaClip owned red background",WS_POPUP,20,20,640,360,nullptr,nullptr,fixtureClass.hInstance,nullptr);
                        SetPropW(other,L"OwnedFixtureTint",reinterpret_cast<HANDLE>(ULONG_PTR(RGB(135,45,65))));
                        auto resizeOwned=[&](HWND hwnd,int x,int y,int width,int height){
                            MINMAXINFO limits{};SendMessageW(hwnd,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
                            width=std::max(width,int(limits.ptMinTrackSize.x));height=std::max(height,int(limits.ptMinTrackSize.y));
                            SetLastError(0);auto positioned=SetWindowPos(hwnd,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);auto positionError=GetLastError();Sleep(1000);
                            report<<"SetWindowPos result="<<positioned<<" error="<<positionError<<"\n";
                            CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> item;ComPtr<IUIAutomationTransformPattern> transform;
                            HRESULT resized=E_NOINTERFACE;
                            if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))){
                                ComPtr<IUIAutomation2> bounded;if(SUCCEEDED(automation.As(&bounded))){bounded->put_ConnectionTimeout(1500);bounded->put_TransactionTimeout(1500);}
                                if(SUCCEEDED(automation->ElementFromHandle(hwnd,&item))&&item&&SUCCEEDED(item->GetCurrentPatternAs(UIA_TransformPatternId,IID_PPV_ARGS(&transform))))resized=transform->Resize(width,height);
                            }
                            transform.Reset();item.Reset();automation.Reset();CoUninitialize();Sleep(500);
                            report<<"Owned UIA resize HRESULT="<<std::hex<<resized<<std::dec<<"\n";
                            RECT client{},bounds{};GetClientRect(hwnd,&client);GetWindowRect(hwnd,&bounds);
                            report<<"Owned resize requested="<<width<<"x"<<height<<" native client="<<client.right<<"x"<<client.bottom<<" bounds="<<bounds.right-bounds.left<<"x"<<bounds.bottom-bounds.top<<"\n";
                        };
                        SetPropW(fixture,L"OwnedFixtureTint",reinterpret_cast<HANDLE>(ULONG_PTR(RGB(35,75,105))));
                        report<<"Media blue background="<<ownedCompositorSnapshot(media,process.dwProcessId,fixture,root/L"media-blue.png")<<"\n";
                        resizeOwned(media,100,100,720,540);
                        report<<"Media red background after move/resize="<<ownedCompositorSnapshot(media,process.dwProcessId,other,root/L"media-red-resized.png")<<"\n";
                        PostMessageW(media,WM_CLOSE,0,0);Sleep(100);
                        if(!appearance)report<<"Capture blue background="<<ownedCompositorSnapshot(search.result,process.dwProcessId,fixture,root/L"capture-blue.png")<<"\n";
                        resizeOwned(search.result,140,100,860,620);
                        report<<"Capture red background after move/resize="<<ownedCompositorSnapshot(search.result,process.dwProcessId,other,root/L"capture-red-resized.png")<<"\n";
                        if(appearance){
                            // Target only this owned HWND. WS_EX_NOACTIVATE is
                            // installed before first show; never SetForegroundWindow.
                            SendMessageW(search.result,WM_SYSCOMMAND,SC_MAXIMIZE,0);Sleep(700);
                            RECT client{};GetClientRect(search.result,&client);
                            report<<"Capture native maximized="<<IsZoomed(search.result)<<" client="<<client.right<<"x"<<client.bottom<<"\n";
                            report<<"Capture maximized external screenshot="<<ownedCompositorSnapshot(search.result,process.dwProcessId,fixture,root/L"capture-maximized.png")<<"\n";
                            SendMessageW(search.result,WM_SYSCOMMAND,SC_RESTORE,0);Sleep(500);
                            report<<"Capture restored="<<!IsZoomed(search.result)<<" screenshot="<<ownedCompositorSnapshot(search.result,process.dwProcessId,other,root/L"capture-restored.png")<<"\n";
                        }
                        DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);foregroundViolation|=foregroundPid==process.dwProcessId;report<<"Test process owns foreground="<<foregroundViolation<<" (must be 0).\n";
                        if(foregroundViolation)++shippingFailures;
                        DestroyWindow(other);
                    }else{report<<"FAIL Media owned HWND did not appear within budget\n";++shippingFailures;}
                }
                PostMessageW(search.result, WM_CLOSE, 0, 0);
            }
        }
        if(activeWgc&&!sawWorkspace&&GetTickCount64()-started>4500){
            struct Owned{DWORD pid;HWND hwnd;};Owned owned{process.dwProcessId,nullptr};
            EnumDesktopWindows(desk,[](HWND h,LPARAM p)->BOOL{auto f=reinterpret_cast<Owned*>(p);DWORD pid{};GetWindowThreadProcessId(h,&pid);wchar_t title[64]{};GetWindowTextW(h,title,64);if(pid==f->pid&&IsWindowVisible(h)&&wcscmp(title,L"ReDot")==0){f->hwnd=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&owned));
            if(owned.hwnd){sawWorkspace=true;ownedWindowSnapshot(owned.hwnd,process.dwProcessId,root/L"real-wgc-frontend.png");}
        }
    }
    if (wait != WAIT_OBJECT_0)
    {
        std::wcerr << L"Owned verification exceeded bounded wait. Terminating only child PID " << process.dwProcessId
                   << L"\n";
        TerminateProcess(process.hProcess, 2);
        WaitForSingleObject(process.hProcess, 5000);
        code = 2;
    }
    else
        GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DestroyWindow(fixture);
    UnregisterClassW(fixtureClass.lpszClassName, fixtureClass.hInstance);
    SetThreadDesktop(previous);
    if(!active)CloseDesktop(desk);
    std::ifstream result(root / (wgc?L"wgc-result.txt":argc == 4 && !dashboardOnly && !faultOnly ? L"restore-result.txt" : L"result.txt"));
    int failures = -1;
    result >> failures;
    if (smoke)
    {
        failures = sawWorkspace && code == 0 ? shippingFailures : 1;
        std::ofstream report(root / L"shipping-smoke.txt");
        report << "Shipping app, private desktop, SDK-free PATH; workspace visible=" << sawWorkspace
               << ", exit=" << code << ", failures=" << failures << "\n";
    }
    if(active){
        std::ofstream report(root/L"passive-input.txt");report<<"Observed owned foreground activation="<<foregroundViolation<<"; no global input or cursor changes.\n";
        if(foregroundViolation&&failures==0)failures=1;
    }
    std::wcout << L"Owned Qt checks exit=" << code << L" failures=" << failures << L" artifacts=" << root.wstring()
               << L"\n";
    return code || failures != 0 ? 1 : 0;
}
