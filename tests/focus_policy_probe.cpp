// Native/Qt focus-policy measurement on an inactive, owned desktop only.
// This is not a physical two-monitor visual test.
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QLabel>
#include "desktop_backdrop.h"

int main(int argc,char** argv){
    QApplication app(argc,argv);
    const auto root=qEnvironmentVariable("PANDACLIP_QT_TEST_ROOT");
    wchar_t desktop[256]{};DWORD bytes{};
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&bytes);
    if(root.isEmpty()||wcsncmp(desktop,L"PandaClipQtQuiet_",17)!=0)return 2;
    QWidget window,other;DesktopBackdrop::prepare(&window);window.setWindowTitle("Owned backdrop policy measurement");
    window.resize(800,500);other.resize(200,100);other.setWindowTitle("Owned second activation target");
    window.setAttribute(Qt::WA_ShowWithoutActivating);other.setAttribute(Qt::WA_ShowWithoutActivating);
    DesktopBackdrop backdrop(&window);window.show();other.show();backdrop.update(true);
    const auto hwnd=reinterpret_cast<HWND>(window.winId());
    const auto otherHwnd=reinterpret_cast<HWND>(other.winId());
    QFile report(root+"/focus-policy.txt");if(!report.open(QIODevice::WriteOnly))return 3;
    auto write=[&](const QString& line){report.write((line+"\n").toUtf8());report.flush();};
    int phase{},failures{},activationChanges{};quint64 initial{},afterFocus{};
    bool material{};
    QTimer timer;timer.setInterval(220);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        if(phase==0){initial=window.property("backdropNativeApplies").toULongLong();material=window.property("desktopBackdropMaterial").toBool();}
        if(phase<12){
            // SetActiveWindow affects only our thread's windows on the private
            // desktop. Never SetForegroundWindow or SwitchDesktop.
            const auto target=phase%2?hwnd:otherHwnd;
            SetActiveWindow(target);activationChanges+=GetActiveWindow()==target;
            SendMessageW(hwnd,WM_ACTIVATEAPP,phase%2,GetCurrentThreadId());
            QEvent event(phase%2?QEvent::WindowActivate:QEvent::WindowDeactivate);
            QApplication::sendEvent(&window,&event);
        }else if(phase==12){
            afterFocus=window.property("backdropNativeApplies").toULongLong();
            write(QString("initialNativeApplies=%1 after12FocusChanges=%2 threadActivationChecks=%3").arg(initial).arg(afterFocus).arg(activationChanges));
            if(window.property("desktopBackdropMaterial").toBool()!=material||activationChanges!=12)++failures;
#ifndef REDOT_BASELINE_FOCUS_POLICY
            if(initial==0||afterFocus!=initial)++failures;
#endif
            SendMessageW(hwnd,WM_SETTINGCHANGE,0,0);
        }else if(phase==13){
            const auto afterPolicy=window.property("backdropNativeApplies").toULongLong();
            write(QString("afterUnchangedSettingsNotification=%1").arg(afterPolicy));
#ifndef REDOT_BASELINE_FOCUS_POLICY
            if(afterPolicy!=afterFocus)++failures;
#endif
            backdrop.update(false);
            const auto disabled=window.property("backdropNativeApplies").toULongLong();
            backdrop.update(false);backdrop.refreshPolicy();
            if(window.property("desktopBackdropMaterial").toBool())++failures;
#ifndef REDOT_BASELINE_FOCUS_POLICY
            if(disabled!=window.property("backdropNativeApplies").toULongLong())++failures;
#endif
            backdrop.update(true);
            write(QString("explicitDisableNativeApplies=%1 reenableNativeApplies=%2").arg(disabled).arg(window.property("backdropNativeApplies").toULongLong()));
            write(QString("RESULT failures=%1; no physical display focus, no perceptual blur claim").arg(failures));
            QFile result(root+"/result.txt");if(result.open(QIODevice::WriteOnly))result.write(QByteArray::number(failures));
            timer.stop();app.exit(failures?1:0);
        }
        ++phase;
    });timer.start();return app.exec();
}
