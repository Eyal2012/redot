#include "scene_editor.h"
#include "application_picker.h"
#include <QContextMenuEvent>
#include "design_system.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QSlider>
#include <QMenu>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QLineEdit>
#include <QThread>
#include <QScrollBar>
#include <windows.h>
#include <fstream>
#include <cmath>
using namespace Scenes;

namespace {
HWND fixtureSecond{};
LRESULT CALLBACK fixtureProc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_APP+901){if(fixtureSecond){DestroyWindow(fixtureSecond);fixtureSecond=nullptr;}return 0;}
    if(message==WM_APP+902){if(!fixtureSecond)fixtureSecond=CreateWindowExW(0,L"PandaClipOwnedPickerFixture",L"Owned application B",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,400,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);return 0;}
    if(message==WM_CLOSE){DestroyWindow(hwnd);PostQuitMessage(0);return 0;}return DefWindowProcW(hwnd,message,w,l);
}
int fixture(){
    WNDCLASSW wc{};wc.lpfnWndProc=fixtureProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PandaClipOwnedPickerFixture";wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&wc);
    auto first=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned application A",WS_OVERLAPPEDWINDOW,0,0,400,300,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(first,SW_SHOWNOACTIVATE);
    fixtureSecond=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned application B",WS_OVERLAPPEDWINDOW,420,0,400,300,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(fixtureSecond,SW_SHOWNOACTIVATE);
    CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned irrelevant tool",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
    CreateWindowExW(0,wc.lpszClassName,L"Owned hidden application",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&std::wstring(argv[1])==L"--fixture")return fixture();
    if(argc==3&&std::wstring(argv[1])==L"--probe"){
        auto before=GetForegroundWindow();auto result=discoverApplications(reinterpret_cast<quintptr>(GetThreadDesktop(GetCurrentThreadId())),GetCurrentProcessId());
        std::ofstream report(argv[2]);int gd=0;for(auto& w:result.windows)if(w.executable.endsWith("GeometryDash.exe",Qt::CaseInsensitive)){++gd;report<<"Geometry Dash target available; stable executable/title/class captured; icon="<<!w.icon.isNull()<<"\n";}
        report<<"visible selectable windows="<<result.windows.size()<<" GeometryDash="<<gd<<" error="<<result.error.toStdString()<<" foreground_unchanged="<<(before==GetForegroundWindow())<<"\n";return result.error.isEmpty()&&before==GetForegroundWindow()?0:1;
    }
    if(argc!=2)return 2;
    const auto root=QString::fromWCharArray(argv[1]);QDir().mkpath(root);
    // All widgets/input stay on an owned desktop which is never switched to.
    auto desktop=CreateDesktopW((L"PandaClipScenes_"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 3;
    const auto foreground=GetForegroundWindow();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    int appargc=1;QByteArray appname="ReDotScenesTest";char* appargv[]={appname.data(),nullptr};QApplication app(appargc,appargv);app.setStyleSheet(DesignSystem::style());
    std::ofstream report((root+"/results.txt").toStdWString());int failures=0,checks=0;
    auto check=[&](bool ok,const char* text){++checks;report<<(ok?"PASS ":"FAIL ")<<text<<"\n";report.flush();if(!ok)++failures;};
    auto pump=[](int ms){QElapsedTimer t;t.start();do{QCoreApplication::processEvents();QThread::msleep(2);}while(t.elapsed()<ms);};
    auto wait=[&](auto condition){QElapsedTimer t;t.start();while(!condition()&&t.elapsed()<5000)pump(5);return condition();};
    auto bytes=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray{};return f.readAll();};
    auto path=root+"/scenes-v1.json";Snapshot expected;
    {
        Model model(path);check(wait([&]{return model.ready();}),"asynchronous initial load completes");check(model.data().scenes.size()==1,"default scene exists");check(!model.remove(model.data().selected),"last scene cannot be removed");
        Snapshot bootstrap;QString bootstrapError;check(decode(bytes(path),bootstrap,bootstrapError)&&bootstrap.selected==model.data().selected,"initial default scene identity is persisted before any edit");
        const auto original=model.data().selected;check(model.rename(original,"Gameplay"),"rename scene");
        auto imagePath=root+"/owned-image.png";QImage image(240,120,QImage::Format_RGB32);image.fill(QColor("#4180AB"));check(image.save(imagePath),"owned image fixture saved");
        auto def=model.addDefinition(Kind::Text,"Title",{{"text","PandaClip scene editor"},{"fontSize",64}}, {900,160});auto visualId=model.addVisual(def);
        auto imageDef=model.addDefinition(Kind::Image,"Image",{{"path",imagePath}},{240,120});auto imageId=model.addVisual(imageDef);
        auto cameraDef=model.addDefinition(Kind::Camera,"Webcam (Stage 2)");auto camera=model.addVisual(cameraDef);
        auto mic=model.addDefinition(Kind::Microphone,"Pinned microphone",{{"deviceId","endpoint-123"},{"deviceName","Owned input"}});auto audioId=model.addAudio(mic);
        auto desktopDef=model.addDefinition(Kind::DesktopAudio,"Explicit desktop",{{"deviceId","output-123"}});model.addAudio(desktopDef);
        auto application=model.addDefinition(Kind::ApplicationAudio,"Game audio",{{"target","owned-game.exe"}});model.addAudio(application);
        check(!model.addAudio(def).size()&&!model.addVisual(mic).size(),"visual/audio kind separation enforced");check(model.addAudio(mic).isEmpty(),"duplicate audio assignment rejected");
        auto audio=model.current()->audio[0];audio.gain=.73;audio.muted=true;audio.syncMs=12.5;check(model.updateAudio(audio),"audio gain mute and sync persisted in model");
        auto copy=model.duplicate(original);check(!copy.isEmpty()&&copy!=original,"duplicate gets a distinct scene ID");
        check(model.current()->visuals[0].sourceId==def&&model.current()->visuals[0].id!=visualId,"duplicate shares definition and creates independent instance IDs");
        check(model.current()->audio[0].id!=audioId&&model.current()->audio[0].sourceId==mic&&model.data().definitions.size()==6,"audio duplicate shares endpoint definition without new captures");
        check(model.current()->audio[0].gain==.73&&model.current()->audio[0].muted&&model.current()->audio[0].syncMs==12.5,"duplicate retains audio configuration");
        auto third=model.create("Scene 3");check(model.reorder(third,0)&&model.data().scenes[0].id==third,"scene reorder");check(model.remove(third)&&model.select(original),"delete and select scenes");
        check(model.moveVisual(visualId,2)&&model.current()->visuals.last().id==visualId,"back-to-front layer ordering");check(model.setResolution(1280,720)&&!model.setResolution(1000,1000),"only valid 16:9 resolutions accepted");model.setResolution(1920,1080);
        auto v=*model.visual(visualId);v.x=200;v.y=200;v.width=800;v.height=240;v.scale=.8;v.rotation=15;v.cropLeft=10;v.cropTop=4;v.cropRight=12;v.cropBottom=8;check(model.updateVisual(v),"all visual transforms supported");
        auto invalid=v;invalid.width=NAN;check(!model.updateVisual(invalid),"nonfinite transforms rejected");invalid=v;invalid.cropLeft=invalid.width;check(!model.updateVisual(invalid),"empty crop rejected");
        Snapshot decoded;QString error;check(decode(encode(model.snapshot()),decoded,error)&&encode(decoded)==encode(model.snapshot()),"lossless versioned model round-trip");
        auto broken=QJsonDocument::fromJson(encode(model.snapshot())).object();auto scenes=broken["scenes"].toArray();auto scene=scenes[0].toObject();auto visuals=scene["visuals"].toArray();auto instance=visuals[0].toObject();instance["source"]="missing";visuals[0]=instance;scene["visuals"]=visuals;scenes[0]=scene;broken["scenes"]=scenes;check(!decode(QJsonDocument(broken).toJson(),decoded,error),"dangling source references rejected");
        Page page(&model);page.resize(1180,750);page.show();pump(200);
        model.selectVisual(visualId);check(page.sourceList()->currentItem()&&page.sourceList()->currentItem()->data(Qt::UserRole).toString()==visualId,"canvas/model selection reaches source list");
        auto mouse=[&](QEvent::Type type,QPointF pos,Qt::MouseButton b,Qt::MouseButtons buttons){QMouseEvent e(type,pos,QPointF(page.canvas()->mapToGlobal(pos.toPoint())),b,buttons,Qt::ControlModifier);QApplication::sendEvent(page.canvas(),&e);};
        v=*model.visual(visualId);v.rotation=0;v.scale=1;v.x=300;v.y=240;model.updateVisual(v);
        auto point=page.canvas()->toWidget(v.transform().map(v.localRect().center()));auto destination=page.canvas()->toWidget(v.transform().map(v.localRect().center())+QPointF(120,45));
        mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,destination,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,destination,Qt::LeftButton,Qt::NoButton);
        check(std::abs(model.visual(visualId)->x-(v.x+120))<.01&&std::abs(model.visual(visualId)->y-(v.y+45))<.01,"drag modifies model in scene coordinates");
        v=*model.visual(visualId);auto corner=page.canvas()->corners(v)[2];auto end=page.canvas()->toWidget(v.transform().map(QPointF(v.width*1.25,v.height*1.25)));
        mouse(QEvent::MouseButtonPress,corner,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        check(std::abs(model.visual(visualId)->width-v.width*1.25)<.1&&std::abs(model.visual(visualId)->height-v.height*1.25)<.1,"corner resize preserves aspect and modifies model");
        v=*model.visual(visualId);v.rotation=37;v.scale=.75;model.updateVisual(v);auto opposite=page.canvas()->corners(v)[2];corner=page.canvas()->corners(v)[0];end=page.canvas()->toWidget(v.transform().map(QPointF(-100,-24)));
        mouse(QEvent::MouseButtonPress,corner,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        check(QLineF(opposite,page.canvas()->corners(*model.visual(visualId))[2]).length()<.1,"rotated corner resize anchors opposite corner");
        v=*model.visual(visualId);v.locked=true;model.updateVisual(v);auto locked=model.snapshot();point=page.canvas()->corners(v).boundingRect().center();mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,point+QPointF(30,20),Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
        check(encode(locked)==encode(model.snapshot())&&!model.fit(visualId)&&!model.center(visualId),"locked source cannot drag fit or center");
        v.locked=false;v.visible=false;model.updateVisual(v);check(page.canvas()->hit(page.canvas()->toWidget(v.transform().map(v.localRect().center())))!=visualId,"hidden source excluded from hit testing");
        v.visible=true;model.updateVisual(v);model.selectVisual(visualId);check(model.fit(visualId)&&model.center(visualId),"fit and center supported");
        check(model.visual(visualId)->rotation==0&&model.visual(visualId)->scale==1,"fit resets rotation and scale");
        QKeyEvent key(QEvent::KeyPress,Qt::Key_Right,Qt::ShiftModifier);double old=model.visual(visualId)->x;QApplication::sendEvent(page.canvas(),&key);check(model.visual(visualId)->x==old+10,"keyboard source movement uses canvas pixels");
        page.sourceList()->setCurrentRow(1);check(page.sourceList()->currentItem()->data(Qt::UserRole).toString()==model.selection(),"source list selection reaches canvas model");
        auto slider=page.findChild<QSlider*>("sceneAudioGain_"+audioId);auto mute=page.findChild<QPushButton*>("sceneAudioMute_"+audioId);check(slider&&mute,"audio configuration controls exist");if(slider)slider->setValue(125);if(mute)mute->setChecked(false);check(model.current()->audio[0].gain==1.25&&!model.current()->audio[0].muted,"audio controls update real assignment");
        for(auto size:{QSize(750,540),QSize(1300,800),QSize(950,620)}){page.resize(size);pump(50);auto r=page.canvas()->viewport();check(std::abs(r.width()/r.height()-16/9.)<.001,"16:9 canvas remains correct after resize");auto p=QPointF(678.25,345.5);check(QLineF(p,page.canvas()->toCanvas(page.canvas()->toWidget(p))).length()<.001,"preview to canvas coordinate round-trip at resized window");check(page.canvas()->width()>=180&&page.findChild<QPushButton*>("sceneAdvancedTransform")->isVisible()&&!page.findChild<QDoubleSpinBox*>(),"small page retains canvas; numeric transforms are not permanently visible");auto tools=page.findChild<QScrollArea*>("sceneEditorToolsScroll");tools->ensureWidgetVisible(page.findChild<QPushButton*>("sceneAdvancedTransform"));check(page.findChild<QPushButton*>("sceneAdvancedTransform")->height()>=24,"source edit actions retain legible height in scrollable small panel");}
        page.findChild<QPushButton*>("sceneAddSource")->click();pump(30);auto menu=page.findChild<QMenu*>();check(menu&&menu->actions().size()==7,"Add Source menu has six visual types plus shared reuse");
        if(menu){auto action=menu->actions()[5];action->trigger();menu->hide();pump(80);auto dialog=page.findChild<QDialog*>("sceneSourceDialog");check(dialog&&dialog->isVisible(),"real source properties opens from Add Source menu");if(dialog){dialog->findChild<QLineEdit*>("sceneDefinitionName")->setText("Added through UI");dialog->findChild<QTextEdit*>()->setPlainText("Working text source");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();pump(30);check(model.data().definitions.size()==7&&model.visual(model.selection())&&model.definition(model.visual(model.selection())->sourceId)->name=="Added through UI","source dialog creates persisted definition and scene instance");}}
        model.selectVisual(imageId);QKeyEvent deletion(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(page.canvas(),&deletion);pump(20);auto confirm=page.findChild<QMessageBox*>();check(confirm&&model.visual(imageId),"Delete requests confirmation before removing source");if(confirm){confirm->button(QMessageBox::Yes)->click();pump(30);check(!model.visual(imageId)&&model.definition(imageDef),"confirmed deletion removes instance and preserves shared definition");}

        model.selectVisual(visualId);page.findChild<QPushButton*>("sceneAdvancedTransform")->click();pump(50);auto advanced=page.findChild<QDialog*>("sceneAdvancedDialog");check(advanced&&advanced->isVisible(),"precise transforms open only in Advanced dialog");
        if(advanced){auto scroll=advanced->findChild<QScrollArea*>("sceneTransformScroll");scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());check(scroll->verticalScrollBar()->value()==scroll->verticalScrollBar()->maximum(),"advanced transform fields reachable through scroll viewport");advanced->findChild<QDoubleSpinBox*>("sceneTransformX")->setValue(177);advanced->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();check(model.visual(visualId)->x==177,"advanced numeric Apply updates autosaved model");advanced->grab().save(root+"/component-advanced.png");advanced->reject();pump(30);}
        auto editMouse=[&](QEvent::Type type,QPointF pos,Qt::MouseButton b,Qt::MouseButtons buttons,Qt::KeyboardModifiers mods=Qt::NoModifier){QMouseEvent event(type,pos,QPointF(page.canvas()->mapToGlobal(pos.toPoint())),b,buttons,mods);QApplication::sendEvent(page.canvas(),&event);};
        v=*model.visual(visualId);v.x=350;v.y=270;v.width=800;v.height=240;v.scale=1;v.rotation=0;v.cropLeft=v.cropRight=v.cropTop=v.cropBottom=0;model.updateVisual(v);model.selectVisual(visualId);
        auto rotate=page.canvas()->rotationHandle(v),pivot=page.canvas()->toWidget(v.transform().map(v.localRect().center()));auto vector=rotate-pivot;
        editMouse(QEvent::MouseButtonPress,rotate,Qt::LeftButton,Qt::LeftButton);destination=pivot+QPointF(-vector.y(),vector.x());editMouse(QEvent::MouseMove,destination,Qt::NoButton,Qt::LeftButton);editMouse(QEvent::MouseButtonRelease,destination,Qt::LeftButton,Qt::NoButton);
        auto rotated=*model.visual(visualId);check(std::abs(rotated.rotation-90)<.01&&QLineF(v.transform().map(v.localRect().center()),rotated.transform().map(rotated.localRect().center())).length()<.01,"rotation handle rotates smoothly about fixed center");
        rotate=page.canvas()->rotationHandle(rotated);pivot=page.canvas()->toWidget(rotated.transform().map(rotated.localRect().center()));vector=rotate-pivot;double angle=22*3.141592653589793/180;destination=pivot+QPointF(vector.x()*std::cos(angle)-vector.y()*std::sin(angle),vector.x()*std::sin(angle)+vector.y()*std::cos(angle));
        editMouse(QEvent::MouseButtonPress,rotate,Qt::LeftButton,Qt::LeftButton);editMouse(QEvent::MouseMove,destination,Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);editMouse(QEvent::MouseButtonRelease,destination,Qt::LeftButton,Qt::NoButton);check(std::abs(model.visual(visualId)->rotation-105)<.01,"Shift rotation snaps to fifteen degrees");
        model.updateVisual(v);auto points=page.canvas()->corners(v);auto freeCorner=points[2]+QPointF(60,7);
        editMouse(QEvent::MouseButtonPress,points[2],Qt::LeftButton,Qt::LeftButton);editMouse(QEvent::MouseMove,freeCorner,Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);editMouse(QEvent::MouseButtonRelease,freeCorner,Qt::LeftButton,Qt::NoButton);check(std::abs(model.visual(visualId)->width/model.visual(visualId)->height-v.width/v.height)>.1,"Shift allows free corner resizing");
        model.updateVisual(v);page.canvas()->setCropMode(true);auto topLeft=page.canvas()->corners(v)[0],cropPoint=page.canvas()->toWidget(v.transform().map(QPointF(75,30)));
        editMouse(QEvent::MouseButtonPress,topLeft,Qt::LeftButton,Qt::LeftButton);editMouse(QEvent::MouseMove,cropPoint,Qt::NoButton,Qt::LeftButton);editMouse(QEvent::MouseButtonRelease,cropPoint,Qt::LeftButton,Qt::NoButton);auto cropped=*model.visual(visualId);
        check(std::abs(cropped.cropLeft-75)<.01&&std::abs(cropped.cropTop-30)<.01&&cropped.width==800&&cropped.x==350,"crop handles change local margins without resizing or moving pixels");
        check(page.canvas()->hit(page.canvas()->toWidget(cropped.transform().map(QPointF(20,10))))!=visualId,"cropped-away pixels do not select invisible source area");QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(page.canvas(),&escape);check(!page.canvas()->cropMode()&&model.selection()==visualId&&!page.findChild<QPushButton*>("sceneCropMode")->isChecked(),"Escape exits crop and synchronizes button without clearing selection");
        page.canvas()->transformAction("Horizontal");check(std::abs(model.visual(visualId)->transform().mapRect(model.visual(visualId)->localRect().adjusted(75,30,0,0)).center().x()-960)<.01,"horizontal alignment centers visible bounds");auto x=model.visual(visualId)->x;page.canvas()->transformAction("Vertical");check(model.visual(visualId)->x==x,"vertical alignment preserves horizontal position");
        page.canvas()->transformAction("Stretch");check(model.visual(visualId)->width==1920&&model.visual(visualId)->height==1080&&model.visual(visualId)->cropLeft==0,"stretch fills canvas and resets crop");page.canvas()->transformAction("Fit");check(std::abs(model.visual(visualId)->width/model.visual(visualId)->height-900./160)<.001,"fit restores source aspect ratio after free stretch");page.canvas()->transformAction("Reset");check(model.visual(visualId)->width==900&&model.visual(visualId)->height==160&&model.visual(visualId)->rotation==0,"reset restores definition dimensions");
        model.updateVisual(v);auto center=page.canvas()->toWidget(v.transform().map(v.localRect().center()));auto almostEdge=page.canvas()->toWidget({v.width/2+2,v.y+v.height/2});
        editMouse(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton);editMouse(QEvent::MouseMove,almostEdge,Qt::NoButton,Qt::LeftButton);check(std::abs(model.visual(visualId)->x)<.01,"drag snaps to canvas edge in screen-independent tolerance");page.grab().save(root+"/component-guides.png");editMouse(QEvent::MouseButtonRelease,almostEdge,Qt::LeftButton,Qt::NoButton);
        model.updateVisual(v);editMouse(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton);editMouse(QEvent::MouseMove,almostEdge,Qt::NoButton,Qt::LeftButton,Qt::ControlModifier);editMouse(QEvent::MouseButtonRelease,almostEdge,Qt::LeftButton,Qt::NoButton);check(std::abs(model.visual(visualId)->x-2)<.01,"Ctrl bypasses snapping");
        model.updateVisual(v);QContextMenuEvent contextEvent(QContextMenuEvent::Mouse,center.toPoint(),page.canvas()->mapToGlobal(center.toPoint()));QApplication::sendEvent(page.canvas(),&contextEvent);pump(20);auto contextMenu=page.canvas()->findChild<QMenu*>("sceneCanvasMenu");check(contextMenu&&contextMenu->actions().size()==7,"canvas context menu exposes alignment crop and advanced transforms");if(contextMenu)contextMenu->hide();

        // Exercise the shipping native clipping path without global input or
        // replacing the production compositor with a screenshot renderer.
        {
            auto before=encode(model.snapshot());QWidget host;host.resize(910,525);Canvas native(&model,&host);native.resize(910,525);
            QRectF physical;native.bindPresentation([&](HWND,const QRectF& r,const QVector<QPolygonF>&,const QVector<QPointF>&){physical=r;return true;});host.show();native.show();pump(80);
            auto r=native.viewport();check(native.mask().contains(r.center().toPoint())&&!native.mask().contains(QPoint(1,1)),"native region keeps scene and removes editor-only black padding");
            HRGN region=CreateRectRgn(0,0,0,0);auto type=GetWindowRgn(reinterpret_cast<HWND>(native.winId()),region);auto scale=native.devicePixelRatioF();
            check(type!=ERROR&&PtInRegion(region,int(r.center().x()*scale),int(r.center().y()*scale))&&!PtInRegion(region,1,1),"deployed child HWND has the same clipped scene region at current scaling");DeleteObject(region);
            check(QLineF(physical.topLeft(),r.topLeft()*scale).length()<.01&&std::abs(physical.width()-r.width()*scale)<.01,"GPU viewport uses identical physical mapping to editor input");
            if(auto selected=model.visual(model.selection()))check(native.mask().contains(native.rotationHandle(*selected).toPoint()),"rotation handle outside the scene remains visible and clickable");
            for(auto size:{QSize(340,230),QSize(1600,900),QSize(910,525)}){host.resize(size);native.resize(size);pump(40);r=native.viewport();check(native.mask().contains(r.center().toPoint())&&std::abs(r.width()/r.height()-16/9.)<.001,"native clipping tracks normal small and large aspect-correct viewports");auto point=QPointF(960,540);check(QLineF(point,native.toCanvas(native.toWidget(point))).length()<.001,"clipped viewport keeps precise source coordinate mapping");}
            if(auto selected=model.visual(model.selection())){auto original=*selected;auto oversizedVisual=original;oversizedVisual.scale=100;oversizedVisual.rotation=37;model.updateVisual(oversizedVisual);pump(40);check(native.mask().boundingRect().width()<=native.width()&&native.mask().boundingRect().height()<=native.height(),"off-canvas rotated outlines build a bounded native mask");model.updateVisual(original);pump(40);}
            auto selected=model.selection();QMouseEvent blank(QEvent::MouseButtonPress,QPointF(1,1),QPointF(host.mapToGlobal(QPoint(1,1))),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&host,&blank);check(model.selection().isEmpty(),"exposed editor padding retains click-to-clear selection");model.selectVisual(selected);
            check(encode(model.snapshot())==before,"preview clipping and resizing never modify saved source transforms");native.hide();host.hide();
        }
        page.grab().save(root+"/component-editor.png"); // component evidence, not compositor-transmission proof
        model.select(copy);expected=model.snapshot();model.flush();check(wait([&]{return model.idle();}),"bounded background writer drains");check(!bytes(path).isEmpty(),"atomic scene file created");page.hide();
    }
    {
        Model model(path);check(wait([&]{return model.ready();}),"restart load completes");check(model.current()->name=="Gameplay copy"&&model.data().scenes.size()==2&&model.data().definitions.size()==7,"last selected scene order and definitions survive restart");check(model.current()->audio[0].gain==.73&&model.current()->audio[0].muted,"duplicate retains independent audio after restart");check(encode(model.snapshot())==encode(expected),"all edited transforms crops layers flags and audio restore exactly");
    }

    // Real Windows discovery on this isolated desktop. The helper process owns
    // multiple real top-level HWNDs, an invisible window and a tool window.
    PROCESS_INFORMATION child{};wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);wchar_t desktopName[256]{};DWORD needed{};GetUserObjectInformationW(desktop,UOI_NAME,desktopName,sizeof(desktopName),&needed);std::wstring childDesktop=L"WinSta0\\"+std::wstring(desktopName);
    auto launch=[&]{STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=childDesktop.data();auto command=L"\""+std::wstring(exe)+L"\" --fixture";return CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)!=FALSE;};
    auto stop=[&](quintptr hwnd){DWORD_PTR ignored{};SendMessageTimeoutW(reinterpret_cast<HWND>(hwnd),WM_CLOSE,0,0,SMTO_ABORTIFHUNG,500,&ignored);if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,5);WaitForSingleObject(child.hProcess,1000);}CloseHandle(child.hThread);CloseHandle(child.hProcess);child={};};
    check(launch(),"owned application fixture starts on never-switched desktop");
    {
        ApplicationPicker picker;picker.resize(560,350);picker.show();check(wait([&]{return !picker.busy()&&picker.result().windows.size()==2;}),"Windows discovers two real windows and excludes own tool hidden windows");
        auto list=picker.findChild<QListWidget*>("sceneApplicationList");auto search=picker.findChild<QLineEdit*>("sceneApplicationSearch");auto refresh=picker.findChild<QPushButton*>("sceneApplicationRefresh");
        search->setText("APPLICATION b");check(list->count()==1,"search filters real window titles");list->setCurrentRow(0);auto selected=picker.selected();check(selected&&selected->title=="Owned application B"&&!selected->icon.isNull()&&!selected->executable.isEmpty(),"selection includes application icon real executable and title");
        QJsonObject identity=selected?selected->identity():QJsonObject{};check(identity["windowClass"]=="PandaClipOwnedPickerFixture"&&!identity.contains("handle")&&!identity.contains("processId"),"stable target identity excludes transient HWND and PID");
        search->setText("ReDotScenesTest");check(list->count()==2,"search finds executable and multiple windows remain distinct");search->clear();
        quintptr first{};for(auto& w:picker.result().windows)if(w.title=="Owned application A")first=w.handle;
        DWORD_PTR ignored{};SendMessageTimeoutW(reinterpret_cast<HWND>(first),WM_APP+901,0,0,SMTO_ABORTIFHUNG,500,&ignored);refresh->click();check(wait([&]{return !picker.busy()&&picker.result().windows.size()==1;}),"Refresh removes closed real window");check(!picker.selected()&&picker.findChild<QLabel*>("sceneApplicationStatus")->text().contains("unavailable"),"closed selection retains identity and reports unavailable");if(selected)check(!applicationStillAvailable(*selected),"stale target validation rejects closed HWND");
        SendMessageTimeoutW(reinterpret_cast<HWND>(first),WM_APP+902,0,0,SMTO_ABORTIFHUNG,500,&ignored);check(wait([&]{return !picker.busy()&&picker.result().windows.size()==2&&picker.selected().has_value();}),"automatic refresh finds reopened window by stable identity");
        stop(first);check(launch(),"owned application process restarts");refresh->click();check(wait([&]{return !picker.busy()&&picker.selected().has_value()&&picker.selected()->processId==child.dwProcessId;}),"process restart reconnects unique executable title class without old PID");first=0;for(auto& w:picker.result().windows)if(w.title=="Owned application A"&&w.processId==child.dwProcessId)first=w.handle;
        Model model(root+"/picker-scene.json");wait([&]{return model.ready();});Page page(&model);page.resize(1100,730);page.show();pump(50);page.findChild<QPushButton*>("sceneAddSource")->click();pump(20);auto menu=page.findChild<QMenu*>();if(menu){menu->actions()[0]->trigger();menu->hide();}pump(100);auto dialog=page.findChild<QDialog*>("sceneSourceDialog");check(dialog&&dialog->findChild<ApplicationPicker*>()&&dialog->findChildren<QLineEdit*>().size()==2,"Game Capture opens application picker without manual path fields");
        if(dialog){auto applications=dialog->findChild<ApplicationPicker*>();check(wait([&]{return !applications->busy()&&applications->result().windows.size()==2;}),"source dialog discovers actual application windows asynchronously");auto rows=applications->findChild<QListWidget*>("sceneApplicationList");rows->setCurrentRow(0);auto chosen=applications->selected();auto save=dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            rows->setCurrentRow(1);SendMessageTimeoutW(reinterpret_cast<HWND>(first),WM_APP+901,0,0,SMTO_ABORTIFHUNG,500,&ignored);save->click();check(model.data().definitions.isEmpty()&&dialog->isVisible(),"Add Source rejects a window closed after selection without opening capture");SendMessageTimeoutW(reinterpret_cast<HWND>(first),WM_APP+902,0,0,SMTO_ABORTIFHUNG,500,&ignored);check(wait([&]{return !applications->busy()&&applications->result().windows.size()==2;}),"picker recovers after stale-selection race");rows->setCurrentRow(0);chosen=applications->selected();applications->grab().save(root+"/component-picker.png");check(save->text()=="Add Source","new game selection uses Add Source action");save->click();pump(30);check(model.data().definitions.size()==1&&model.current()->visuals.size()==1&&model.data().definitions[0].settings["executable"]==chosen->executable,"Add Source saves target and independent visual instance without capture");}
        // Window Capture and Application Audio use the same discovery component.
        for(int actionIndex:{1}){page.findChild<QPushButton*>("sceneAddSource")->click();pump(20);auto menus=page.findChildren<QMenu*>();QMenu* currentMenu=nullptr;for(auto m:menus)if(m->isVisible())currentMenu=m;if(currentMenu){currentMenu->actions()[actionIndex]->trigger();currentMenu->hide();}pump(80);auto dialogs=page.findChildren<QDialog*>("sceneSourceDialog");QDialog* currentDialog=nullptr;for(auto d:dialogs)if(d->isVisible())currentDialog=d;check(currentDialog&&currentDialog->findChild<ApplicationPicker*>(),"Window Capture reuses identical application picker");if(currentDialog)currentDialog->reject();}
        model.flush();wait([&]{return model.idle();});Snapshot saved;QString error;check(decode(bytes(root+"/picker-scene.json"),saved,error)&&saved.definitions[0].settings.contains("windowClass"),"target association survives JSON persistence");
        for(auto& w:picker.result().windows)if(w.title=="Owned application A")first=w.handle;stop(first);picker.hide();page.hide();
    }
    auto corruptPath=root+"/corrupt.json";const QByteArray original="{broken original data";{QFile f(corruptPath);f.open(QIODevice::WriteOnly);f.write(original);}
    {
        Model model(corruptPath);check(wait([&]{return model.ready();}),"corrupt config recovers asynchronously");check(bytes(corruptPath)==original&&!model.readOnly(),"loading preserves corrupt original without overwriting");model.create("Recovered");model.flush();check(wait([&]{return model.idle();}),"recovery write completes");auto backups=QDir(root).entryList({"corrupt.json.corrupt-*.json"},QDir::Files);check(backups.size()==1&&bytes(root+"/"+backups[0])==original,"corrupt original archived before replacement");Snapshot value;QString error;check(decode(bytes(corruptPath),value,error)&&value.scenes.size()==2,"replacement is valid scene configuration");
    }
    auto futurePath=root+"/future.json";Snapshot fresh;fresh.scenes.append(Scene{});auto json=QJsonDocument::fromJson(bytes(path)).object();json["version"]=2;auto future=QJsonDocument(json).toJson();{QFile f(futurePath);f.open(QIODevice::WriteOnly);f.write(future);}
    {Model model(futurePath);wait([&]{return model.ready();});check(model.readOnly()&&model.create("Forbidden").isEmpty(),"future version remains read-only");model.flush();check(bytes(futurePath)==future,"unsupported version preserved exactly");}
    auto blocker=root+"/blocked";{QFile f(blocker);f.open(QIODevice::WriteOnly);f.write("owned blocker");}
    {Model model(blocker+"/scenes.json");wait([&]{return model.ready();});model.create("Write fails");model.flush();wait([&]{return model.idle();});pump(20);check(model.status().contains("Cannot create")||model.status().contains("failed"),"controlled write failure reported");check(bytes(blocker)=="owned blocker","write fault preserves unrelated owned file");}
    check(GetForegroundWindow()==foreground,"no input-desktop foreground change");report<<"RESULT checks="<<checks<<" failures="<<failures<<" scale="<<qEnvironmentVariable("QT_SCALE_FACTOR").toStdString()<<"; owned inactive desktop; no physical input or audio\n";return failures?1:0;
}
