#pragma once
// Glass Lab only: Qt owns the entire client, including custom chrome. Native
// sizing styles remain for hit tests, but DefWindowProc must not paint a second
// classic non-client frame over Qt's DirectComposition surface.
class LabNativeSurface final : public QObject {
    HWND hwnd_{};
    QPointer<LabController> controller_;
    bool legacy_{};
    unsigned ncPaints_{}, erases_{};
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
        auto self=reinterpret_cast<LabNativeSurface*>(data);
        if(m==WM_NCPAINT){++self->ncPaints_;if(!self->legacy_)return 0;}
        if(m==WM_ERASEBKGND){++self->erases_;if(!self->legacy_)return 1;}
        if(self->controller_&&(m==WM_ACTIVATE||m==WM_SETTINGCHANGE||m==WM_DWMCOMPOSITIONCHANGED||m==WM_THEMECHANGED||m==WM_WINDOWPOSCHANGED))self->controller_->refresh();
        if(m==WM_NCDESTROY){RemoveWindowSubclass(h,procedure,id);self->hwnd_=nullptr;}
        return DefSubclassProc(h,m,w,l);
    }
public:
    LabNativeSurface(QQuickView* view,LabController* controller):QObject(view),hwnd_(reinterpret_cast<HWND>(view->winId())),controller_(controller){
#ifdef GLASS_LAB_CHECK
        legacy_=qEnvironmentVariable("PANDACLIP_LAB_NATIVE_FRAME")=="legacy";
#endif
        if(!legacy_){const COLORREF none=0xfffffffe;DwmSetWindowAttribute(hwnd_,34,&none,sizeof(none));}
        SetWindowSubclass(hwnd_,procedure,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this));
        QTimer::singleShot(900,this,[this,view]{
            RECT client{},outer{};GetClientRect(hwnd_,&client);GetWindowRect(hwnd_,&outer);
            unsigned children=0;EnumChildWindows(hwnd_,[](HWND,LPARAM p)->BOOL{++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&children));
            const COLORREF border=0xfffffffe;const auto borderResult=DwmSetWindowAttribute(hwnd_,34,&border,sizeof(border));
            logLine(L"Glass Lab DWM border suppression request: HRESULT=0x%08X; visible edge must be checked externally.",unsigned(borderResult));
            logLine(L"Glass Lab surface HWND=%p legacyFrame=%d children=%u style=0x%llX exStyle=0x%llX client=%ldx%ld outer=%ldx%ld Qt=%dx%d DPR=%.2f alpha=%d clearAlpha=%d opacity=%.2f NCpaint=%u erase=%u; Qt owns client, no child presentation HWND.",hwnd_,legacy_,children,static_cast<unsigned long long>(GetWindowLongPtrW(hwnd_,GWL_STYLE)),static_cast<unsigned long long>(GetWindowLongPtrW(hwnd_,GWL_EXSTYLE)),client.right,client.bottom,outer.right-outer.left,outer.bottom-outer.top,view->width(),view->height(),view->devicePixelRatio(),view->format().alphaBufferSize(),view->color().alpha(),view->opacity(),ncPaints_,erases_);
        });
    }
    ~LabNativeSurface(){if(hwnd_)RemoveWindowSubclass(hwnd_,procedure,reinterpret_cast<UINT_PTR>(this));}
};
