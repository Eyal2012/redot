// Verification only. Output is a Qt root-item GPU grab, NOT an OS screenshot
// or swapchain readback (affinity remains enabled). WGC input is a real HWND.
#include <QtTest/QTest>
#include <QJsonArray>
#include <QQuickItemGrabResult>
#include <psapi.h>
static HWND sceneWindow{};static unsigned sceneTick{},scenePhase{};static bool freezeScene{},ownedActivation{};
static HWND testedWindow{};
static void CALLBACK desktopForeground(HWINEVENTHOOK,DWORD,HWND h,LONG,LONG,DWORD,DWORD){if(h==sceneWindow||h==testedWindow)ownedActivation=true;}
static LRESULT CALLBACK desktopScene(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_TIMER){++sceneTick;if(!freezeScene)++scenePhase;InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_PAINT){PAINTSTRUCT p{};auto destination=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);auto dc=CreateCompatibleDC(destination);auto bitmap=CreateCompatibleBitmap(destination,r.right,r.bottom);auto previous=SelectObject(dc,bitmap);auto b=CreateSolidBrush(RGB(24,113,140));FillRect(dc,&r,b);DeleteObject(b);
        for(int i=0;i<80;++i){RECT stripe{i*40,0,i*40+12,r.bottom};b=CreateSolidBrush(RGB(18,56,89));FillRect(dc,&stripe,b);DeleteObject(b);}
        for(int i=0;i<30;++i){RECT stripe{0,i*40,r.right,i*40+3};b=CreateSolidBrush(RGB(193,212,208));FillRect(dc,&stripe,b);DeleteObject(b);}
        RECT square{400+int(scenePhase%240),220,445+int(scenePhase%240),390};b=CreateSolidBrush(RGB(241,180,43));FillRect(dc,&square,b);DeleteObject(b);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(245,244,239));auto f=CreateFontW(24,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");auto old=SelectObject(dc,f);wchar_t text[80];swprintf_s(text,L"REAL SEPARATE DESKTOP WINDOW / FRAME %u",sceneTick);TextOutW(dc,140,135,text,int(wcslen(text)));SelectObject(dc,old);DeleteObject(f);
        // WGC can observe incremental GDI drawing. Publish the completed fixture
        // in one blit, so optics comparisons do not measure partially painted grids.
        BitBlt(destination,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);EndPaint(h,&p);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void runDesktopChecks(QQuickView& view,LabController&,QQuickItem* root,const QString& folder){
    QTimer::singleShot(600,&view,[&view,root,folder]{
        auto source=root->findChild<DesktopTexture*>("desktopSource");QJsonArray results;bool all=true;QDir().mkpath(folder+"/frames");
        auto check=[&](QString name,bool ok){results.append(QJsonObject{{"check",name},{"passed",ok}});all&=ok;};
        testedWindow=reinterpret_cast<HWND>(view.winId());auto hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,desktopForeground,0,0,WINEVENT_OUTOFCONTEXT);
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(testedWindow,MONITOR_DEFAULTTONEAREST),&mi);view.resize(1040,view.devicePixelRatio()>1.1?650:720);SetWindowPos(testedWindow,HWND_TOPMOST,mi.rcMonitor.left+90,mi.rcMonitor.top+35,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        RECT rect{};GetWindowRect(testedWindow,&rect);WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=desktopScene;wc.lpszClassName=L"GlassLabLiveDesktopFixture";RegisterClassW(&wc);
        sceneWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned live desktop glass fixture",WS_POPUP|WS_DISABLED,rect.left-48,rect.top-48,rect.right-rect.left+96,rect.bottom-rect.top+96,nullptr,nullptr,wc.hInstance,nullptr);SetWindowPos(sceneWindow,testedWindow,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW|SWP_NOACTIVATE);SetTimer(sceneWindow,1,33,nullptr);
        root->setProperty("solidPreview",false);root->setProperty("backgroundMode",0);root->setProperty("sampleMoving",false);root->setProperty("desktopFill",0.0);root->setProperty("desktopBlur",0.0);root->setProperty("desktopRefraction",0.0);
        auto waitLive=[&]{const auto end=GetTickCount64()+6000;while(source&&!source->live()&&GetTickCount64()<end)QTest::qWait(20);};waitLive();
        check("real WGC monitor source imported into Qt is live",source&&source->live());DWORD affinity{};check("actual lab exclusion remains 0x11",GetWindowDisplayAffinity(testedWindow,&affinity)&&affinity==WDA_EXCLUDEFROMCAPTURE);
        auto renderedOutput=[&]{QTest::qWait(180);auto grab=root->grabToImage(QSize(view.width(),view.height()));if(!grab)return QImage{};const auto end=GetTickCount64()+6000;while(grab->image().isNull()&&GetTickCount64()<end)QTest::qWait(10);return grab->image();};
        auto snap=[&](const QString& name){auto image=renderedOutput();image.save(folder+"/"+name+".png");return image;};
        auto baseline=snap("desktop-zero-blur-zero-refraction");check("rendered-output readback succeeds",!baseline.isNull());
        check("owned background physically visible",IsWindowVisible(sceneWindow));
        // Exact screen alignment sampled in blank foreground area, at zero effects.
        const double dpr=view.devicePixelRatio();unsigned matches{},samples{};
        const int sampleY=dpr>1.1?310:355,sampleX=dpr>1.1?470:120;
        for(int y=int(sampleY*dpr);y<int((sampleY+20)*dpr);y+=7)for(int x=int(sampleX*dpr);x<int((sampleX+230)*dpr)&&x<700*dpr;x+=7){const int bx=x+48,by=y+48;const QColor expected=by%40<3?QColor(193,212,208):bx%40<12?QColor(18,56,89):QColor(24,113,140);auto c=baseline.pixelColor(x,y);if(std::abs(c.red()-expected.red())<5&&std::abs(c.green()-expected.green())<5&&std::abs(c.blue()-expected.blue())<5)++matches;++samples;}
        results.append(QJsonObject{{"alignment matched",int(matches)},{"alignment samples",int(samples)}});check("physical-pixel alignment against separate window grid",matches>samples*.75);
        freezeScene=true;QTest::qWait(80);baseline=snap("desktop-clear");root->setProperty("desktopBlur",20.0);QTest::qWait(80);auto blurred=snap("desktop-strong-blur");root->setProperty("desktopBlur",0.0);root->setProperty("desktopRefraction",16.0);QTest::qWait(80);auto bent=snap("desktop-edge-refraction");
        unsigned blurChanges{},edgeChanges{},centerChanges{},textChanges{},glyphSamples{};
        // Use the heading's left words over blue grid, not the yellow fixture:
        // yellow R=241 can round partially covered LCD glyph pixels to R=245.
        for(int y=0;y<baseline.height();++y)for(int x=0;x<baseline.width();++x){auto a=baseline.pixelColor(x,y),b=blurred.pixelColor(x,y),c=bent.pixelColor(x,y);const int bdiff=abs(a.red()-b.red())+abs(a.green()-b.green())+abs(a.blue()-b.blue()),cdiff=abs(a.red()-c.red())+abs(a.green()-c.green())+abs(a.blue()-c.blue());if(x>400*dpr&&x<670*dpr&&y>310*dpr&&y<355*dpr&&bdiff>10)++blurChanges;if((x<25||y<25)&&cdiff>10)++edgeChanges;if(x>400*dpr&&x<650*dpr&&y>310*dpr&&y<350*dpr&&cdiff>10)++centerChanges;if(x>28*dpr&&x<160*dpr&&y>160*dpr&&y<195*dpr&&a.red()==245&&a.green()==242&&a.blue()==250){++glyphSamples;if(bdiff>2)++textChanges;}}
        check("custom blur visibly changes desktop-only region",blurChanges>500);check("desktop edges visibly bend with refraction",edgeChanges>200);check("refraction leaves center unchanged",centerChanges<20);check("opaque foreground glyph pixels unchanged through blur",glyphSamples>30&&textChanges==0);
        results.append(QJsonObject{{"blur changed pixels",int(blurChanges)},{"edge changed pixels",int(edgeChanges)},{"center changed pixels",int(centerChanges)},{"opaque glyph samples",int(glyphSamples)},{"text changed pixels",int(textChanges)}});freezeScene=false;
        // Short output demonstration, owned background only. No desktop recorder.
        root->setProperty("desktopFill",.18);root->setProperty("desktopBlur",4.0);root->setProperty("desktopRefraction",8.0);
        QJsonArray frameTimes;const auto demoOrigin=GetTickCount64();
        for(int i=0;i<(qEnvironmentVariableIsSet("PANDACLIP_DESKTOP_FAST_CHECK")?3:36);++i){root->setProperty("desktopBlur",i<12?0.0:i<24?20.0:0.0);root->setProperty("desktopRefraction",i<24?0.0:16.0);QTest::qWait(100);renderedOutput().save(folder+QString("/frames/%1.png").arg(i,4,10,QChar('0')));frameTimes.append(double(GetTickCount64()-demoOrigin));}
        QFile timesFile(folder+"/frames/times.json");if(timesFile.open(QIODevice::WriteOnly))timesFile.write(QJsonDocument(frameTimes).toJson());
        root->setProperty("desktopBlur",4.0);root->setProperty("desktopRefraction",8.0);
        check("live background continues moving",source&&source->diagnostics()["published"].toULongLong()>30);
        auto gear=root->findChild<QQuickItem*>("gear");auto point=gear->mapToScene({gear->width()/2,gear->height()/2}).toPoint();QTest::mouseClick(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,point);QTest::qWait(100);auto popup=root->findChild<QObject*>("demoPopup");check("separate gear opens demonstration popover",popup&&popup->property("visible").toBool());snap("desktop-popover");QTest::keyClick(static_cast<QWindow*>(&view),Qt::Key_Escape);QTest::qWait(40);check("popover Escape dismisses",!popup->property("visible").toBool());
        // Move/resize only this HWND; expand only this owned background.
        SetWindowPos(testedWindow,HWND_TOPMOST,mi.rcMonitor.left+130,mi.rcMonitor.top+45,0,0,SWP_NOSIZE|SWP_NOACTIVATE);view.resize(900,650);GetWindowRect(testedWindow,&rect);SetWindowPos(sceneWindow,testedWindow,rect.left-48,rect.top-48,rect.right-rect.left+96,rect.bottom-rect.top+96,SWP_NOACTIVATE);QTest::qWait(600);waitLive();check("custom source recovers after move/resize",source->live());snap("desktop-moved-resized");
        root->setProperty("backgroundMode",2);QTest::qWait(200);check("Solid mode stops capture",!source->capturing()&&source->diagnostics()["cropBytes"].toULongLong()==0);snap("solid");root->setProperty("backgroundMode",0);waitLive();check("Custom mode restarts cleanly",source->live());source->injectCaptureFailure(true);QTest::qWait(500);check("unavailable capture clearly falls back",!source->live()&&source->status().contains("HRESULT"));snap("capture-unavailable");source->injectCaptureFailure(false);waitLive();check("explicit retry after capture error works",source->live());ShowWindow(testedWindow,SW_SHOWMINNOACTIVE);QTest::qWait(300);check("minimized source releases GPU frames",source->diagnostics()["cropBytes"].toULongLong()==0);ShowWindow(testedWindow,SW_SHOWNOACTIVATE);waitLive();check("restored source resumes",source->live());
        PROCESS_MEMORY_COUNTERS memory{};GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));auto startDiag=source->diagnostics();const auto startBytes=memory.WorkingSetSize;FILETIME created{},exited{},kernel0{},user0{},kernel1{},user1{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel0,&user0);const auto start=GetTickCount64();
        // Quiet 30-second stability sample, WITHOUT readback/disk images.
        QTest::qWait(qEnvironmentVariableIsSet("PANDACLIP_DESKTOP_FAST_CHECK")?1000:30000);GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel1,&user1);GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));auto endDiag=source->diagnostics();auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};const double cpu=(ticks(kernel1)+ticks(user1)-ticks(kernel0)-ticks(user0))/10000.0/(GetTickCount64()-start)*100.;
        results.append(QJsonObject{{"idle/readback-free measurement seconds",double(GetTickCount64()-start)/1000},{"CPU percent of one logical processor",cpu},{"workingSet start bytes",double(startBytes)},{"workingSet end bytes",double(memory.WorkingSetSize)},{"start",QJsonObject::fromVariantMap(startDiag)},{"end",QJsonObject::fromVariantMap(endDiag)}});
        root->setProperty("backgroundMode",2);QTest::qWait(200);GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel0,&user0);const auto solidStart=GetTickCount64();auto solidStartDiag=source->diagnostics();QTest::qWait(qEnvironmentVariableIsSet("PANDACLIP_DESKTOP_FAST_CHECK")?500:10000);GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel1,&user1);const double solidCpu=(ticks(kernel1)+ticks(user1)-ticks(kernel0)-ticks(user0))/10000.0/(GetTickCount64()-solidStart)*100.;results.append(QJsonObject{{"solid baseline seconds",double(GetTickCount64()-solidStart)/1000},{"CPU percent of one logical processor",solidCpu},{"start",QJsonObject::fromVariantMap(solidStartDiag)},{"end",QJsonObject::fromVariantMap(source->diagnostics())}});
        check("bounded crop buffers after longer sample",endDiag["cropBytes"].toULongLong()==startDiag["cropBytes"].toULongLong());check("owned windows never activated",!ownedActivation);
        KillTimer(sceneWindow,1);DestroyWindow(sceneWindow);UnhookWinEvent(hook);
        QFile report(folder+"/desktop-checks.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"passed",all},{"DPR",dpr},{"evidence","Qt GPU rendered-output readback with exclusion kept enabled; background is separate owned physical-desktop HWND"},{"results",results}}).toJson());
        QCoreApplication::exit(all?0:1);
    });
}
