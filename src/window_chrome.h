#pragma once
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QToolButton>
#include <QWindow>
#include <QMouseEvent>
#include <QStyle>
#include <QQuickWidget>
#include <QQuickItem>
#include <QPushButton>
#include <QTimer>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QMenuBar>
#include <QMenu>
#include <QFrame>
#include <QStatusBar>
#include <QDockWidget>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include "logging.h"
#include "desktop_backdrop.h"
#include "design_system.h"
#include <functional>

class WorkspaceMaterial final : public QWidget {
public:
    inline static QList<QPointer<WorkspaceMaterial>> surfaces;
    explicit WorkspaceMaterial(QWidget* parent):QWidget(parent){
        setAttribute(Qt::WA_TransparentForMouseEvents);setFocusPolicy(Qt::NoFocus);
        setStyleSheet("background:transparent;");surfaces.append(this);lower();
    }
    void information(const QString&,const QString&,bool){} // material no longer contains repeated source text
    ~WorkspaceMaterial()override{surfaces.removeAll(this);}
};
// One GPU scene per current top-level window, behind actual interactive widgets.
// DWM supplies the desktop backdrop; bounded GPU materials draw foreground tint/rims.
// Discovery runs only after widget structure changes, never on paint/scroll.
class WorkspaceGlassLayer final:public QQuickWidget {
    QWidget* owner_{};QTimer refresh_,discover_;QVariantList previous_;
    QList<QPointer<QWidget>> controls_;
    bool owns(QWidget* widget)const{
        if(widget==owner_||widget==this||!owner_->isAncestorOf(widget)||widget->window()!=owner_->window())return false;
        for(auto p=widget->parentWidget();p&&p!=owner_;p=p->parentWidget())if(p->property("materialHost").toBool())return false;
        return true;
    }
    void discover(){
        controls_.clear();
        for(auto w:owner_->findChildren<QWidget*>()) {
            if(!owns(w)||qobject_cast<QQuickWidget*>(w)||w->property("materialSuppressed").toBool())continue;
            if(qobject_cast<QLineEdit*>(w)&&(qobject_cast<QComboBox*>(w->parentWidget())||qobject_cast<QKeySequenceEdit*>(w->parentWidget())))continue;
            if(qobject_cast<QAbstractButton*>(w)||qobject_cast<QComboBox*>(w)||qobject_cast<QLineEdit*>(w)||
               qobject_cast<QKeySequenceEdit*>(w)||qobject_cast<QAbstractItemView*>(w)||
               qobject_cast<QPlainTextEdit*>(w)||
               qobject_cast<QMenuBar*>(w)||qobject_cast<QStatusBar*>(w))controls_.append(w);
            if(auto area=qobject_cast<QAbstractScrollArea*>(w)){
                area->viewport()->setAutoFillBackground(false);
                auto palette=area->viewport()->palette();palette.setColor(QPalette::Base,Qt::transparent);area->viewport()->setPalette(palette);
            }
        }
    }
    void updateRegions(){
        if(!owner_->isVisible()||!rootObject())return;
        QVariantList regions;
        auto append=[&](QWidget* w,int radius){
            if(!w||!w->isVisible()||!owns(w)||w->width()<8||w->height()<8||regions.size()>=128)return;
            const QRect bounds(w->mapTo(owner_,QPoint()),w->size());QRect clip=bounds.intersected(owner_->rect());
            for(auto p=w->parentWidget();p&&p!=owner_;p=p->parentWidget())clip=clip.intersected(QRect(p->mapTo(owner_,QPoint()),p->size()));
            if(clip.isEmpty())return;
            QVariantMap r;r["id"]=QString::number(reinterpret_cast<quintptr>(w));
            r["x"]=bounds.x();r["y"]=bounds.y();r["w"]=bounds.width();r["h"]=bounds.height();
            r["cx"]=clip.x();r["cy"]=clip.y();r["cw"]=clip.width();r["ch"]=clip.height();r["radius"]=radius;regions<<r;
        };
        for(auto& p:WorkspaceMaterial::surfaces)if(p)append(p,p->property("materialRadius").isValid()?p->property("materialRadius").toInt():24);
        // Interactive controls already paint the canonical QSS border/fill.
        // A second GPU rim produces mismatched arcs and doubled outlines.
        for(auto& w:controls_)if(w&&(qobject_cast<QAbstractItemView*>(w)||qobject_cast<QPlainTextEdit*>(w)))append(w,DesignSystem::values().value("controlRadius").toInt());
        bool available=owner_->property("desktopBackdropMaterial").toBool();
#ifdef REDOT_UI_VERIFICATION
        // Foreground alpha/renderer fixture only; never claims DWM transmission.
        available=available||qApp->property("verificationBackdropAlpha").toBool();
#endif
        const bool glass=available&&!qApp->property("solidMaterials").toBool();
        if(rootObject()->property("glass").toBool()!=glass)rootObject()->setProperty("glass",glass);
        if(regions!=previous_){previous_=regions;rootObject()->setProperty("regions",regions);}
        setProperty("materialRegionCount",regions.size());
    }
public:
    explicit WorkspaceGlassLayer(QWidget* owner):QQuickWidget(owner),owner_(owner){
        setObjectName("workspaceGlassLayer");setAttribute(Qt::WA_TransparentForMouseEvents);setFocusPolicy(Qt::NoFocus);
        auto format=this->format();format.setAlphaBufferSize(8);setFormat(format);setClearColor(Qt::transparent);
        setResizeMode(QQuickWidget::SizeRootObjectToView);DesignSystem::bind(engine());setSource(QUrl("qrc:/PandaClip/WorkspaceGlass.qml"));
        setGeometry(owner->rect());lower();show();qApp->installEventFilter(this);
        // winId() on a child host would CREATE an HWND and make its transparent
        // margins an opaque native rectangle. Diagnostics must be read-only.
        QTimer::singleShot(1000,this,[this]{logLine(L"Workspace material presentation: visible=%d hidden=%d native=%d status=%d ownerSurface=%d ownerExStyle=0x%llX.",isVisible(),isHidden(),testAttribute(Qt::WA_NativeWindow),int(status()),owner_->windowHandle()?int(owner_->windowHandle()->surfaceType()):-1,static_cast<unsigned long long>(GetWindowLongPtrW(reinterpret_cast<HWND>(owner_->effectiveWinId()),GWL_EXSTYLE)));});
        discover_.setSingleShot(true);discover_.setInterval(0);
        QObject::connect(&discover_,&QTimer::timeout,this,[this]{discover();updateRegions();});
        refresh_.setInterval(100);QObject::connect(&refresh_,&QTimer::timeout,this,[this]{updateRegions();});refresh_.start();discover_.start();
    }
    bool eventFilter(QObject* object,QEvent* e)override{
        if(object==owner_&&e->type()==QEvent::Resize){setGeometry(owner_->rect());lower();}
        if(e->type()==QEvent::ChildPolished||e->type()==QEvent::ParentChange||e->type()==QEvent::Show){
            if(auto w=qobject_cast<QWidget*>(object);w&&w!=this&&(w==owner_||w->window()==owner_))if(!discover_.isActive())discover_.start();
        }
        return false;
    }
};
// Destroy the floating scene before its owner changes RHI/native ancestry.
// A fresh stable layer is created after floating, never reparented with a dock.
class FloatingGlassHost final:public QObject {
    QDockWidget* dock_{};QPointer<WorkspaceGlassLayer> layer_;
public:
    explicit FloatingGlassHost(QDockWidget* dock):QObject(dock),dock_(dock){dock->installEventFilter(this);
        QObject::connect(dock,&QDockWidget::topLevelChanged,this,[this](bool floating){
            if(!floating){delete layer_;layer_=nullptr;dock_->setStyleSheet("");return;}
            // Qt 6.8's RHI QWidget backingstore cannot safely return a Quick
            // composition window to the dock hierarchy. Retain real docking
            // and use the shared solid material palette in detached docks.
            dock_->setStyleSheet(QString("QDockWidget {background:%1; border:1px solid %2; border-radius:14px;}").arg(DesignSystem::rgba("surfaceSolid"),DesignSystem::rgba("outline")));
        });
    }
    void detach(){delete layer_;layer_=nullptr;}
    bool eventFilter(QObject*,QEvent* e)override{
        if(e->type()==QEvent::ParentAboutToChange)detach();return false;
    }
};
class GlassAction final : public QPushButton {
public:
    explicit GlassAction(const QString& text):QPushButton(text){}
    void information(const QString&,const QString&,bool){}
};
class DockMaterialLayout final:public QObject {
    WorkspaceMaterial* material_{};
public:
    DockMaterialLayout(WorkspaceMaterial* m,QWidget* owner):QObject(owner),material_(m){material_->setGeometry(owner->rect());}
    bool eventFilter(QObject* target,QEvent* event)override{
        if(event->type()==QEvent::Resize)material_->setGeometry(static_cast<QWidget*>(target)->rect());
        return false; // Unhandled title-header mouse events propagate to QDockWidget for native docking.
    }
};

// Retain Windows' sizing/system-menu styles. Only the caption's layout/painting
// moves into Qt. Physical native hit tests and monitor work areas remain native.
class WindowChrome final : public QObject, public QAbstractNativeEventFilter {
    QPointer<QWindow> window_;
    QPointer<QWidget> widgetOwner_;
    HWND hwnd_{};
    HWND attached_{};
    std::function<QRect()> title_, maximum_;
    std::function<void(bool,bool)> maxState_;
    bool pressed_{};
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
        auto self=reinterpret_cast<WindowChrome*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(hwnd,procedure,id);if(self->attached_==hwnd)self->attached_=nullptr;return DefSubclassProc(hwnd,message,w,l);}
        MSG msg{};msg.hwnd=hwnd;msg.message=message;msg.wParam=w;msg.lParam=l;qintptr result{};
        if(self->nativeEventFilter({},&msg,&result))return result;
        return DefSubclassProc(hwnd,message,w,l);
    }
    void configure(){
        if(widgetOwner_&&widgetOwner_->windowHandle()!=window_){window_=widgetOwner_->windowHandle();if(window_)window_->installEventFilter(this);}
        if(!window_||!window_->handle())return;
        hwnd_=reinterpret_cast<HWND>(window_->winId());
        if(attached_!=hwnd_){
            if(attached_)RemoveWindowSubclass(attached_,procedure,reinterpret_cast<UINT_PTR>(this));
            attached_=hwnd_;SetWindowSubclass(hwnd_,procedure,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this));
            logLine(L"Custom chrome bound: title=%s, hwnd=%p.",window_->title().toStdWString().c_str(),hwnd_);
        }
        auto style=GetWindowLongPtrW(hwnd_,GWL_STYLE);
        SetWindowLongPtrW(hwnd_,GWL_STYLE,style|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX);
        BOOL dark=TRUE;DwmSetWindowAttribute(hwnd_,20,&dark,sizeof(dark));
        // One contour, supplied by DWM at physical-pixel precision. Qt fills
        // the rectangular client; it must not draw a second rounded window rim.
        const auto border=DesignSystem::color("windowBorder");
        const COLORREF edge=RGB(border.red(),border.green(),border.blue());
        DwmSetWindowAttribute(hwnd_,34,&edge,sizeof(edge));
        const int corners=2;DwmSetWindowAttribute(hwnd_,33,&corners,sizeof(corners));
        const bool desktop=widgetOwner_?widgetOwner_->property("desktopBackdropAccepted").toBool():window_->property("desktopBackdropAccepted").toBool();
        const MARGINS margins=desktop?MARGINS{-1,-1,-1,-1}:MARGINS{1,1,1,1};DwmExtendFrameIntoClientArea(hwnd_,&margins);
        SetWindowPos(hwnd_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
public:
    WindowChrome(QWindow* window, QObject* owner,std::function<QRect()> title,std::function<QRect()> maximum,
                 std::function<void(bool,bool)> state={})
        :QObject(owner),window_(window),widgetOwner_(qobject_cast<QWidget*>(owner)),title_(std::move(title)),maximum_(std::move(maximum)),maxState_(std::move(state)) {
        hwnd_=reinterpret_cast<HWND>(window->winId());
        if(widgetOwner_)widgetOwner_->installEventFilter(this);
        window->installEventFilter(this);configure();
    }
    ~WindowChrome()override {if(attached_)RemoveWindowSubclass(attached_,procedure,reinterpret_cast<UINT_PTR>(this));}
    bool eventFilter(QObject*,QEvent* event)override{
        // Enabling Widget GPU composition can recreate a dialog's platform
        // window. Rebind after creation/show and reapply the custom frame.
        if(event->type()==QEvent::PlatformSurface||event->type()==QEvent::Show||event->type()==QEvent::WinIdChange)
            QTimer::singleShot(0,this,[this]{configure();});
        return false;
    }
    HRESULT backdrop(bool enabled){
        const int kind=enabled?3:1; // documented DWMSBT_TRANSIENTWINDOW / NONE (Windows 11 22621+).
        return DwmSetWindowAttribute(hwnd_,38,&kind,sizeof(kind));
    }
    bool nativeEventFilter(const QByteArray&,void* message,qintptr* result)override {
        auto m=static_cast<MSG*>(message);if(!window_)return false;
        if(m->hwnd!=hwnd_){if(!window_->handle()||m->hwnd!=reinterpret_cast<HWND>(window_->winId()))return false;hwnd_=m->hwnd;}
        // Qt owns the full client. DefWindowProc's classic sizing-frame paint
        // otherwise overlays a white rectangle despite our zero NC margins.
        if(m->message==WM_NCPAINT){*result=0;return true;}
        // Custom caption visual state is invariant. This is nonclient paint
        // policy only, never SetActiveWindow/SetForegroundWindow or focus input.
        if(m->message==WM_NCACTIVATE){
            if(IsIconic(hwnd_))*result=DefWindowProcW(hwnd_,m->message,m->wParam,-1);
            else{
                DefWindowProcW(hwnd_,m->message,TRUE,-1);
                // Explicitly allow the real activation change. A FALSE return
                // to an incoming deactivation could prevent it.
                *result=TRUE;
            }
            logLine(L"Custom chrome nonclient visual policy hwnd=%p requestedActive=%d foreground=%p; input activation is not overridden.",hwnd_,!!m->wParam,GetForegroundWindow());
            return true;
        }
        if(m->message==WM_ERASEBKGND){*result=1;return true;}
        if(m->message==WM_NCCALCSIZE&&m->wParam){
            if(IsZoomed(hwnd_)){
                auto p=reinterpret_cast<NCCALCSIZE_PARAMS*>(m->lParam);
                MONITORINFO info{sizeof(info)};if(GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&info))p->rgrc[0]=info.rcWork;
            }
            *result=0;return true;
        }
        if(m->message==WM_GETMINMAXINFO){
            auto p=reinterpret_cast<MINMAXINFO*>(m->lParam);MONITORINFO info{sizeof(info)};
            if(GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&info)){
                p->ptMaxPosition={info.rcWork.left-info.rcMonitor.left,info.rcWork.top-info.rcMonitor.top};
                p->ptMaxSize={info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top};
            }
            const auto size=window_->minimumSize();const auto scale=window_->devicePixelRatio();
            p->ptMinTrackSize={LONG(size.width()*scale),LONG(size.height()*scale)};
            *result=0;return true;
        }
        if(m->message==WM_NCHITTEST){
            if(window_->visibility()==QWindow::FullScreen)return false;
            POINT point{GET_X_LPARAM(m->lParam),GET_Y_LPARAM(m->lParam)};ScreenToClient(hwnd_,&point);
            RECT rect{};GetClientRect(hwnd_,&rect);
            int edge=GetSystemMetricsForDpi(SM_CXSIZEFRAME,GetDpiForWindow(hwnd_))+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,GetDpiForWindow(hwnd_));
            if(!IsZoomed(hwnd_)){
                bool l=point.x<edge,r=point.x>=rect.right-edge,t=point.y<edge,b=point.y>=rect.bottom-edge;
                if(l||r||t||b){*result=t?(l?HTTOPLEFT:r?HTTOPRIGHT:HTTOP):b?(l?HTBOTTOMLEFT:r?HTBOTTOMRIGHT:HTBOTTOM):l?HTLEFT:HTRIGHT;return true;}
            }
            const auto scale=window_->devicePixelRatio();QPoint local(int(point.x/scale),int(point.y/scale));
            if(maximum_&&maximum_().contains(local)){*result=HTMAXBUTTON;return true;}
            if(title_&&title_().contains(local)){*result=HTCAPTION;return true;}
            *result=HTCLIENT;return true;
        }
        if(m->message==WM_NCMOUSEMOVE&&m->wParam==HTMAXBUTTON){
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,hwnd_,0};TrackMouseEvent(&track);
            if(maxState_)maxState_(true,pressed_);return false; // DefWindowProc supplies Windows snap layouts.
        }
        if(m->message==WM_NCLBUTTONDOWN&&(m->wParam==HTCAPTION||(m->wParam>=HTLEFT&&m->wParam<=HTBOTTOMRIGHT))){
            // This is a native caption/resize action, not a dock-layout mouse
            // event. Enter Windows' move/size loop directly; a Qt client-event
            // filter must not swallow it after our custom non-client hit test.
            *result=DefWindowProcW(hwnd_,m->message,m->wParam,m->lParam);return true;
        }
        if(m->message==WM_NCLBUTTONDBLCLK&&m->wParam==HTCAPTION){toggleMaximize();*result=0;return true;}
        if(m->message==WM_NCMOUSELEAVE){if(maxState_)maxState_(false,false);pressed_=false;}
        if(m->message==WM_NCLBUTTONDOWN&&m->wParam==HTMAXBUTTON){pressed_=true;if(maxState_)maxState_(true,true);*result=0;return true;}
        if(m->message==WM_NCLBUTTONUP&&pressed_){
            pressed_=false;if(maxState_)maxState_(false,false);
            if(m->wParam==HTMAXBUTTON)toggleMaximize();*result=0;return true;
        }
        return false; // Qt handles DPI, Alt+F4, WM_SYSCOMMAND, menus and native move/resize loops.
    }
    void toggleMaximize(){
        if(widgetOwner_){widgetOwner_->isMaximized()?widgetOwner_->showNormal():widgetOwner_->showMaximized();return;}
        if(window_)window_->visibility()==QWindow::Maximized?window_->showNormal():window_->showMaximized();
    }
};

// A transparent surface above the native compositor material. No preview grab,
// CPU video blur or capture of the desktop is used by presentation.
class ChromeTitle final : public QWidget {
    QWidget* owner_{};
    QToolButton *maximum_{},*minimum_{},*close_{};
    QToolButton* options_{};
    QLabel* title_{};
public:
    WindowChrome* chrome{};
    ChromeTitle(QWidget* owner,const QString& title):QWidget(owner),owner_(owner){
        setObjectName("customTitleBar");setFixedHeight(52);setAttribute(Qt::WA_StyledBackground,false);
        auto row=new QHBoxLayout(this);row->setContentsMargins(12,4,6,4);row->setSpacing(5);
        auto icon=new QLabel;icon->setPixmap(QApplication::windowIcon().pixmap(22,22));row->addWidget(icon);
        title_=new QLabel(title);title_->setStyleSheet("background:transparent; font-size:13px;font-weight:500;");row->addWidget(title_);row->addStretch();
        auto button=[&](QString name,QString text){auto b=new QToolButton(this);b->setObjectName(name);b->setProperty("caption",true);b->setProperty("materialSuppressed",true);b->setAccessibleName(name);b->setToolTip(name);b->setText(text);b->setFixedSize(43,32);b->setFocusPolicy(Qt::StrongFocus);row->addWidget(b);return b;};
        minimum_=button("Minimize","—");maximum_=button("Maximize","□");close_=button("Close","×");
        QObject::connect(minimum_,&QToolButton::clicked,owner,[owner]{owner->showMinimized();});
        setStyleSheet(styleSheet()+"QWidget#customTitleBar {background:transparent;} QLabel {background:transparent;}");
        QObject::connect(maximum_,&QToolButton::clicked,owner,[owner]{owner->isMaximized()?owner->showNormal():owner->showMaximized();});
        QObject::connect(close_,&QToolButton::clicked,owner,[owner]{owner->close();});
        owner->winId();
        chrome=new WindowChrome(owner->windowHandle(),owner,[this]{return QRect(mapTo(owner_,QPoint(36,5)),QSize(std::max(0,width()-(options_?246:190)),height()-10));},
            [this]{return QRect(maximum_->mapTo(owner_,QPoint()),maximum_->size());},[this](bool hover,bool pressed){
                maximum_->setProperty("nativeHover",hover);maximum_->setProperty("nativePressed",pressed);maximum_->style()->unpolish(maximum_);maximum_->style()->polish(maximum_);
            });
    }
    void material(bool){} // accessibility/backdrop policy belongs to the root
    void options(QMenu* menu){options_=new QToolButton(this);options_->setText("⋯");options_->setAccessibleName("Application menu");options_->setToolTip("Application menu and advanced dock layout");options_->setFixedSize(36,32);options_->setMenu(menu);options_->setPopupMode(QToolButton::InstantPopup);static_cast<QHBoxLayout*>(layout())->insertWidget(3,options_);}
    void information(const QString&,const QString&,bool){}
protected:
    void mousePressEvent(QMouseEvent* e)override{
        if(e->button()==Qt::LeftButton){
            setProperty("moveRequests",property("moveRequests").toInt()+1);
            // Native child composition can deliver client mouse events without
            // the root HWND's caption hit test. Explicitly enter the platform
            // move operation; Windows retains snap and maximized restore.
            const bool accepted=owner_->windowHandle()->startSystemMove();
            setProperty("moveAccepted",accepted);e->accept();return;
        }
        QWidget::mousePressEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent* e)override{
        if(e->button()==Qt::LeftButton){owner_->isMaximized()?owner_->showNormal():owner_->showMaximized();e->accept();return;}
        QWidget::mouseDoubleClickEvent(e);
    }
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        maximum_->setText(owner_->isMaximized()?"❐":"□");maximum_->setAccessibleName(owner_->isMaximized()?"Restore":"Maximize");
    }
};
