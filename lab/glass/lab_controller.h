#pragma once
#include <QApplication>
#include <QQuickView>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDynamicPropertyChangeEvent>
#include <QPointer>
#include <thread>
#include "desktop_backdrop.h"

class LabController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap preset READ preset CONSTANT)
    Q_PROPERTY(bool acrylic READ acrylic NOTIFY policyChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY policyChanged)
    Q_PROPERTY(bool motionAllowed READ motionAllowed NOTIFY policyChanged)
    Q_PROPERTY(double pixelRatio READ pixelRatio NOTIFY policyChanged)
    Q_PROPERTY(QString policy READ policy NOTIFY policyChanged)
    Q_PROPERTY(QString saveStatus READ saveStatus NOTIFY saveStatusChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY saveStatusChanged)
    QQuickView* view_;
    QTimer policyTimer_;
    QString path_,status_;
    QVariantMap preset_;
    std::jthread writer_;
    bool saving_{};
    bool requested_{true};
    bool energySaver_{};
    bool active_{}, transparency_{true}, composition_{}, contrast_{}, configured_{}, motionAllowed_{true};
    HRESULT requestResult_{E_PENDING}, supportResult_{E_PENDING}, coverageResult_{E_PENDING};
    QString reason_, details_;
    void updateEffective(){
        SYSTEM_POWER_STATUS power{};energySaver_=GetSystemPowerStatus(&power)&&power.SystemStatusFlag==1;
        const auto hwnd=reinterpret_cast<HWND>(view_->winId());
        active_=GetForegroundWindow()==hwnd;
        DWORD enabled=1,bytes=sizeof(enabled);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&enabled,&bytes);
        transparency_=enabled!=0;
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);contrast_=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);motionAllowed_=animations&&!contrast_;
        BOOL composition{};DwmIsCompositionEnabled(&composition);composition_=composition!=FALSE;
        int current{};supportResult_=DwmGetWindowAttribute(hwnd,38,&current,sizeof(current));
        const bool alpha=view_->format().alphaBufferSize()>0&&view_->color().alpha()==0&&view_->opacity()==1;
        reason_=!requested_?"forced solid preview":contrast_?"Windows high contrast":!transparency_?"Windows Transparency effects off":!composition_?"DWM composition unavailable":energySaver_?"Windows Energy Saver":GetSystemMetrics(SM_REMOTESESSION)?"Remote Desktop session":!active_?"inactive window":FAILED(supportResult_)?"Desktop Acrylic unsupported":!alpha?"Qt alpha surface unavailable":"";
#ifdef GLASS_LAB_CHECK
        // An inactive render probe isolates native paint/alpha defects. It is
        // never evidence of active-desktop transmission or a shipping override.
        if(reason_=="inactive window"&&qEnvironmentVariableIsSet("PANDACLIP_LAB_RENDER_PROBE"))reason_.clear();
#endif
        const int kind=reason_.isEmpty()?3:1;
        requestResult_=DwmSetWindowAttribute(hwnd,38,&kind,sizeof(kind));
        const COLORREF noBorder=0xfffffffe;DwmSetWindowAttribute(hwnd,34,&noBorder,sizeof(noBorder));
        configured_=kind==3&&SUCCEEDED(requestResult_);
        const MARGINS margins=configured_?MARGINS{-1,-1,-1,-1}:MARGINS{0,0,0,0};
        coverageResult_=DwmExtendFrameIntoClientArea(hwnd,&margins);
        if(FAILED(coverageResult_)){configured_=false;reason_="DWM backdrop coverage failed";}
        if(kind==3&&FAILED(requestResult_))reason_="Desktop Acrylic request failed";
        view_->setProperty("desktopBackdropAccepted",configured_);
        details_=QString("Windows transparency: %1 | Window: %2 | Forced solid: %3\nDWM composition: %4 | Backdrop support: %5 | Request: 0x%6 | Coverage: 0x%7\nQt: alpha %8, clear alpha %9, window opacity %10. Visible desktop transmission: unverified.")
            .arg(transparency_?"on":"off",active_?"active":"inactive",requested_?"no":"yes",composition_?"on":"off",SUCCEEDED(supportResult_)?"available":"unavailable")
            .arg(quint32(requestResult_),8,16,QChar('0')).arg(quint32(coverageResult_),8,16,QChar('0'))
            .arg(view_->format().alphaBufferSize()).arg(view_->color().alpha()).arg(view_->opacity());
        details_.replace("Request:",kind==3?"Acrylic request:":"None/solid request:");
        logLine(L"Glass Lab policy: %s; %s",policy().toStdWString().c_str(),details_.toStdWString().c_str());
        Q_EMIT policyChanged();
    }
public:
    LabController(QQuickView* view,QString folder):QObject(view),view_(view),path_(folder+"/approved-material-v1.json") {
        // Accessibility defaults must be available to Component.onCompleted,
        // before QML is loaded and before the native window is first shown.
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        contrast_=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;motionAllowed_=animations&&!contrast_;
        preset_={{"tint",.025},{"surfaceOpacity",.06},{"highlight",.14},{"radius",18.0},{"refraction",3.0},{"localBlur",.15},{"grain",0.0},{"backdropShade",.025},{"materialPreset","Clear"},{"solidPreview",false}};
        QFile file(path_);
        if(file.open(QIODevice::ReadOnly)){
            const auto object=QJsonDocument::fromJson(file.read(8192)).object();
            const QVariantMap bounds{{"tint",1.0},{"surfaceOpacity",.65},{"highlight",1.0},{"radius",32.0},{"refraction",8.0},{"localBlur",4.0},{"grain",1.0},{"backdropShade",.35},{"desktopBlur",24.0},{"desktopRefraction",16.0},{"desktopFill",.6},{"backgroundMode",2.0}};
            for(auto it=bounds.begin();it!=bounds.end();++it)if(object.contains(it.key())&&object[it.key()].isDouble())
                preset_[it.key()]=std::clamp(object[it.key()].toDouble(),it.key()=="radius"?8.0:0.0,it.value().toDouble());
            const auto name=object["materialPreset"].toString();
            if(name=="Clear"||name=="Soft Frost"||name=="Solid")preset_["materialPreset"]=name;
            if(object["solidPreview"].isBool())preset_["solidPreview"]=object["solidPreview"].toBool();
            if(object["reduceTransparency"].isBool())preset_["reduceTransparency"]=object["reduceTransparency"].toBool();
            status_="Loaded prototype preset";
        }else status_="Preset has not been saved";
        view_->installEventFilter(this);
        policyTimer_.setSingleShot(true);policyTimer_.setInterval(60);
        QObject::connect(&policyTimer_,&QTimer::timeout,this,[this]{updateEffective();});
        QObject::connect(view_,&QWindow::activeChanged,this,[this]{refresh();});
        QObject::connect(view_,&QWindow::screenChanged,this,[this]{refresh();});
    }
    ~LabController()override{if(writer_.joinable())writer_.join();}
    QVariantMap preset()const{return preset_;}
    bool acrylic()const{return configured_;}
    bool highContrast()const{return contrast_;}
    bool motionAllowed()const{return motionAllowed_;}
    double pixelRatio()const{return view_->effectiveDevicePixelRatio();}
    QString policy()const{
        return configured_?"Acrylic configured · visible transmission unverified":"Solid fallback: "+reason_;
    }
    Q_PROPERTY(QString policyDetails READ policyDetails NOTIFY policyChanged)
    QString policyDetails()const{return details_;}
    QString saveStatus()const{return status_;}
    bool saving()const{return saving_;}
    Q_INVOKABLE void solid(bool enabled){requested_=!enabled;updateEffective();}
    Q_INVOKABLE void refresh(){if(!policyTimer_.isActive())policyTimer_.start();}
    Q_INVOKABLE void openWindowsSettings(){QDesktopServices::openUrl(QUrl("ms-settings:easeofaccess-visualeffects"));}
    Q_INVOKABLE void minimize(){view_->showMinimized();}
    Q_INVOKABLE void maximize(){view_->visibility()==QWindow::Maximized?view_->showNormal():view_->showMaximized();}
    Q_INVOKABLE void close(){view_->close();}
    Q_INVOKABLE void move(){view_->startSystemMove();}
    Q_INVOKABLE void save(const QVariantMap& values){
        if(saving_)return;
        if(writer_.joinable())writer_.join(); // prior completion has already been delivered
        saving_=true;status_="Saving prototype preset…";Q_EMIT saveStatusChanged();
        const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(values)).toJson();const auto path=path_;
        writer_=std::jthread([this,bytes,path]{
            QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);
            const bool ok=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
            const QString result=ok?"Saved prototype-only preset":"Preset save failed: "+file.errorString();
            QMetaObject::invokeMethod(this,[this,result]{saving_=false;status_=result;Q_EMIT saveStatusChanged();},Qt::QueuedConnection);
        });
    }
    bool eventFilter(QObject*,QEvent* e)override{
        if(e->type()==QEvent::Show||e->type()==QEvent::Expose||e->type()==QEvent::Resize)refresh();
        return false;
    }
Q_SIGNALS:
    void policyChanged();
    void saveStatusChanged();
};
