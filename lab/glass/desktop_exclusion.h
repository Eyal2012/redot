#pragma once
#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <windows.h>
// Prototype-only. Affinity is applied before Show to every owned Qt HWND.
class DesktopExclusion final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool safe READ safe NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    QTimer timer_;bool safe_{true};QString status_;
    bool apply(QWindow* w){
        if(!w->handle())return true;
        const auto h=reinterpret_cast<HWND>(w->winId());DWORD pid{},value{};GetWindowThreadProcessId(h,&pid);
        if(pid!=GetCurrentProcessId()||GetAncestor(h,GA_ROOT)!=h)return true;
        DWORD current{};if(GetWindowDisplayAffinity(h,&current)&&current==WDA_EXCLUDEFROMCAPTURE)return true;
        SetLastError(0);const bool set=SetWindowDisplayAffinity(h,WDA_EXCLUDEFROMCAPTURE)!=FALSE;const DWORD error=GetLastError();
        const bool get=GetWindowDisplayAffinity(h,&value)!=FALSE;
        if(!set||!get||value!=WDA_EXCLUDEFROMCAPTURE){status_=QString("Exclusion failed: HWND %1, set %2, Win32 %3, readback 0x%4; Custom Desktop disabled").arg(quintptr(h),0,16).arg(set).arg(error).arg(value,0,16);return false;}return true;
    }
public:
    explicit DesktopExclusion(QObject* parent=nullptr):QObject(parent){qApp->installEventFilter(this);timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{refresh();});timer_.start();}
    bool safe()const{return safe_;}QString status()const{return status_;}
    void refresh(){bool ok=true;for(auto w:QGuiApplication::topLevelWindows())ok=apply(w)&&ok;safe_=ok;if(ok)status_="Owned HWND exclusion effective: 0x11; visual source validation is separate";Q_EMIT changed();}
    bool eventFilter(QObject* o,QEvent* e)override{if(auto w=qobject_cast<QWindow*>(o);w&&(e->type()==QEvent::Show||e->type()==QEvent::WinIdChange)){safe_=apply(w)&&safe_;Q_EMIT changed();}return false;}
Q_SIGNALS:void changed();
};
