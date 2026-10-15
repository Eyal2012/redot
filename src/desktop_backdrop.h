#pragma once
#include <QWidget>
#include <QWindow>
#include <QTimer>
#include <QPointer>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include "logging.h"

// DWM owns scene acquisition/blur. Never capture or copy desktop pixels here.
class DesktopBackdrop final : public QObject, public QAbstractNativeEventFilter {
    QPointer<QWidget> owner_;
    QPointer<QWindow> quickOwner_;
    HWND applied_{};
    HWND attached_{};
    bool requested_{true};
    bool previousRequest_{};
    bool policyReadPending_{true};
    bool previousPolicy_{};
    bool forceNativeRefresh_{};
    HRESULT last_{E_PENDING};
    QTimer policyRefresh_;
    QTimer focusRefresh_;
    quint64 generation_{};
    quint64 nativeApplies_{};
    quint64 focusNotifications_{};
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
        auto self=reinterpret_cast<DesktopBackdrop*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(hwnd,procedure,id);if(self->attached_==hwnd)self->attached_=nullptr;}
        else if(message==WM_SETTINGCHANGE||message==WM_DWMCOMPOSITIONCHANGED||message==WM_THEMECHANGED){
            // QWidget's platform handler consumes some settings messages before
            // QApplication native filters see them. Observe our own HWND first.
            if(!self->policyRefresh_.isActive())self->policyRefresh_.start();
            self->forceNativeRefresh_|=message==WM_DWMCOMPOSITIONCHANGED;
            logLine(L"Backdrop policy notification hwnd=%p message=0x%X generation=%llu.",hwnd,message,self->generation_);
        }
        else if(message==WM_ACTIVATE||message==WM_ACTIVATEAPP){
            ++self->focusNotifications_;
            if(!self->focusRefresh_.isActive())self->focusRefresh_.start();
        }
        return DefSubclassProc(hwnd,message,w,l);
    }
    QObject* properties()const{return owner_?static_cast<QObject*>(owner_.data()):static_cast<QObject*>(quickOwner_.data());}
    void install(QObject* window){
        window->installEventFilter(this);qApp->installEventFilter(this);qApp->installNativeEventFilter(this);
        policyRefresh_.setSingleShot(true);policyRefresh_.setInterval(100);
        QObject::connect(&policyRefresh_,&QTimer::timeout,this,[this]{refreshPolicy();});
        focusRefresh_.setSingleShot(true);focusRefresh_.setInterval(0);
        QObject::connect(&focusRefresh_,&QTimer::timeout,this,[this]{refreshFocusDiagnostics();});
    }
    void refreshFocusDiagnostics(){
        if(!properties()||!applied_)return;
        const auto foreground=GetForegroundWindow();
        properties()->setProperty("desktopBackdropActive",foreground==applied_);
        properties()->setProperty("desktopBackdropInteractionActive",sameActivationFamily(applied_,foreground));
        properties()->setProperty("backdropFocusNotifications",focusNotifications_);
        logLine(L"Backdrop focus observation hwnd=%p foreground=%p active=%d notifications=%llu nativeApplies=%llu; no native backdrop/margin writes.",applied_,foreground,foreground==applied_,focusNotifications_,nativeApplies_);
    }
public:
    // An owned popup/menu is part of its window's interaction, not a switch
    // to an unrelated application. Never use PID equality (tray/other roots).
    static bool sameActivationFamily(HWND window,HWND foreground){
        if(!window||!foreground)return false;
        return GetAncestor(window,GA_ROOTOWNER)==GetAncestor(foreground,GA_ROOTOWNER);
    }
    static void prepare(QWidget* window) {
        window->setWindowFlag(Qt::FramelessWindowHint,true);
        window->setAttribute(Qt::WA_TranslucentBackground);
        window->setAutoFillBackground(false);
    }
    explicit DesktopBackdrop(QWidget* owner):QObject(owner),owner_(owner) {
        install(owner);
    }
    explicit DesktopBackdrop(QWindow* owner):QObject(owner),quickOwner_(owner){install(owner);}
    ~DesktopBackdrop()override{if(attached_)RemoveWindowSubclass(attached_,procedure,reinterpret_cast<UINT_PTR>(this));qApp->removeNativeEventFilter(this);}
    void update(bool requested) {
        requested_=requested;
        if(!properties()|| (owner_&&!owner_->windowHandle()) || (quickOwner_&&!quickOwner_->handle()))return;
        auto hwnd=reinterpret_cast<HWND>(owner_?owner_->winId():quickOwner_->winId());
        if(attached_!=hwnd){
            if(attached_)RemoveWindowSubclass(attached_,procedure,reinterpret_cast<UINT_PTR>(this));
            attached_=hwnd;if(!SetWindowSubclass(hwnd,procedure,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this)))logLine(L"Backdrop HWND notification attachment failed: hwnd=%p error=%lu.",hwnd,GetLastError());
        }
        if(applied_==hwnd&&previousRequest_==requested&&!policyReadPending_)return;
        policyReadPending_=false;previousRequest_=requested;
        HIGHCONTRASTW contrast{sizeof(contrast)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        BOOL composition{};DwmIsCompositionEnabled(&composition);
        bool policy=requested&&composition&&!(contrast.dwFlags&HCF_HIGHCONTRASTON);
        DWORD transparency=1,bytes=sizeof(transparency);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&bytes);
        policy=policy&&transparency!=0;
        // Re-querying policy is not a reason to reconstruct the native effect.
        // Only a different HWND, effective enable state or compositor reset
        // requires native writes. Focus is not part of this decision.
        if(applied_!=hwnd||previousPolicy_!=policy||forceNativeRefresh_){
            const int kind=policy?3:1; // DWMSBT_TRANSIENTWINDOW / NONE
            const BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
            last_=DwmSetWindowAttribute(hwnd,38,&kind,sizeof(kind));
            const MARGINS margins=policy&&SUCCEEDED(last_)?MARGINS{-1,-1,-1,-1}:MARGINS{1,1,1,1};
            auto marginResult=DwmExtendFrameIntoClientArea(hwnd,&margins);
            ++nativeApplies_;
            logLine(L"Backdrop native apply hwnd=%p count=%llu enabled=%d HRESULT=0x%08X marginsHRESULT=0x%08X compositorReset=%d.",hwnd,nativeApplies_,policy,unsigned(last_),unsigned(marginResult),forceNativeRefresh_);
        }
        applied_=hwnd;previousPolicy_=policy;forceNativeRefresh_=false;
        const bool accepted=policy&&SUCCEEDED(last_);
        const auto foreground=GetForegroundWindow();
        const bool nativeActive=foreground==hwnd;
        const bool active=sameActivationFamily(hwnd,foreground);
        // Focus is diagnostic only. Clicking a window on either monitor must
        // not change Qt alpha/materials. DWM still owns its native blur policy.
        properties()->setProperty("desktopBackdropAccepted",accepted);
        properties()->setProperty("desktopBackdropMaterial",accepted);
        properties()->setProperty("desktopBackdropActive",nativeActive);
        properties()->setProperty("desktopBackdropInteractionActive",active);
        properties()->setProperty("backdropNativeApplies",nativeApplies_);
        logLine(L"Backdrop material hwnd=%p foregroundHwnd=%p ownedInteraction=%d material=%s; focus does not select a Qt fallback; API state is not visible transmission evidence.",hwnd,GetForegroundWindow(),active,accepted?L"Acrylic":L"solid");
        properties()->setProperty("windowsTransparencyDisabled",transparency==0);
        properties()->setProperty("backdropRefreshGeneration",++generation_);
        properties()->setProperty("desktopBackdropStatus",accepted?
            QString("Desktop Acrylic requested; native applies=%1; focus does not reapply the backdrop. Windows controls effective blur; physical focus-transition verification required.").arg(nativeApplies_):
            !requested?"Solid appearance selected":!composition?"Solid fallback: desktop composition unavailable":
            (contrast.dwFlags&HCF_HIGHCONTRASTON)?"Solid fallback: Windows high contrast is enabled":
            transparency==0?"Solid fallback: Windows Transparency effects are off (Settings > Accessibility > Visual effects)":
            QString("Solid fallback: Desktop Acrylic unavailable (HRESULT 0x%1)").arg(quint32(last_),8,16,QChar('0')));
        logLine(L"Desktop Acrylic hwnd=%p generation=%llu request=%d, composition=%d, highContrast=%d, WindowsTransparency=%lu, policy=%d, accepted=%d, HRESULT=0x%08X; visible compositor effect not established by API success.",hwnd,generation_,requested,composition,!!(contrast.dwFlags&HCF_HIGHCONTRASTON),transparency,policy,accepted,unsigned(last_));
    }
    bool eventFilter(QObject* object,QEvent* event) override {
        if(owner_&&(event->type()==QEvent::Show||event->type()==QEvent::WinIdChange)){
            if(auto popup=qobject_cast<QWidget*>(object);popup&&popup->windowType()==Qt::Popup&&popup->parentWidget()&&popup->parentWidget()->window()==owner_&&owner_->windowHandle()&&owner_->windowHandle()->handle()&&popup->windowHandle()&&popup->windowHandle()->handle()){
                // Qt's native choice/menu popup can be unowned on this Windows
                // plugin. Associate only our logical child, before activation.
                const auto hwnd=reinterpret_cast<HWND>(popup->windowHandle()->winId());
                const auto parent=reinterpret_cast<HWND>(owner_->windowHandle()->winId());
                if(GetWindow(hwnd,GW_OWNER)!=parent){
                    SetLastError(0);SetWindowLongPtrW(hwnd,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(parent));
                    logLine(L"Owned native popup hwnd=%p parent=%p effectiveOwner=%p error=%lu.",hwnd,parent,GetWindow(hwnd,GW_OWNER),GetLastError());
                }
            }
        }
        if(object!=properties())return false;
        if(event->type()==QEvent::Show||event->type()==QEvent::WinIdChange||event->type()==QEvent::Expose) {
            if(!policyRefresh_.isActive())policyRefresh_.start();
        }
        else if(event->type()==QEvent::WindowActivate||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::ActivationChange){
            ++focusNotifications_;
            if(!focusRefresh_.isActive())focusRefresh_.start();
        }
        return false;
    }
    void refreshPolicy(){policyReadPending_=true;update(requested_);}
    bool nativeEventFilter(const QByteArray&,void* message,qintptr*)override{
        auto m=static_cast<MSG*>(message);
        if(m->hwnd==applied_&&(m->message==WM_SETTINGCHANGE||m->message==WM_DWMCOMPOSITIONCHANGED||m->message==WM_THEMECHANGED)){
            forceNativeRefresh_|=m->message==WM_DWMCOMPOSITIONCHANGED;
            if(!policyRefresh_.isActive())policyRefresh_.start();
        }
        return false;
    }
};
