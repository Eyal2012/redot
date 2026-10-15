// Verification only. No input injection, foreground activation or personal-window control.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include "../../tests/owned_window_snapshot.h"

static DWORD childPid{};
static HWND childWindow{};
static int palette{};
static HWND backingWindow{};
static bool activated{};
static HWND activatedHwnd{};
static void CALLBACK foregroundEvent(HWINEVENTHOOK,DWORD,HWND window,LONG,LONG,DWORD,DWORD){DWORD pid{};GetWindowThreadProcessId(window,&pid);if(window&&((childPid&&pid==childPid)||(backingWindow&&window==backingWindow))){activated=true;activatedHwnd=window;}}
static BOOL CALLBACK findWindow(HWND h,LPARAM){DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==childPid&&IsWindowVisible(h)&&GetWindow(h,GW_OWNER)==nullptr){childWindow=h;return FALSE;}return TRUE;}
static void pump(DWORD ms){const auto end=GetTickCount64()+ms;do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(15);}while(GetTickCount64()<end);}
#include "owned_lens_recording.h"
static LRESULT CALLBACK background(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);
        auto brush=CreateSolidBrush(palette?RGB(177,86,43):RGB(24,113,140));FillRect(dc,&r,brush);DeleteObject(brush);
        for(int i=0;i<12;++i){RECT stripe{i*110,0,i*110+54,r.bottom};brush=CreateSolidBrush(palette?RGB(95,40,39):RGB(18,56,89));FillRect(dc,&stripe,brush);DeleteObject(brush);}
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(232,236,242));auto font=CreateFontW(68,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");auto old=SelectObject(dc,font);
        TextOutW(dc,65,200,palette?L"AMBER / OWNED BACKDROP":L"BLUE / OWNED BACKDROP",22);SelectObject(dc,old);DeleteObject(font);
        EndPaint(h,&p);return 0;
    }
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    return DefWindowProcW(h,m,w,l);
}
int wmain(int argc,wchar_t** argv){
    if(argc<4){std::wcerr<<L"Usage: launcher <owned-exe> <output-folder> private|active|lens [clear|soft-frost|solid] [popover]\nactive means passive physical-desktop screenshots; no activation.\n";return 2;}
    std::wstring options;
    for(int i=4;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"popover"||arg==L"clear"||arg==L"soft-frost"||arg==L"solid")options+=L" --"+arg;else return 2;}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const std::filesystem::path exe=std::filesystem::absolute(argv[1]),out=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(out);
    const bool lens=std::wstring(argv[3])==L"lens";
    const bool active=std::wstring(argv[3])==L"active"||lens;auto foreground=GetForegroundWindow();POINT pointer{};GetCursorPos(&pointer);
    SetEnvironmentVariableW(L"PANDACLIP_GLASS_LAB_DATA",(out/L"profile").c_str());
    SetEnvironmentVariableW(L"QT_QUICK_CONTROLS_STYLE",L"Basic");
    HDESK desktop{};std::wstring desktopName;
    if(!active){desktopName=L"PandaClipGlassLab_"+std::to_wstring(GetCurrentProcessId());desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop)return 3;}
    auto log=CreateFileW((out/L"process.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);SetHandleInformation(log,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT);
    STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;start.wShowWindow=SW_SHOWNOACTIVATE;start.hStdOutput=log;start.hStdError=log;start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    if(!active)start.lpDesktop=desktopName.data();
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GlassLabOwnedBackdrop";wc.lpfnWndProc=background;RegisterClassW(&wc);
    HWND bg{};bool ok=true;
    auto hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundEvent,0,0,WINEVENT_OUTOFCONTEXT);
    for(int pass=0;pass<1;++pass){
        std::wstring cmd=L"\""+exe.wstring()+L"\""+(active?L" --passive":L"")+(lens?L" --animate-sample --lens-demo":L"")+options;PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,0,nullptr,exe.parent_path().c_str(),&start,&process)){ok=false;break;}
        childPid=process.dwProcessId;childWindow=nullptr;CloseHandle(process.hThread);
        if(!active){
            const auto until=GetTickCount64()+45000;while(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT&&GetTickCount64()<until)pump(50);
            DWORD code=1;GetExitCodeProcess(process.hProcess,&code);if(code==STILL_ACTIVE){TerminateProcess(process.hProcess,97);code=97;}ok=ok&&code==0;
            std::cout<<"Private UI check exit="<<code<<"\n";
        }else{
            auto until=GetTickCount64()+12000;while(!childWindow&&GetTickCount64()<until){EnumWindows(findWindow,0);pump(100);if(GetForegroundWindow()==childWindow&&childWindow)ok=false;}
            if(!childWindow){ok=false;}else{
                auto ex=GetWindowLongPtrW(childWindow,GWL_EXSTYLE);ok=ok&&((ex&(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT))==(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT));
                bg=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,wc.lpszClassName,L"Glass Lab owned backing scene",WS_POPUP|WS_DISABLED,0,0,1200,850,nullptr,nullptr,wc.hInstance,nullptr);
                backingWindow=bg;
                if(lens){
                    RECT r{};SetWindowPos(childWindow,HWND_TOPMOST,100,75,1040,720,SWP_NOACTIVATE);GetWindowRect(childWindow,&r);SetWindowPos(bg,childWindow,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_SHOWWINDOW|SWP_NOACTIVATE);pump(600);
                    ok=ok&&recordOwnedLens(childWindow,childPid,out/L"moving-lens-frames");
                }
                for(int scene=0;scene<(lens?0:pass==0?2:1);++scene){
                    palette=scene;
                    RECT r{};GetWindowRect(childWindow,&r);
                    SetWindowPos(childWindow,HWND_TOPMOST,scene?190:100,scene?95:75,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
                    GetWindowRect(childWindow,&r);SetWindowPos(bg,childWindow,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_SHOWWINDOW|SWP_NOACTIVATE);InvalidateRect(bg,nullptr,FALSE);
                    for(int i=0;i<18;++i){pump(100);if(GetForegroundWindow()==childWindow||GetForegroundWindow()==bg)ok=false;}
                    const auto image=out/(scene?L"window-amber.png":L"window-blue.png");
                    const bool captured=ownedCompositorSnapshot(childWindow,childPid,bg,image);ok=ok&&captured;std::wcout<<image.filename().wstring()<<L" actual compositor screenshot="<<captured<<L"\n";
                }
                PostMessageW(childWindow,WM_CLOSE,0,0);
            }
            const auto untilExit=GetTickCount64()+6000;while(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT&&GetTickCount64()<untilExit)pump(50);
            DWORD code{};GetExitCodeProcess(process.hProcess,&code);if(code==STILL_ACTIVE){TerminateProcess(process.hProcess,98);ok=false;}
            if(bg){DestroyWindow(bg);bg=nullptr;backingWindow=nullptr;}
        }
        CloseHandle(process.hProcess);
    }
    POINT after{};GetCursorPos(&after);const bool stationary=pointer.x==after.x&&pointer.y==after.y;
    ok=ok&&!activated;
    std::ofstream report(out/L"launcher.txt");report<<"checks="<<(ok?"PASS":"FAIL")<<"\nowned foreground activation observed="<<activated<<" hwnd="<<activatedHwnd<<"\nphysical pointer unchanged="<<stationary<<" (external user movement is not injected by this launcher)\ninitial foreground still present="<<IsWindow(foreground)<<"\n";
    UnhookWinEvent(hook);CloseHandle(log);if(desktop)CloseDesktop(desktop);CoUninitialize();return ok?0:1;
}
