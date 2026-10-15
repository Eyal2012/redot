#include <QApplication>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QImage>
#include "desktop_backdrop.h"

int main(int argc,char** argv) {
    QApplication app(argc,argv);QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    const auto root=qEnvironmentVariable("PANDACLIP_QT_TEST_ROOT");if(root.isEmpty())return 2;
    QWidget window;DesktopBackdrop::prepare(&window);window.resize(620,340);window.setWindowTitle("PandaClip owned composition preflight");
    DesktopBackdrop backdrop(&window);
    auto surface=new QQuickWidget(&window);auto format=surface->format();format.setAlphaBufferSize(8);surface->setFormat(format);surface->setClearColor(Qt::transparent);
    surface->setResizeMode(QQuickWidget::SizeRootObjectToView);surface->setGeometry(window.rect());
    // Representative surface, not an internal wallpaper or a scene snapshot.
    surface->setSource(QUrl("qrc:/PandaClip/BackdropProbe.qml"));surface->lower();
    auto label=new QLabel("Desktop Acrylic composition preflight",&window);label->setGeometry(38,55,520,38);label->setStyleSheet("color:white;background:transparent;font:20px 'Segoe UI';");
    auto button=new QPushButton("Separate, sharp foreground action",&window);button->setGeometry(58,190,410,48);button->setStyleSheet("color:white;background:transparent;border:1px solid #9990aa;border-radius:12px;font:14px 'Segoe UI';");
    window.show();backdrop.update(true);
    QTimer::singleShot(1500,&window,[&] {
        const auto image=surface->grabFramebuffer();int failures{};
        QFile report(root+"/composition-preflight.txt");report.open(QIODevice::WriteOnly);
        auto check=[&](bool ok,const QString& name){report.write((QString(ok?"PASS ":"FAIL ")+name+"\n").toUtf8());if(!ok)++failures;};
        check(surface->status()==QQuickWidget::Ready&&!image.isNull(),"production Qt/D3D11 surface initialized");
        check(image.hasAlphaChannel(),"GPU target has alpha channel");
        if(!image.isNull()) {
            check(qAlpha(image.pixel(8,8))==0,"unpainted root is transparent rather than opaque black");
            const auto panel=image.pixel(100,130);
            check(qAlpha(panel)>0&&qAlpha(panel)<255&&qBlue(panel)>0,"representative panel transmits through alpha without a black fill");
            image.save(root+"/composition-gpu-alpha.png");window.grab().save(root+"/composition-widgets.png");
        }
        check(button->isVisible()&&button->isEnabled(),"sharp foreground button remains interactive");
        report.write(window.property("desktopBackdropStatus").toString().toUtf8()+"\n");
        report.write("COMPOSITOR TRANSMISSION UNVERIFIED: inactive desktop screenshots omit DWM output. Manual controlled-window color change required.\n");
        QFile result(root+"/result.txt");result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures));app.exit(failures?1:0);
    });
    return app.exec();
}
