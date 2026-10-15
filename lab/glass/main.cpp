#include <QApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickItem>
#include <QSurfaceFormat>
#include <QSGRendererInterface>
#include <QQuickStyle>
#include <windowsx.h>
#include "lab_controller.h"
#include "window_chrome.h"
#include "native_surface.h"
#include "desktop_exclusion.h"
#include "desktop_texture.h"
static LRESULT CALLBACK passiveGuard(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATEANDEAT;
    if(message==WM_WINDOWPOSCHANGING)reinterpret_cast<WINDOWPOS*>(l)->flags|=SWP_NOACTIVATE;
    if(message==WM_ENABLE&&w)EnableWindow(window,FALSE);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,passiveGuard,id);
    return DefSubclassProc(window,message,w,l);
}
#ifdef GLASS_LAB_CHECK
#include "checks.inl"
#endif
#ifdef GLASS_LAB_DESKTOP_CHECK
#include "desktop_checks.inl"
#endif

int main(int argc,char** argv){
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QApplication app(argc,argv);
    QQuickStyle::setStyle("Basic");
    app.setApplicationName("PandaClip Glass Lab");app.setOrganizationName("ReDotGlassLab");
    qmlRegisterType<DesktopTexture>("GlassLab.Experimental",1,0,"DesktopTexture");
    const auto folder=qEnvironmentVariable("PANDACLIP_GLASS_LAB_DATA",QCoreApplication::applicationDirPath()+"/GlassLabData");
    SetEnvironmentVariableW(L"LOCALAPPDATA",folder.toStdWString().c_str()); // child-only diagnostic location, never the main profile
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){logLine(L"Glass Lab Qt: %s",message.toStdWString().c_str());fprintf(stderr,"%s\n",message.toUtf8().constData());});
    QQuickView view;
    view.setFlags(Qt::Window|Qt::FramelessWindowHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);
    const bool passive=app.arguments().contains("--passive");
    if(passive)view.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
    QSurfaceFormat format=view.format();format.setAlphaBufferSize(8);view.setFormat(format);view.setColor(Qt::transparent);
    view.setTitle("PandaClip Glass Lab");view.setResizeMode(QQuickView::SizeRootObjectToView);view.setMinimumSize({800,650});view.resize(1040,720);
    LabController controller(&view,folder);
    DesktopExclusion exclusion;
    view.rootContext()->setContextProperty("Exclusion",&exclusion);
    view.rootContext()->setContextProperty("Lab",&controller);
    view.setSource(QUrl("qrc:/GlassLab/Main.qml"));
    if(view.status()!=QQuickView::Ready){for(const auto& error:view.errors())logLine(L"Glass Lab QML load failed: %s",error.toString().toStdWString().c_str());return 2;}
    const auto hwnd=reinterpret_cast<HWND>(view.winId());
    exclusion.refresh();
    if(passive){SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);EnableWindow(hwnd,FALSE);}
    QObject::connect(&view,&QWindow::activeChanged,&view,[&view,passive]{logLine(L"Glass Lab active=%d passive=%d.",view.isActive(),passive);});
    auto root=view.rootObject();
#if defined(GLASS_LAB_DESKTOP_CHECK) || defined(GLASS_LAB_CHECK)
    root->setProperty("backgroundMode",2); // create owned backing HWND before capture/evidence
#endif
    const QString recipe=app.arguments().contains("--solid")?"Solid":app.arguments().contains("--soft-frost")?"Soft Frost":app.arguments().contains("--clear")?"Clear":"";
    if(!recipe.isEmpty())QMetaObject::invokeMethod(root,"applyMaterialPreset",Q_ARG(QVariant,QVariant(recipe)));
    if(app.arguments().contains("--animate-sample"))root->setProperty("sampleMoving",true);
    if(app.arguments().contains("--lens-demo")){root->setProperty("refraction",8.0);root->setProperty("localBlur",0.0);}
    auto chrome=new WindowChrome(&view,&view,[&view]{return QRect(12,8,std::max(0,view.width()-168),40);},[&view]{return QRect(view.width()-96,8,38,32);});
    Q_UNUSED(chrome);
    new LabNativeSurface(&view,&controller);
    if(passive){SetWindowSubclass(hwnd,passiveGuard,1,0);EnableWindow(hwnd,FALSE);}
    controller.solid(root->property("solidPreview").toBool()||root->property("backgroundMode").toInt()!=1||root->property("reduceTransparency").toBool());
    if(app.arguments().contains("--popover"))QTimer::singleShot(600,&view,[root]{if(auto popup=root->findChild<QObject*>("demoPopup"))QMetaObject::invokeMethod(popup,"open");});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&view,[&]{view.setSource(QUrl());});
    view.show();
#ifdef GLASS_LAB_DESKTOP_CHECK
    runDesktopChecks(view,controller,root,folder);
#endif
#ifdef GLASS_LAB_CHECK
    if(qEnvironmentVariableIsSet("PANDACLIP_LAB_RENDER_PROBE")){
        root->setProperty("sampleMoving",false);
        QTimer::singleShot(15000,&view,&QWindow::close);
    }else runLabChecks(view,controller,root,folder);
#else
    Q_UNUSED(root);
#endif
    return app.exec();
}
