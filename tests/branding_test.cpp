#include "brand_icon.h"
#include "settings_store.h"
#include <QFile>
#include <QDir>
#include <QPainter>
#include <QElapsedTimer>
#include <iostream>
#include <vector>
struct NativeTestWindow:QWidget{void recreate(){hide();destroy();winId();show();}};
int main(int argc,char** argv){
    const auto previous=GetThreadDesktop(GetCurrentThreadId());auto name=L"ReDotBrandTest_"+std::to_wstring(GetCurrentProcessId());auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 2;
    int failures=0;{
    QApplication app(argc,argv);auto args=app.arguments();if(args.size()!=2)return 2;QDir().mkpath(args[1]+"/local");SetEnvironmentVariableW(L"LOCALAPPDATA",(args[1]+"/local").toStdWString().c_str());
    auto check=[&](bool value,const char* message){std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';if(!value)++failures;};
    using namespace Branding;
    check(engineMode(false,false,0,false,false)==Mode::Idle,"initializing recording is neutral");check(engineMode(false,false,0,true,true)==Mode::Idle,"failed or initializing replay is neutral");check(engineMode(true,false,1,true,true)==Mode::Replay,"completed replay frame enables green");check(engineMode(true,false,1,false,true)==Mode::Recording,"recording takes priority over replay");check(engineMode(true,true,1,false,true)==Mode::Idle,"draining recording is not advertised active");check(engineMode(false,false,100,false,true)==Mode::Idle,"stopped or failed worker is neutral despite old frame count");
    QApplication::setWindowIcon(icon());NativeTestWindow window;window.setWindowTitle("ReDot brand verification");window.resize(320,200);window.winId();window.show();QApplication::processEvents();const auto foreground=GetForegroundWindow();auto hwnd=reinterpret_cast<HWND>(window.winId());
    {
        TaskbarIcon taskbar(&window);for(auto mode:{Mode::Idle,Mode::Replay,Mode::Idle,Mode::Recording,Mode::Recording,Mode::Replay,Mode::Idle}){taskbar.update(mode);QApplication::processEvents();check(window.property("taskbarMode").toUInt()==unsigned(mode),"native top-level reflects expected taskbar state");check(SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0)!=0&&SendMessageW(hwnd,WM_GETICON,ICON_BIG,0)!=0,"native small and large icons installed");auto native=QImage::fromHICON(reinterpret_cast<HICON>(SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0)));auto dot=native.pixelColor(native.width()*50/64,native.height()*43/64);check(mode==Mode::Replay?dot.green()>dot.red()+40:mode==Mode::Recording?dot.red()>dot.green()+80:dot.blue()>dot.green(),"native HICON contains the correct status-dot color");}
        auto updates=window.property("taskbarIconUpdates").toULongLong();for(int n=0;n<1000;++n)taskbar.update(Mode::Idle);check(window.property("taskbarIconUpdates").toULongLong()==updates,"unchanged engine state performs zero icon writes");
        window.showMinimized();app.processEvents();taskbar.update(Mode::Replay);window.showNormal();app.processEvents();check(window.property("taskbarMode").toUInt()==1,"minimize and restore retain status");
        SendMessageW(hwnd,WM_ACTIVATE,WA_INACTIVE,0);SendMessageW(hwnd,WM_ACTIVATEAPP,FALSE,0);updates=window.property("taskbarIconUpdates").toULongLong();app.processEvents();check(updates==window.property("taskbarIconUpdates").toULongLong(),"focus notifications perform zero icon writes");
        QElapsedTimer warm;warm.start();int warmStep=0;while(warm.elapsed()<2000){taskbar.update(Mode(warmStep++%3));app.processEvents();Sleep(1);}auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);for(int batch=0;batch<3;++batch){for(int n=0;n<300;++n){taskbar.update(Mode(n%3));app.processEvents();}std::cout<<"RESOURCE batch="<<batch<<" GDI="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" USER="<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<"\n";}std::cout<<"RESOURCES before GDI="<<gdi<<" USER="<<user<<" after GDI="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" USER="<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<"\n";check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+4&&GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user+4,"cached transitions do not leak native resources");
        check(QApplication::windowIcon().cacheKey()==icon().cacheKey(),"application captions and tray keep neutral branding");
        taskbar.update(Mode::Replay);window.recreate();app.processEvents();hwnd=reinterpret_cast<HWND>(window.winId());check(window.property("taskbarMode").toUInt()==1&&SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0)!=0,"recreated HWND receives current native status icon");auto beforeReset=window.property("taskbarIconUpdates").toULongLong();PostMessageW(hwnd,RegisterWindowMessageW(L"TaskbarCreated"),0,0);app.processEvents();app.processEvents();check(window.property("taskbarIconUpdates").toULongLong()>beforeReset,"Explorer taskbar reconstruction reapplies cached icon");
    }
    check(GetForegroundWindow()==foreground,"test preserves physical foreground");
    QImage sheet(940,450,QImage::Format_ARGB32);sheet.fill(QColor("#201B2B"));QPainter paint(&sheet);paint.setPen(Qt::white);int y=12;
    for(int size:{16,24,32,48,256}){int x=100;paint.drawText(10,y+16,QString::number(size)+" px");for(auto mode:{Mode::Idle,Mode::Replay,Mode::Recording}){auto image=icon(mode).pixmap(size,size).toImage().convertToFormat(QImage::Format_ARGB32);paint.drawImage(x,y,image);x+=size+15;}y+=size+9;}
    paint.end();sheet.save(args[1]+"/logo-sizes.png");
    for(int size:{16,24,32,48,256}){auto base=icon().pixmap(size,size).toImage().convertToFormat(QImage::Format_ARGB32);for(auto mode:{Mode::Replay,Mode::Recording}){auto image=icon(mode).pixmap(size,size).toImage().convertToFormat(QImage::Format_ARGB32);bool same=true,different=false;for(int y=0;y<size;++y)for(int x=0;x<size;++x)if(base.pixel(x,y)!=image.pixel(x,y)){different=true;double dx=(x+.5)*64/size-50,dy=(y+.5)*64/size-43;if(dx*dx+dy*dy>100)same=false;}check(same&&different,"only the integrated dot changes at every tested size");}}
    {SettingsStore settings;settings.save({{"duration",L"37"},{"folder",L"C:/Owned/Existing clips"},{"sourceTitle",L"Existing game"}});}std::wstring warning;SettingsStore settings;auto loaded=settings.load(warning);check(warning.empty()&&loaded["duration"]==L"37"&&loaded["folder"]==L"C:/Owned/Existing clips","legacy settings location and schema are reused without migration or relocation");
    QFile result(args[1]+"/result.txt");result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures));std::cout<<"RESULT failures="<<failures<<'\n';window.close();app.processEvents();
    }SetThreadDesktop(previous);CloseDesktop(desktop);return failures?1:0;
}
