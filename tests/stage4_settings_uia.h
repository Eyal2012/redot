#pragma once
// Exact deployed Qt HWND via Windows UIA. No global input, activation or shell
// control. Every action targets the positively identified owned child process.
#include <iomanip>
#include <map>
static int stage4SettingsUi(HWND workspace,const std::filesystem::path& root,HWND background=nullptr){
    std::ofstream report(root/L"settings-uia.txt");int failures{};
    auto check=[&](bool ok,const char* name){report<<(ok?"PASS ":"FAIL ")<<name<<"\n";report.flush();if(!ok)++failures;};
    DWORD pid{};GetWindowThreadProcessId(workspace,&pid);
    auto pump=[](DWORD ms){auto end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}};
    const auto co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Runtime{bool com;~Runtime(){if(com)CoUninitialize();}} runtime{SUCCEEDED(co)};
    ComPtr<IUIAutomation> automation;
    if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))){check(false,"UIA initialization");return failures;}
    ComPtr<IUIAutomation2> bounded;if(SUCCEEDED(automation.As(&bounded))){bounded->put_ConnectionTimeout(1500);bounded->put_TransactionTimeout(1500);}
    auto find=[&](IUIAutomationElement* parent,const wchar_t* name){
        VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);ComPtr<IUIAutomationCondition> condition;
        automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);
        ComPtr<IUIAutomationElement> item;if(parent)parent->FindFirst(TreeScope_Descendants,condition.Get(),&item);return item;
    };
    // Native creation can expose a visible root HWND before Qt finishes its
    // controls/accessibility provider. Wait for the real named Invoke control,
    // refreshing the root, instead of treating early provider readiness as a
    // missing Settings page. Never invoke twice or request foreground focus.
    ComPtr<IUIAutomationElement> main,settingsAction;ComPtr<IUIAutomationInvokePattern> invoke;
    const auto readyLimit=GetTickCount64()+5000;
    do {
        main.Reset();automation->ElementFromHandle(workspace,&main);
        settingsAction=find(main.Get(),L"Settings");invoke.Reset();
        if(settingsAction&&SUCCEEDED(settingsAction->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke))))break;
        pump(50);
    } while(GetTickCount64()<readyLimit);
    check(invoke&&SUCCEEDED(invoke->Invoke()),"shipping Settings navigation invoked");pump(700);
    struct Search{DWORD pid;HWND window{};} search{pid};
    EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),[](HWND h,LPARAM p)->BOOL{
        auto s=reinterpret_cast<Search*>(p);DWORD id{};GetWindowThreadProcessId(h,&id);wchar_t name[160]{};GetWindowTextW(h,name,160);
        if(id==s->pid&&IsWindowVisible(h)&&wcsstr(name,L"Settings and source properties")){s->window=h;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    if(!search.window){check(false,"owned Settings HWND exists");return failures;}
    auto window=search.window;ComPtr<IUIAutomationElement> settings;automation->ElementFromHandle(window,&settings);
    auto minimum=find(settings.Get(),L"Minimize to tray"),close=find(settings.Get(),L"Close to tray");
    check(minimum&&close,"both named tray checkboxes present in actual shipping Settings");
    if(!minimum||!close){PostMessageW(window,WM_CLOSE,0,0);return failures;}
    auto general=find(settings.Get(),L"General");ComPtr<IUIAutomationSelectionItemPattern> category;
    BOOL selected{};if(general&&SUCCEEDED(general->GetCurrentPatternAs(UIA_SelectionItemPatternId,IID_PPV_ARGS(&category))))category->get_CurrentIsSelected(&selected);
    check(selected,"Settings opens General, without requiring category hunting");
    ComPtr<IUIAutomationTogglePattern> minToggle,closeToggle;
    check(SUCCEEDED(minimum->GetCurrentPatternAs(UIA_TogglePatternId,IID_PPV_ARGS(&minToggle)))&&SUCCEEDED(close->GetCurrentPatternAs(UIA_TogglePatternId,IID_PPV_ARGS(&closeToggle))),"both expose real UIA TogglePattern");
    if(!minToggle||!closeToggle){PostMessageW(window,WM_CLOSE,0,0);return failures;}
    wchar_t scaleText[32]{};GetEnvironmentVariableW(L"QT_SCALE_FACTOR",scaleText,32);const double qtScale=scaleText[0]?wcstod(scaleText,nullptr):1;
    SetWindowPos(window,nullptr,100,100,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);pump(300);
    MINMAXINFO limits{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
    check(limits.ptMinTrackSize.x>0&&limits.ptMinTrackSize.y>0,"production Qt/native minimum geometry available");
    const double scale=limits.ptMinTrackSize.x/640.;
    report<<"Qt factor="<<qtScale<<" effective Qt/native scale="<<scale<<" native window DPI="<<GetDpiForWindow(window)<<"; not a Windows display-setting change\n";
    for(auto compact:{false,true}){
        SetWindowPos(window,nullptr,100,100,int((compact?640:900)*scale),int((compact?480:700)*scale),SWP_NOZORDER|SWP_NOACTIVATE);pump(500);
        RECT bounds{},client{};GetWindowRect(window,&bounds);GetClientRect(window,&client);
        report<<"SIZE "<<(compact?"small":"normal")<<" physical client="<<client.right<<"x"<<client.bottom<<"\n";
        check(!compact||(client.right==limits.ptMinTrackSize.x&&client.bottom==limits.ptMinTrackSize.y),"small check reaches actual production minimum size");
        for(auto item:{minimum.Get(),close.Get()}){
            RECT r{};BOOL offscreen=TRUE,enabled=FALSE;item->get_CurrentBoundingRectangle(&r);item->get_CurrentIsOffscreen(&offscreen);item->get_CurrentIsEnabled(&enabled);
            report<<"CONTROL rect="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<" offscreen="<<offscreen<<" enabled="<<enabled<<"\n";
            check(!offscreen&&enabled&&r.right>r.left&&r.bottom>r.top&&r.left>=bounds.left&&r.top>=bounds.top&&r.right<=bounds.right&&r.bottom<=bounds.bottom,"checkbox visible, enabled and contained without scrolling");
        }
        if(background){const auto file=root/(compact?L"settings-small.png":L"settings-normal.png");check(ownedCompositorSnapshot(window,pid,background,file),"external owned Settings compositor screenshot saved");}
    }
    auto saved=[&](bool min,bool closing){
        std::ifstream file(root/L"local"/L"PandaClip"/L"settings-v1.txt");std::string header;std::getline(file,header);
        std::map<std::string,std::string> values;std::string key,value;while(file>>std::quoted(key)>>std::quoted(value))values[key]=value;
        // Older profiles omit minimizeTray. That is the documented off
        // default, and an unchanged checkbox must not manufacture a save.
        const auto minimum=values.contains("minimizeTray")?values["minimizeTray"]:"0";
        const auto closed=values.contains("closeTray")?values["closeTray"]:"0";
        return minimum==(min?"1":"0")&&closed==(closing?"1":"0");
    };
    auto configure=[&](bool min,bool closing){
        for(auto pair:{std::pair{minToggle.Get(),min},std::pair{closeToggle.Get(),closing}}){ToggleState value{};const auto hr=pair.first->get_CurrentToggleState(&value);
            if(FAILED(hr))return false;if((value==ToggleState_On)!=pair.second&&FAILED(pair.first->Toggle()))return false;}
        auto end=GetTickCount64()+3000;while(GetTickCount64()<end&&!saved(min,closing))pump(25);
        ToggleState a{},b{};minToggle->get_CurrentToggleState(&a);closeToggle->get_CurrentToggleState(&b);
        report<<"PREFERENCES minimize="<<min<<" close="<<closing<<" saved="<<saved(min,closing)<<"\n";
        return (a==ToggleState_On)==min&&(b==ToggleState_On)==closing&&saved(min,closing);
    };
    for(unsigned combination=0;combination<4;++combination)check(configure(combination&1,combination&2),"UIA checkbox pair independently reaches matching saved preferences");
    check(configure(false,false),"owned preference reset through actual controls");
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);check(foreground!=pid,"owned UI actions did not acquire physical foreground");
    PostMessageW(window,WM_CLOSE,0,0);pump(200);
    report<<"NOT VERIFIED physical Explorer icon/clicks; quiet profile suppresses shell publication. No global keyboard/mouse input or recording started.\nRESULT failures="<<failures<<"\n";
    return failures;
}
