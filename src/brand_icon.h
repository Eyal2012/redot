#pragma once
#include "brand_state.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include <QTimer>
#include <QIcon>
#include <QVariant>
#include <map>
#include <windows.h>
namespace Branding {
inline const QIcon& icon(Mode mode=Mode::Idle){
    static const QIcon variants[]={QIcon(":/ReDot/idle.ico"),QIcon(":/ReDot/replay.ico"),QIcon(":/ReDot/recording.ico")};
    return variants[static_cast<unsigned>(mode)];
}
class TaskbarIcon final:public QObject,public QAbstractNativeEventFilter {
    QPointer<QWidget> owner_;Mode mode_{Mode::Idle},applied_{Mode::Idle};HWND hwnd_{};UINT dpi_{};bool valid_{},pending_{};
    std::map<std::pair<unsigned,int>,HICON> handles_;
    UINT taskbarCreated_{RegisterWindowMessageW(L"TaskbarCreated")},buttonCreated_{RegisterWindowMessageW(L"TaskbarButtonCreated")};
    HICON handle(int size){auto key=std::make_pair(static_cast<unsigned>(mode_),size);auto found=handles_.find(key);if(found!=handles_.end())return found->second;auto result=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101+key.first),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));if(result)handles_[key]=result;return result;}
    void refresh(){
        pending_=false;if(!owner_||!owner_->windowHandle())return;
        auto hwnd=reinterpret_cast<HWND>(owner_->effectiveWinId());if(!IsWindow(hwnd))return;
        UINT dpi=GetDpiForWindow(hwnd);if(valid_&&hwnd==hwnd_&&dpi==dpi_&&mode_==applied_)return;
        auto smallIcon=handle(GetSystemMetricsForDpi(SM_CXSMICON,dpi)),largeIcon=handle(GetSystemMetricsForDpi(SM_CXICON,dpi));if(!smallIcon||!largeIcon){owner_->setProperty("taskbarIconError","Unable to load ReDot native icon resources");return;}
        // Qt retains the neutral brand for captions and recreation. Apply only
        // cached native status handles; WinIdChange reapplies them on a new HWND.
        SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(smallIcon));SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(largeIcon));
        hwnd_=hwnd;dpi_=dpi;applied_=mode_;valid_=true;
        owner_->setProperty("taskbarMode",int(mode_));owner_->setProperty("taskbarIconUpdates",owner_->property("taskbarIconUpdates").toULongLong()+1);
    }
    void schedule(){if(pending_)return;pending_=true;QTimer::singleShot(0,this,[this]{refresh();});}
    bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::WinIdChange||event->type()==QEvent::DevicePixelRatioChange){valid_=false;schedule();}else if(event->type()==QEvent::Show)schedule();return false;}
    bool nativeEventFilter(const QByteArray&,void* message,qintptr*)override{
        auto m=static_cast<MSG*>(message);if(m->message==taskbarCreated_||(m->hwnd==hwnd_&&(m->message==buttonCreated_||m->message==WM_DPICHANGED))){valid_=false;schedule();}return false;
    }
public:
    explicit TaskbarIcon(QWidget* owner):QObject(owner),owner_(owner){owner->installEventFilter(this);qApp->installNativeEventFilter(this);refresh();}
    ~TaskbarIcon()override{qApp->removeNativeEventFilter(this);if(owner_)owner_->removeEventFilter(this);if(IsWindow(hwnd_)){SendMessageW(hwnd_,WM_SETICON,ICON_SMALL,0);SendMessageW(hwnd_,WM_SETICON,ICON_BIG,0);}for(auto [key,handle]:handles_)DestroyIcon(handle);}
    void update(Mode mode){mode_=mode;refresh();}
};
}
