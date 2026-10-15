#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static LRESULT safeSend(HWND h,UINT m,WPARAM w,LPARAM l){
    DWORD_PTR result{};if(!h)return 0;
    if(!SendMessageTimeoutW(h,m,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result)){std::wcerr<<L"FAIL targeted API timeout message="<<m<<L"\n";throw std::runtime_error("owned UI message timeout");}
    return static_cast<LRESULT>(result);
}
#define SendMessageW safeSend
static HWND appWindow{};static DWORD ownedPid{};static int failures{};
static void check(bool ok,const wchar_t *label){std::wcout<<(ok?L"PASS ":L"FAIL ")<<label<<L"\n";if(!ok)++failures;}
static std::wstring text(HWND h){wchar_t b[8192]{};SendMessageW(h,WM_GETTEXT,8192,reinterpret_cast<LPARAM>(b));return b;}
static void pump(unsigned ms){auto end=GetTickCount64()+ms;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<end);}
static HWND child(int id){struct Search{int id;HWND result{};}s{id};EnumChildWindows(appWindow,[](HWND h,LPARAM p)->BOOL{auto &s=*reinterpret_cast<Search*>(p);if(GetDlgCtrlID(h)==s.id){s.result=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.result;}
static void click(int id){SendMessageW(appWindow,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(child(id)));pump(50);}
static void choice(int id,int index){HWND h=child(id);SendMessageW(h,CB_SETCURSEL,index,0);SendMessageW(appWindow,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(h));pump(50);}
static SCROLLINFO scroll(HWND h){SCROLLINFO s{sizeof(s),SIF_ALL};GetScrollInfo(h,SB_VERT,&s);return s;}
static void wheel(HWND h,int delta){RECT r{};GetWindowRect(h,&r);SendMessageW(h,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),MAKELPARAM(r.left+12,r.top+12));}
static HWND viewport(){HWND h=GetParent(child(2020));return GetParent(h);}
static bool waitText(int id,const wchar_t *pattern,unsigned timeout=10000){auto end=GetTickCount64()+timeout;do{pump(50);if(text(child(id)).find(pattern)!=std::wstring::npos)return true;}while(GetTickCount64()<end);std::wcout<<L"Status "<<id<<L": "<<text(child(id))<<L"\n";return false;}
static bool screenshot(const std::filesystem::path &file){
    RECT r{};GetWindowRect(appWindow,&r);HDC dc=GetDC(appWindow),mem=CreateCompatibleDC(dc);HBITMAP b=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);auto old=SelectObject(mem,b);
    BOOL printed=PrintWindow(appWindow,mem,PW_RENDERFULLCONTENT);SelectObject(mem,old);DeleteDC(mem);ReleaseDC(appWindow,dc);
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr=printed?CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)):E_FAIL;
    if(SUCCEEDED(hr))hr=factory->CreateBitmapFromHBITMAP(b,nullptr,WICBitmapIgnoreAlpha,&bitmap);
    if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->WriteSource(bitmap.Get(),nullptr);
    if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();DeleteObject(b);return SUCCEEDED(hr);
}
static LRESULT CALLBACK sourceProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_TIMER){InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);auto b=CreateSolidBrush(RGB(18,26,39));FillRect(dc,&r,b);DeleteObject(b);int x=static_cast<int>((GetTickCount64()/8)%500);b=CreateSolidBrush(RGB(40,190,220));RECT box{x,100,x+64,164};FillRect(dc,&box,b);DeleteObject(b);SetTextColor(dc,RGB(230,240,250));SetBkMode(dc,TRANSPARENT);TextOutW(dc,24,24,L"Owned quiet WGC source (no sound)",33);EndPaint(h,&p);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void settingsChecks(const wchar_t *mode,std::ofstream &measurements){
    click(2002);choice(2070,7);HWND page=viewport();pump(700);
    auto file=std::filesystem::path([]{wchar_t b[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",b,32768);return std::wstring(b);}())/L"PandaClip/settings-v1.txt";
    auto stamp=std::filesystem::last_write_time(file);std::vector<double> values;
    for(int i=0;i<80;++i){SendMessageW(page,WM_VSCROLL,MAKEWPARAM(SB_THUMBPOSITION,i%2?450:0),0);auto begin=std::chrono::steady_clock::now();wheel(page,i%2?120:-120);values.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());}
    std::sort(values.begin(),values.end());std::wcout<<L"Measured "<<mode<<L" scroll median="<<values[40]<<L" p95="<<values[76]<<L" ms\n";
    std::wstring wm=mode;std::string narrow;for(auto c:wm)narrow.push_back(static_cast<char>(c));measurements<<narrow<<","<<values[40]<<","<<values[76]<<","<<values[79]<<"\n";
    SendMessageW(page,WM_VSCROLL,SB_TOP,0);wheel(child(2020),-120);check(scroll(page).nPos>0,L"targeted wheel over edit scrolls");
    SendMessageW(page,WM_VSCROLL,SB_TOP,0);auto selection=SendMessageW(child(2030),CB_GETCURSEL,0,0);wheel(child(2030),-120);check(scroll(page).nPos>0 && SendMessageW(child(2030),CB_GETCURSEL,0,0)==selection,L"targeted wheel over dropdown retains selection");
    if(child(2070)){SendMessageW(page,WM_VSCROLL,SB_TOP,0);selection=SendMessageW(child(2070),CB_GETCURSEL,0,0);wheel(child(2070),-120);check(scroll(page).nPos>0 && SendMessageW(child(2070),CB_GETCURSEL,0,0)==selection,L"sticky category wheel scrolls without changing category");}
    SendMessageW(page,WM_VSCROLL,MAKEWPARAM(SB_THUMBPOSITION,400),0);check(scroll(page).nPos==400,L"scrollbar thumb-position API");
    SendMessageW(page,WM_KEYDOWN,VK_END,0);auto s=scroll(page);check(s.nPos==s.nMax-static_cast<int>(s.nPage)+1,L"End reaches bottom");
    SendMessageW(page,WM_KEYDOWN,VK_HOME,0);check(scroll(page).nPos==0,L"Home reaches top");
    SendMessageW(page,WM_KEYDOWN,VK_NEXT,0);check(scroll(page).nPos>0,L"PageDown moves viewport");SendMessageW(page,WM_KEYDOWN,VK_PRIOR,0);check(scroll(page).nPos==0,L"PageUp restores top");
    pump(1000);check(std::filesystem::last_write_time(file)==stamp,L"scrolling/navigation do not write settings");
}
int wmain(int argc,wchar_t **argv){
    std::wcout.setf(std::ios::unitbuf);
    if(argc!=4 && argc!=5){std::wcerr<<L"Usage: quiet-ui-test app.exe isolated-root media-fixture-folder\n";return 2;}
    auto root=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(root);
    auto local=root/L"local",profile=root/L"profile",output=root/L"output";
    for(auto p:{local,profile,output,root/L"screenshots"})std::filesystem::create_directories(p);
    SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str());SetEnvironmentVariableW(L"USERPROFILE",profile.c_str());SetEnvironmentVariableW(L"TEMP",root.c_str());SetEnvironmentVariableW(L"TMP",root.c_str());SetEnvironmentVariableW(L"PANDACLIP_TEST_QUIET_UI",L"1");SetEnvironmentVariableW(L"PANDACLIP_INTERACTIVE_TESTS",nullptr);
    bool hidden=argc==5&&wcscmp(argv[4],L"--hidden")==0;
    SetEnvironmentVariableW(L"PANDACLIP_TEST_HIDE_UI",hidden?L"1":nullptr);
    // This desktop is never made the input desktop. No global mouse/key injection.
    std::wstring desktopName=L"PandaClipQuiet_"+std::to_wstring(GetCurrentProcessId());
    HDESK previous=GetThreadDesktop(GetCurrentThreadId());HDESK desk=hidden?nullptr:CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_ENUMERATE|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
    if(!hidden && (!desk || !SetThreadDesktop(desk))){std::wcerr<<L"SKIPPED isolated UI: CreateDesktop/SetThreadDesktop failed, Win32 "<<GetLastError()<<L". No fallback to the user's desktop.\n";if(desk)CloseDesktop(desk);return 3;}
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::filesystem::create_directories(local/L"PandaClip");{
        std::wofstream f(local/L"PandaClip/settings-v1.txt");f<<L"PandaClip settings 1\n\"folder\" \""<<output.generic_wstring()<<L"\"\n\"width\" \"1280\"\n\"height\" \"720\"\n\"fps\" \"30\"\n\"gameEnabled\" \"0\"\n\"micEnabled\" \"0\"\n\"gameMode\" \"0\"\n\"closeTray\" \"0\"\n\"recordFormat\" \"1\"\n";
    }
    // Make paths UTF-8 just as the production store expects (Unicode workspace).
    {auto settings=local/L"PandaClip/settings-v1.txt";std::wstring value=L"PandaClip settings 1\n\"folder\" \""+output.generic_wstring()+L"\"\n\"width\" \"1280\"\n\"height\" \"720\"\n\"fps\" \"30\"\n\"gameEnabled\" \"0\"\n\"micEnabled\" \"0\"\n\"gameMode\" \"0\"\n\"closeTray\" \"0\"\n\"recordFormat\" \"1\"\n";int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string bytes(count,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),bytes.data(),count,nullptr,nullptr);std::ofstream f(settings,std::ios::binary);f<<bytes;}
    if(std::filesystem::exists(argv[3]))for(auto &entry:std::filesystem::recursive_directory_iterator(argv[3]))if(entry.path().extension()==L".mp4" || entry.path().extension()==L".tracks"){
        auto name=entry.path().parent_path().filename().wstring()+L"_"+entry.path().filename().wstring();if(entry.path().filename().wstring().starts_with(L"PandaClip_Clip_"))name=L"PandaClip_Clip_"+name;std::filesystem::copy_file(entry.path(),output/name,std::filesystem::copy_options::overwrite_existing);
    }
    WNDCLASSW wc{};wc.lpfnWndProc=sourceProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PandaClipQuietSource";RegisterClassW(&wc);
    HWND source=CreateWindowW(wc.lpszClassName,L"PandaClip isolated silent source",WS_OVERLAPPEDWINDOW,80,60,640,400,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(source,hidden?SW_HIDE:SW_SHOWNOACTIVATE);SetTimer(source,1,33,nullptr);
    STARTUPINFOW startup{sizeof(startup)};std::wstring desktop=L"winsta0\\"+desktopName;startup.lpDesktop=hidden?nullptr:desktop.data();startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION process{};std::wstring cmd=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\"";
    if(!CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,nullptr,nullptr,&startup,&process)){std::wcerr<<L"Cannot start owned app: "<<GetLastError()<<L"\n";DestroyWindow(source);SetThreadDesktop(previous);if(desk)CloseDesktop(desk);CoUninitialize();return 1;}
    ownedPid=process.dwProcessId;
    auto end=GetTickCount64()+12000;do{pump(50);EnumWindows([](HWND h,LPARAM)->BOOL{DWORD pid{};GetWindowThreadProcessId(h,&pid);wchar_t cls[128]{};GetClassNameW(h,cls,128);if(pid==ownedPid && wcscmp(cls,L"PandaClipMainWindow")==0){appWindow=h;return FALSE;}return TRUE;},0);}while(!appWindow&&GetTickCount64()<end);
    check(appWindow!=nullptr,hidden?L"owned app exists but stays hidden (targeted APIs only)":L"owned app opened on inactive desktop");
    if(hidden)check(!IsWindowVisible(appWindow),L"test root never visible on the input desktop");
    try {
    if(appWindow){
        SetWindowPos(appWindow,nullptr,100,100,1200,960,SWP_NOZORDER|SWP_NOACTIVATE);pump(1600);
        std::ofstream measurements(root/L"measurements.csv");measurements<<"mode,scrollMedianMs,scrollP95Ms,scrollMaxMs\n";
        std::wcout<<L"DPI="<<GetDpiForWindow(appWindow)<<L". PrintWindow screenshots may omit GPU-rendered video/native backdrop on an inactive desktop.\n";
        if(argc==5&&!hidden){settingsChecks(L"baseline-idle",measurements);SendMessageW(appWindow,WM_CLOSE,0,0);}
        else {
        check(screenshot(root/L"screenshots/capture.png"),L"Capture screenshot");
        settingsChecks(L"idle",measurements);
        choice(2070,2);check(screenshot(root/L"screenshots/settings.png"),L"Settings screenshot");
        for(int category=0;category<7;++category){choice(2070,category);auto s=scroll(viewport());check(s.nPage>0,L"category viewport range/page");SendMessageW(viewport(),WM_VSCROLL,SB_BOTTOM,0);}
        choice(2070,6);SendMessageW(child(2072),BM_SETCHECK,BST_UNCHECKED,0);click(2072);check(text(child(2074)).find(L"Solid")!=std::wstring::npos,L"solid appearance fallback");check(screenshot(root/L"screenshots/appearance-solid.png"),L"Appearance screenshot");
        SendMessageW(child(2072),BM_SETCHECK,BST_CHECKED,0);click(2072);SendMessageW(child(2073),BM_SETCHECK,BST_UNCHECKED,0);click(2073);pump(1000);
        click(2000);click(2075);click(2075);click(2077);click(2077);check((GetWindowLongPtrW(child(1006),GWL_STYLE)&WS_VISIBLE)!=0,L"preview toggle restored");
        auto gain=SendMessageW(child(1019),TBM_GETPOS,0,0);SendMessageW(child(2406),TBM_SETPOS,TRUE,137);SendMessageW(appWindow,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(child(2406)));check(SendMessageW(child(1019),TBM_GETPOS,0,0)==137,L"Capture gain uses existing audio control");SendMessageW(child(2406),TBM_SETPOS,TRUE,gain);SendMessageW(appWindow,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(child(2406)));
        SendMessageW(child(2404),BM_SETCHECK,BST_CHECKED,0);click(2404);check(SendMessageW(child(1017),BM_GETCHECK,0,0)==BST_CHECKED,L"Capture mute shares production state");SendMessageW(child(2404),BM_SETCHECK,BST_UNCHECKED,0);click(2404);
        click(2001);pump(2200);check(SendMessageW(child(2053),LVM_GETITEMCOUNT,0,0)>=8,L"gallery discovers ordinary/replay source combinations");check(screenshot(root/L"screenshots/clips.png"),L"Clips screenshot");
        choice(2071,3);check(SendMessageW(child(2053),LVM_GETITEMCOUNT,0,0)==6,L"gallery audio filter excludes video-only");choice(2071,0);
        SendMessageW(child(2051),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"PandaClip_Clip_"));pump(200);check(SendMessageW(child(2053),LVM_GETITEMCOUNT,0,0)==4,L"gallery search finds replay fixture cards");
        SendMessageW(child(2051),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));choice(2052,1);check(SendMessageW(child(2053),LVM_GETITEMCOUNT,0,0)==8,L"sorting retains all matching cards");choice(2052,0);
        {
            LVITEMW item{};item.state=LVIS_SELECTED|LVIS_FOCUSED;item.stateMask=LVIS_SELECTED|LVIS_FOCUSED;
            auto remote=VirtualAllocEx(process.hProcess,nullptr,sizeof(item),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            if(remote){WriteProcessMemory(process.hProcess,remote,&item,sizeof(item),nullptr);SendMessageW(child(2053),LVM_SETITEMSTATE,0,reinterpret_cast<LPARAM>(remote));
                VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
            }
        }pump(1800);click(2054);pump(1500);check(text(child(2063)).find(L"Playback ")!=std::wstring::npos,L"selected clip plays silently with track controls");check(SendMessageW(child(2059),CB_GETCOUNT,0,0)==3,L"playback exposes Mix/Game/Microphone choices");std::wcout<<L"Playback: "<<text(child(2063))<<L"\n";check(screenshot(root/L"screenshots/playback.png"),L"playback screenshot (silent)");
        click(2003);pump(500);check(screenshot(root/L"screenshots/diagnostics.png"),L"Diagnostics screenshot");click(1030);pump(700);bool reportFound=false;for(auto &entry:std::filesystem::directory_iterator(output))if(entry.path().filename().wstring().starts_with(L"PandaClip_Diagnostics_") && std::filesystem::file_size(entry.path())>0)reportFound=true;check(reportFound,L"diagnostics report exported without opening windows");
        std::wcout<<L"SKIPPED real WGC/Windows consent in default quiet UI checks: inactive-desktop capture was unavailable; use the explicit interactive runner. No permission dialog is requested.\n";
        if(std::filesystem::path(argv[1]).stem()==L"ReDotQuietApp"){
            std::wcout<<L"Separate UI worker checks use generated owned GPU frames, not WGC or WASAPI.\n";
            click(2000);SendMessageW(appWindow,WM_APP+50,0,0);if(!waitText(1012,L"Recording active",15000))throw std::runtime_error("generated recording failed to start");check(true,L"production recording worker starts with injected GPU source");
            settingsChecks(L"recording-generated",measurements);for(int page=0;page<4;++page){click(2000+page);pump(350);}click(1011);if(!waitText(1012,L"Saved MP4",12000))throw std::runtime_error("generated recording did not finalize");check(true,L"generated recording finalizes");pump(700);
            click(2000);SendMessageW(appWindow,WM_APP+50,1,0);pump(2000);settingsChecks(L"replay-generated",measurements);
            for(int page=0;page<4;++page){click(2000+page);pump(350);}click(1024);check(waitText(1027,L"Saved clip:",12000),L"generated replay saves while navigating");click(1023);pump(1700);
            click(2000);SendMessageW(appWindow,WM_APP+50,2,0);pump(2200);settingsChecks(L"both-generated",measurements);click(1024);check(waitText(1027,L"Saved clip:",12000),L"shared recording/replay export");click(1023);click(1011);check(waitText(1012,L"Saved MP4",12000),L"shared recording/replay drains");pump(700);
            click(2001);pump(1200);check(SendMessageW(child(2053),LVM_GETITEMCOUNT,0,0)==12,L"gallery adds all four finalized worker recordings/replays");
        }
        click(2002);choice(2070,7);for(auto size:{SIZE{875,775},SIZE{1450,1000}}){SetWindowPos(appWindow,nullptr,100,100,size.cx,size.cy,SWP_NOZORDER|SWP_NOACTIVATE);pump(400);SendMessageW(viewport(),WM_VSCROLL,SB_BOTTOM,0);auto s=scroll(viewport());check(s.nPos==std::max(0,s.nMax-static_cast<int>(s.nPage)+1),L"resized settings bottom reachable");check(screenshot(root/(size.cx==875?L"screenshots/settings-small.png":L"screenshots/settings-large.png")),L"resized Settings screenshot");}
        check(text(child(2020))==L"1280" && text(child(2021))==L"720",L"numeric settings retained across navigation/resizing");
        SetWindowPos(appWindow,nullptr,100,100,875,775,SWP_NOZORDER|SWP_NOACTIVATE);pump(300);
        click(2000);check(screenshot(root/L"screenshots/capture-small.png"),L"compact Capture screenshot");
        click(2076);check(screenshot(root/L"screenshots/source-details-small.png"),L"compact source-details screenshot");click(2076);
        click(2001);check(screenshot(root/L"screenshots/clips-small.png"),L"compact Clips screenshot");
        SendMessageW(appWindow,WM_CLOSE,0,0);pump(1000);
        }
    }
    }catch(const std::exception &e){++failures;std::cerr<<"Owned UI failure: "<<e.what()<<"\n";PostMessageW(appWindow,WM_CLOSE,0,0);}
    DWORD wait=WaitForSingleObject(process.hProcess,15000);check(wait==WAIT_OBJECT_0,L"owned app drained and exited");if(wait!=WAIT_OBJECT_0){std::wcout<<L"FAIL owned test process exceeded shutdown budget (not user's app).\n";TerminateProcess(process.hProcess,2);WaitForSingleObject(process.hProcess,5000);}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);KillTimer(source,1);DestroyWindow(source);UnregisterClassW(wc.lpszClassName,wc.hInstance);CoUninitialize();SetThreadDesktop(previous);if(desk)CloseDesktop(desk);
    std::wcout<<L"Quiet checks: "<<failures<<L" failure(s). Physical pointer, keyboard, input desktop, personal sessions and audio playback were not controlled.\n";
    return failures?1:0;
}
