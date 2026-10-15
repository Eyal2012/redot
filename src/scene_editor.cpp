#include "scene_editor.h"
#include "window_chrome.h"
#include "dashboard_icons.h"
#include "audio_device_catalog.h"
#include "application_picker.h"
#include "scene_runtime.h"
#include <QContextMenuEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QToolButton>
#include <QShortcut>
#include <QWheelEvent>
#include <QWindow>
#include <QTextEdit>
#include <algorithm>
#include <cmath>

namespace Scenes {
namespace {
QWidget* panel(const QString& name,QWidget* parent=nullptr) {
    auto w=new QWidget(parent);w->setObjectName(name);
    auto material=new WorkspaceMaterial(w);w->installEventFilter(new DockMaterialLayout(material,w));return w;
}
class ScrollInput final:public QObject {
    QScrollArea* scroll_;
public:
    ScrollInput(QScrollArea* area):QObject(area),scroll_(area){}
    bool eventFilter(QObject*,QEvent* event)override {
        if(event->type()!=QEvent::Wheel)return false;
        auto wheel=static_cast<QWheelEvent*>(event);auto bar=scroll_->verticalScrollBar();
        auto pixels=wheel->pixelDelta().y();int delta=pixels?pixels:wheel->angleDelta().y()/120*QApplication::wheelScrollLines()*24;
        if(delta){bar->setValue(bar->value()-delta);wheel->accept();return true;}return false;
    }
};
QPushButton* button(const QString& text,const QString& name,QBoxLayout* layout) {
    auto b=new QPushButton(text);b->setObjectName(name);b->setAccessibleName(text);layout->addWidget(b);return b;
}
QLabel* title(const QString& text) { auto l=new QLabel(text);l->setObjectName("sectionHeading");return l; }
QString association(const Definition& d) {
    auto value=d.settings["deviceName"].toString();
    if(value.isEmpty())value=d.settings["target"].toString();
    if(value.isEmpty())value=d.kind==Kind::DesktopAudio?"Whole system — explicit":d.kind==Kind::Microphone?"Windows default recording input (eConsole)":"Select application in source properties";
    return value;
}
void confirmRemove(QWidget* parent,const QString& description,std::function<void()> done) {
    auto dialog=new QMessageBox(QMessageBox::Question,"Remove from scene",description,QMessageBox::Yes|QMessageBox::Cancel,parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setDefaultButton(QMessageBox::Cancel);
    QObject::connect(dialog,&QMessageBox::finished,parent,[done](int result){if(result==QMessageBox::Yes)done();});dialog->open();
}
void nameDialog(QWidget* owner,Model* model,bool rename) {
    const auto s=model->current();if(!s)return;const auto id=s->id;
    auto d=new QInputDialog(owner);d->setWindowTitle(rename?"Rename scene":"Create scene");d->setLabelText("Scene name");d->setTextValue(rename?s->name:QString("Scene %1").arg(model->data().scenes.size()+1));d->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(d,&QInputDialog::textValueSelected,model,[model,id,rename](const QString& name){if(rename)model->rename(id,name);else if(!name.trimmed().isEmpty())model->create(name);});d->open();
}
}
Canvas::Canvas(Model* model,QWidget* parent):QWidget(parent),model_(model) {
    setObjectName("sceneCanvas");setAccessibleName("Scene canvas; arrow keys move selection; Delete removes; Escape clears selection");setFocusPolicy(Qt::StrongFocus);setMinimumSize(180,140);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    connect(model,&Model::changed,this,[this]{refreshImages();update();});connect(model,&Model::visualChanged,this,qOverload<>(&Canvas::update));connect(model,&Model::selectionChanged,this,qOverload<>(&Canvas::update));
    connect(&imageWorker_,&QFutureWatcher<QImage>::finished,this,[this]{
        auto image=imageWorker_.result();if(image.isNull())imageErrors_[loadingImage_]="Image unavailable / unsupported";
        else images_.insert(loadingImage_,image);loadingImage_.clear();update();nextImage();
    });
}
QRectF Canvas::viewport() const {
    // Reserve only the handle hit area and the 24-DIP rotation stem, not a
    // black presentation rectangle around the scene. Coordinates remain DIP.
    auto r=QRectF(rect()).adjusted(6,34,-6,-6);auto scene=model_->current();
    const double aspect=scene?double(scene->width)/scene->height:16/9.;
    double w=std::max(1.,std::min(r.width(),std::max(1.,r.height())*aspect));
    return {r.center().x()-w/2,r.center().y()-w/aspect/2,w,w/aspect};
}
QPointF Canvas::toCanvas(QPointF p) const { auto r=viewport();auto s=model_->current();return {(p.x()-r.x())*(s?s->width:1920)/r.width(),(p.y()-r.y())*(s?s->height:1080)/r.height()}; }
QPointF Canvas::toWidget(QPointF p) const { auto r=viewport();auto s=model_->current();return {r.x()+p.x()*r.width()/(s?s->width:1920),r.y()+p.y()*r.height()/(s?s->height:1080)}; }
QPolygonF Canvas::corners(const Visual& v) const { QPolygonF result;for(auto p:QPolygonF(v.localRect()).mid(0,4))result<<toWidget(v.transform().map(p));return result; }
QString Canvas::hit(QPointF p) const {
    if(!viewport().contains(p))return {};auto s=model_->current();if(!s)return {};p=toCanvas(p);
    for(auto it=s->visuals.crbegin();it!=s->visuals.crend();++it)if(it->visible&&it->localRect().adjusted(it->cropLeft,it->cropTop,-it->cropRight,-it->cropBottom).contains(it->transform().inverted().map(p)))return it->id;
    return {};
}
void Canvas::refreshImages() {
    if(render_)return; // The canonical runtime owns production asset decoding/uploads.
    QSet<QString> wanted;
    for(auto& d:model_->data().definitions)if(d.kind==Kind::Image){auto path=d.settings["path"].toString();if(!path.isEmpty())wanted.insert(path);}
    for(auto it=images_.begin();it!=images_.end();)if(!wanted.contains(it.key()))it=images_.erase(it);else ++it;
    for(auto it=requestedImages_.begin();it!=requestedImages_.end();)if(!wanted.contains(*it)&&*it!=loadingImage_){imageErrors_.remove(*it);pendingImages_.removeAll(*it);it=requestedImages_.erase(it);}else ++it;
    // At most eight 2048-pixel decoded images (128 MiB worst case), one worker,
    // and a bounded pending list. Nothing decodes or reads files while painting.
    for(auto& path:wanted)if(!requestedImages_.contains(path)&&requestedImages_.size()<8){requestedImages_.insert(path);pendingImages_.append(path);}
    nextImage();
}
void Canvas::nextImage() {
    if(!loadingImage_.isEmpty()||pendingImages_.isEmpty())return;
    loadingImage_=pendingImages_.takeFirst();auto path=loadingImage_;
    imageWorker_.setFuture(QtConcurrent::run([path]{QImageReader reader(path);reader.setAutoTransform(true);auto size=reader.size();
        if(!size.isValid()||size.width()>16384||size.height()>16384)return QImage{};
        if(size.width()>2048||size.height()>2048)reader.setScaledSize(size.scaled(2048,2048,Qt::KeepAspectRatio));return reader.read();}));
}
void Canvas::updatePresentationMask(const QRectF& viewport,const QVector<QPolygonF>& lines,const QVector<QPointF>& handles) {
    const auto scale=devicePixelRatioF();
    if(viewport==maskedViewport_&&lines==maskedLines_&&handles==maskedHandles_&&scale==maskedScale_)return;
    // Clip the *native HWND*, rather than painting another opaque panel over
    // its clear color. The surrounding editor reveals its existing material.
    QRegion region(viewport.toAlignedRect());
    QPainterPathStroker stroke;stroke.setWidth(3);QPainterPath clip;clip.addRect(rect());
    for(const auto& line:lines){QPainterPath path;path.addPolygon(line);region|=QRegion(stroke.createStroke(path).intersected(clip).toFillPolygon().toPolygon());}
    for(auto point:handles)region|=QRegion(QRectF(point-QPointF(9,9),QSizeF(18,18)).toAlignedRect());
    region&=QRegion(rect());if(mask()!=region)setMask(region);
    else if(scale!=maskedScale_&&windowHandle())windowHandle()->setMask(region);
    maskedViewport_=viewport;maskedLines_=lines;maskedHandles_=handles;maskedScale_=scale;
}
void Canvas::paintEvent(QPaintEvent*) {
    if(render_){
        auto scale=devicePixelRatioF();QVector<QPolygonF> lines;QVector<QPointF> handles;
        if(auto v=model_->visual(model_->selection());v&&v->visible){auto polygon=corners(*v);if(cropMode_){polygon.clear();for(auto point:QPolygonF(v->localRect().adjusted(v->cropLeft,v->cropTop,-v->cropRight,-v->cropBottom)).mid(0,4))polygon<<toWidget(v->transform().map(point));}
            auto outline=polygon;outline<<polygon[0];lines<<outline;if(!v->locked){for(int i=0;i<8;++i)handles<< (i<4?polygon[i]:(polygon[i-4]+polygon[(i-3)%4])/2);if(!cropMode_){auto rotate=rotationHandle(*v);lines<<QPolygonF{(polygon[0]+polygon[1])/2,rotate};handles<<rotate;}}}
        auto r=viewport();if(guideX_>=0){auto x=toWidget({guideX_,0}).x();lines<<QPolygonF{{x,r.top()},{x,r.bottom()}};}if(guideY_>=0){auto y=toWidget({0,guideY_}).y();lines<<QPolygonF{{r.left(),y},{r.right(),y}};}
        updatePresentationMask(r,lines,handles);
        for(auto& line:lines)for(auto& pt:line)pt*=scale;for(auto& pt:handles)pt*=scale;
        render_(reinterpret_cast<HWND>(winId()),QRectF(r.topLeft()*scale,r.size()*scale),lines,handles);return;
    }
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto r=viewport();
    p.setPen(QPen(DesignSystem::color("outline"),1));p.setBrush(DesignSystem::color("videoBlack"));p.drawRoundedRect(r,12,12);
    auto s=model_->current();if(!s)return;
    p.save();QPainterPath clip;clip.addRoundedRect(r,12,12);p.setClipPath(clip);p.translate(r.topLeft());p.scale(r.width()/s->width,r.height()/s->height);
    if(s->visuals.isEmpty()){p.setPen(DesignSystem::color("secondary"));QFont f=p.font();f.setPixelSize(42);p.setFont(f);p.drawText(QRectF(0,0,s->width,s->height),Qt::AlignCenter,"Add a source to build this scene");}
    for(auto& v:s->visuals)if(v.visible)if(auto d=model_->definition(v.sourceId)) {
        p.save();p.setWorldTransform(v.transform(),true);p.setClipRect(QRectF(v.cropLeft,v.cropTop,v.width-v.cropLeft-v.cropRight,v.height-v.cropTop-v.cropBottom),Qt::IntersectClip);
        if(d->kind==Kind::Text) {
            auto color=QColor(d->settings["color"].toString("#F5F0FF"));if(!color.isValid())color=DesignSystem::color("text");p.setPen(color);QFont f("Segoe UI");f.setPixelSize(std::clamp(d->settings["fontSize"].toInt(64),8,400));p.setFont(f);
            p.drawText(v.localRect(),Qt::AlignCenter|Qt::TextWordWrap,d->settings["text"].toString("ReDot"));
        } else if(d->kind==Kind::Image&&images_.contains(d->settings["path"].toString())) {
            const auto& image=images_[d->settings["path"].toString()];
            p.drawImage(v.localRect(),image);
        } else {
            p.fillRect(v.localRect(),DesignSystem::color("surfaceSolid"));p.setPen(QPen(DesignSystem::color("outline"),2));p.drawRect(v.localRect().adjusted(2,2,-2,-2));QFont f("Segoe UI");f.setPixelSize(std::clamp(int(std::min(v.width/18,v.height/7)),12,44));p.setFont(f);p.setPen(DesignSystem::color("secondary"));
            auto message=d->kind==Kind::Image?(imageErrors_.value(d->settings["path"].toString(),requestedImages_.contains(d->settings["path"].toString())?"Loading image…":"Image cache limit; reopen Scenes to reload")):"Unbound component preview · live source requires the scene runtime";
            p.drawText(v.localRect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,d->name+"\n"+kindName(d->kind)+"\n"+message);
        }p.restore();
    }p.restore();
    if(auto v=model_->visual(model_->selection());v&&v->visible) {
        auto points=corners(*v);
        if(cropMode_) {points.clear();for(auto q:QPolygonF(v->localRect().adjusted(v->cropLeft,v->cropTop,-v->cropRight,-v->cropBottom)).mid(0,4))points<<toWidget(v->transform().map(q));}
        p.setBrush(Qt::NoBrush);p.setPen(QPen(DesignSystem::color("accent"),1.5,v->locked?Qt::DashLine:Qt::SolidLine));p.drawPolygon(points);
        if(!v->locked){p.setBrush(DesignSystem::color("handle"));for(int i=0;i<8;++i){auto q=i<4?points[i]:(points[i-4]+points[(i-3)%4])/2;p.drawRoundedRect(QRectF(q-QPointF(4,4),QSizeF(8,8)),2,2);}
            if(!cropMode_){auto top=(points[0]+points[1])/2,rotate=rotationHandle(*v);p.drawLine(top,rotate);p.drawEllipse(rotate,6,6);}
        }
    }
    p.setPen(QPen(DesignSystem::color("accent"),1,Qt::DashLine));
    if(guideX_>=0){auto x=toWidget({guideX_,0}).x();p.drawLine(QPointF(x,r.top()),QPointF(x,r.bottom()));}
    if(guideY_>=0){auto y=toWidget({0,guideY_}).y();p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y));}
}
void Canvas::bindPresentation(std::function<bool(HWND,const QRectF&,const QVector<QPolygonF>&,const QVector<QPointF>&)> render){
    render_=std::move(render);setAttribute(Qt::WA_DontCreateNativeAncestors);setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
    if(parentWidget())parentWidget()->installEventFilter(this);
    auto timer=new QTimer(this);timer->setInterval(33);connect(timer,&QTimer::timeout,this,[this]{if(isVisible())update();});timer->start();
}
bool Canvas::eventFilter(QObject* watched,QEvent* event) {
    if(watched==parentWidget()&&event->type()==QEvent::MouseButtonPress&&isVisible()){
        auto mouse=static_cast<QMouseEvent*>(event);auto point=mapFrom(parentWidget(),mouse->position());
        // Mask holes expose the parent material and no longer receive native
        // input. Retain the previous click-on-editor-space deselection behavior.
        if(mouse->button()==Qt::LeftButton&&rect().contains(point.toPoint())&&!mask().contains(point.toPoint())){model_->selectVisual({});setFocus(Qt::MouseFocusReason);mouse->accept();return true;}
    }
    return QWidget::eventFilter(watched,event);
}
void Page::runtimeStatus(const QHash<QString,QString>& states){
    for(int i=0;i<sources_->count();++i){auto row=sources_->item(i);if(auto visual=model_->visual(row->data(Qt::UserRole).toString()))if(auto d=model_->definition(visual->sourceId)){auto status=states.value(d->id,"Waiting for source");row->setToolTip(d->name+" · "+status);row->setText(d->name+(status.startsWith("Unavailable")?" · unavailable":""));}}
    if(auto scene=model_->current())for(auto& a:scene->audio)if(auto label=findChild<QLabel*>("sceneAudioState_"+a.id)){auto status=states.value(a.sourceId,"Idle · opens during recording/replay");label->setText(status);label->setToolTip(status);}
}
QPointF Canvas::rotationHandle(const Visual& v) const {
    auto points=corners(v);auto top=(points[0]+points[1])/2,center=(points[0]+points[2])/2;
    auto direction=top-center;double length=std::hypot(direction.x(),direction.y());return top+(length>0?direction/length*24:QPointF(0,-24));
}
void Canvas::setCropMode(bool enabled){if(cropMode_==enabled)return;cropMode_=enabled;dragging_.clear();update();Q_EMIT cropModeChanged(enabled);}
void Canvas::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton)return;setFocus(Qt::MouseFocusReason);corner_=-1;rotating_=false;cropping_=cropMode_||e->modifiers().testFlag(Qt::AltModifier);dragging_.clear();
    if(auto v=model_->visual(model_->selection());v&&v->visible&&!v->locked) {
        auto points=corners(*v);
        if(cropping_){points.clear();for(auto q:QPolygonF(v->localRect().adjusted(v->cropLeft,v->cropTop,-v->cropRight,-v->cropBottom)).mid(0,4))points<<toWidget(v->transform().map(q));}
        if(!cropping_&&QLineF(rotationHandle(*v),e->position()).length()<=10){rotating_=true;dragging_=v->id;}
        else for(int i=0;i<8;++i){auto q=i<4?points[i]:(points[i-4]+points[(i-3)%4])/2;if(QLineF(q,e->position()).length()<=9){corner_=i;dragging_=v->id;break;}}
    }
    if(dragging_.isEmpty()){auto id=hit(e->position());model_->selectVisual(id);if(auto v=model_->visual(id);v&&!v->locked)dragging_=id;}
    if(auto v=model_->visual(dragging_)){start_=*v;press_=toCanvas(e->position());
        if(rotating_)anchor_=start_.transform().map(start_.localRect().center());
        else if(corner_>=0){auto points=QPolygonF(start_.localRect());int opposite=corner_<4?(corner_+2)%4:(corner_-4+2)%4;auto q=corner_<4?points[opposite]:(points[opposite]+points[(opposite+1)%4])/2;anchor_=start_.transform().map(q);}
    }
}
void Canvas::mouseMoveEvent(QMouseEvent* e) {
    auto current=model_->visual(dragging_);if(!current||current->locked||!current->visible||!(e->buttons()&Qt::LeftButton))return;
    auto v=start_;auto point=toCanvas(e->position());guideX_=guideY_=-1;
    if(rotating_) {
        auto a=press_-anchor_,b=point-anchor_;double degrees=(std::atan2(b.y(),b.x())-std::atan2(a.y(),a.x()))*180/3.141592653589793;
        v.rotation=std::remainder(start_.rotation+degrees,360.);if(e->modifiers().testFlag(Qt::ShiftModifier))v.rotation=std::round(v.rotation/15)*15;
        QTransform orient;orient.rotate(v.rotation);orient.scale(v.scale,v.scale);auto origin=anchor_-orient.map(v.localRect().center());v.x=origin.x();v.y=origin.y();
    } else if(corner_<0) {
        if(cropping_)return;
        v.x=std::clamp(v.x+point.x()-press_.x(),-100000.,100000.);v.y=std::clamp(v.y+point.y()-press_.y(),-100000.,100000.);
        if(!e->modifiers().testFlag(Qt::ControlModifier))if(auto scene=model_->current()){
            auto bounds=v.transform().mapRect(v.localRect().adjusted(v.cropLeft,v.cropTop,-v.cropRight,-v.cropBottom));double limit=7.*scene->width/viewport().width();
            auto snap=[limit](double a,double b,double c,double extent,double& guide){double delta=limit+1;for(double target:{0.,extent/2.,extent})for(double value:{a,b,c})if(std::abs(target-value)<std::abs(delta)){delta=target-value;guide=target;}if(std::abs(delta)>limit){guide=-1;return 0.;}return delta;};
            v.x+=snap(bounds.left(),bounds.center().x(),bounds.right(),scene->width,guideX_);v.y+=snap(bounds.top(),bounds.center().y(),bounds.bottom(),scene->height,guideY_);
        }
    } else if(cropping_) {
        auto local=v.transform().inverted().map(point);
        bool left=corner_==0||corner_==3||corner_==7,right=corner_==1||corner_==2||corner_==5;
        bool top=corner_==0||corner_==1||corner_==4,bottom=corner_==2||corner_==3||corner_==6;
        if(left)v.cropLeft=std::clamp(local.x(),0.,v.width-v.cropRight-1);
        if(right)v.cropRight=std::clamp(v.width-local.x(),0.,v.width-v.cropLeft-1);
        if(top)v.cropTop=std::clamp(local.y(),0.,v.height-v.cropBottom-1);
        if(bottom)v.cropBottom=std::clamp(v.height-local.y(),0.,v.height-v.cropTop-1);
    } else {
        QTransform orient;orient.rotate(v.rotation);orient.scale(v.scale,v.scale);auto delta=orient.inverted().map(point-anchor_);
        bool left=corner_==0||corner_==3||corner_==7,top=corner_==0||corner_==1||corner_==4;
        double w=corner_==4||corner_==6?v.width:std::clamp(delta.x()*(left?-1:1),1.,16384.);
        double h=corner_==5||corner_==7?v.height:std::clamp(delta.y()*(top?-1:1),1.,16384.);
        if(v.aspectLocked&&!e->modifiers().testFlag(Qt::ShiftModifier)){double ratio=v.width/v.height;if(corner_==4||corner_==6)w=h*ratio;else if(corner_==5||corner_==7)h=w/ratio;else if(w/h>ratio)h=w/ratio;else w=h*ratio;double cap=std::min({1.,16384./w,16384./h});w*=cap;h*=cap;}
        QPointF opposite(left?w:0,top?h:0);if(corner_==4||corner_==6)opposite.setX(w/2);if(corner_==5||corner_==7)opposite.setY(h/2);
        auto origin=anchor_-orient.map(opposite);v.cropLeft*=w/v.width;v.cropRight*=w/v.width;v.cropTop*=h/v.height;v.cropBottom*=h/v.height;v.x=origin.x();v.y=origin.y();v.width=w;v.height=h;
    }model_->updateVisual(v);
}
void Canvas::mouseReleaseEvent(QMouseEvent*){dragging_.clear();corner_=-1;rotating_=false;guideX_=guideY_=-1;update();}
void Canvas::transformAction(const QString& action){
    if(action=="Advanced"){Q_EMIT advancedTransformRequested();return;}if(action=="Crop"){setCropMode(!cropMode_);return;}
    auto selected=model_->visual(model_->selection());auto scene=model_->current();if(!selected||selected->locked||!scene)return;
    auto v=*selected;auto bounds=v.transform().mapRect(v.localRect().adjusted(v.cropLeft,v.cropTop,-v.cropRight,-v.cropBottom));
    if(action=="Horizontal")v.x+=scene->width/2.-bounds.center().x();
    else if(action=="Vertical")v.y+=scene->height/2.-bounds.center().y();
    else if(action=="Fit"){model_->fit(v.id);return;}
    else if(action=="Stretch"){v.aspectLocked=false;v.x=v.y=v.rotation=0;v.scale=1;v.width=scene->width;v.height=scene->height;v.cropLeft=v.cropTop=v.cropRight=v.cropBottom=0;}
    else if(action=="Reset"){if(auto d=model_->definition(v.sourceId)){v.x=v.y=v.rotation=0;v.scale=1;v.width=d->intrinsic.width();v.height=d->intrinsic.height();v.cropLeft=v.cropTop=v.cropRight=v.cropBottom=0;}}
    else return;
    model_->updateVisual(v);
}
void Canvas::contextMenuEvent(QContextMenuEvent* e){
    if(!hit(e->pos()).isEmpty())model_->selectVisual(hit(e->pos()));
    auto menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("sceneCanvasMenu");auto selected=model_->visual(model_->selection());
    for(auto pair:{std::pair{"Center horizontally","Horizontal"},{"Center vertically","Vertical"},{"Fit to screen","Fit"},{"Stretch to screen","Stretch"},{"Reset transform","Reset"},{"Crop mode · drag handles / Alt+drag","Crop"},{"Advanced Transform…","Advanced"}}){auto action=menu->addAction(pair.first,this,[this,key=QString(pair.second)]{transformAction(key);});action->setEnabled(selected&&!selected->locked);if(QString(pair.second)=="Crop"){action->setCheckable(true);action->setChecked(cropMode_);}}
    connect(menu,&QMenu::aboutToHide,this,[this]{setFocus(Qt::PopupFocusReason);});menu->popup(e->globalPos());
}
void Canvas::keyPressEvent(QKeyEvent* e) {
    if(e->key()==Qt::Key_Escape){dragging_.clear();guideX_=guideY_=-1;if(cropMode_)setCropMode(false);else model_->selectVisual({});update();return;}
    if(e->key()==Qt::Key_Delete){auto id=model_->selection();if(!id.isEmpty())confirmRemove(this,"Remove this source instance? Shared definitions remain available.",[this,id]{model_->removeVisual(id);});return;}
    if(auto selected=model_->visual(model_->selection());selected&&!selected->locked) {
        auto v=*selected;double step=e->modifiers().testFlag(Qt::ShiftModifier)?10:1;
        if(e->key()==Qt::Key_Left)v.x-=step;else if(e->key()==Qt::Key_Right)v.x+=step;else if(e->key()==Qt::Key_Up)v.y-=step;else if(e->key()==Qt::Key_Down)v.y+=step;else {QWidget::keyPressEvent(e);return;}model_->updateVisual(v);return;
    }QWidget::keyPressEvent(e);
}
Page::Page(Model* model,QWidget* parent):QWidget(parent),model_(model) {
    setObjectName("scenesPage");auto outer=new QVBoxLayout(this);outer->setContentsMargins(14,4,16,8);outer->setSpacing(10);
    auto top=new QHBoxLayout;auto titles=new QVBoxLayout;auto label=new QLabel("Scenes");label->setStyleSheet("font-size:26px;font-weight:600;");titles->addWidget(label);titles->addWidget(new QLabel("Build what ReDot records"));top->addLayout(titles);top->addStretch();auto create=button("New Scene","sceneNew",top);outer->addLayout(top);
    auto split=new QSplitter(Qt::Horizontal);split->setObjectName("sceneEditorSplitter");split->setChildrenCollapsible(false);outer->addWidget(split,1);
    auto left=panel("sceneListPanel");left->setMinimumWidth(136);left->setMaximumWidth(240);auto lb=new QVBoxLayout(left);lb->setContentsMargins(10,10,10,10);lb->addWidget(title("Scenes"));scenes_=new QListWidget;scenes_->setObjectName("scenesList");scenes_->setAccessibleName("Scenes list");lb->addWidget(scenes_,1);
    auto l1=new QHBoxLayout;auto add=button("+","sceneCreate",l1);add->setToolTip("Create Scene");auto rename=button("Rename","sceneRename",l1);lb->addLayout(l1);
    auto l2=new QHBoxLayout;auto duplicate=button("Duplicate","sceneDuplicate",l2);auto remove=button("−","sceneDelete",l2);remove->setToolTip("Delete Scene");lb->addLayout(l2);
    auto order=new QHBoxLayout;auto up=button("↑","sceneUp",order);up->setToolTip("Move scene up");auto down=button("↓","sceneDown",order);down->setToolTip("Move scene down");lb->addLayout(order);split->addWidget(left);
    auto center=panel("sceneEditorPanel");auto cb=new QVBoxLayout(center);cb->setContentsMargins(8,10,8,10);heading_=title("");heading_->setWordWrap(true);cb->addWidget(heading_);canvas_=new Canvas(model);cb->addWidget(canvas_,1);
    auto tools=new QHBoxLayout;auto centerButton=button("Center","sceneCenter",tools);auto fitButton=button("Fit","sceneFit",tools);tools->addStretch();resolution_=new QComboBox;resolution_->setObjectName("sceneResolution");resolution_->addItems({"1920 × 1080","1280 × 720","2560 × 1440","3840 × 2160"});resolution_->setMaximumWidth(156);tools->addWidget(resolution_);cb->addLayout(tools);auto note=new QLabel("Selected scene · shared by Capture, recording and replay");note->setWordWrap(true);note->setStyleSheet("font-size:11px;");cb->addWidget(note);split->addWidget(center);
    auto right=panel("sceneSourcesPanel");right->setMinimumWidth(185);right->setMaximumWidth(290);auto rb=new QVBoxLayout(right);rb->setContentsMargins(10,10,10,10);auto sh=new QHBoxLayout;sh->addWidget(title("Sources"));sh->addStretch();auto addSource=button("+","sceneAddSource",sh);addSource->setToolTip("Add visual source");rb->addLayout(sh);
    sources_=new QListWidget;sources_->setObjectName("sceneSourceList");sources_->setAccessibleName("Visual sources, front to back");sources_->setMinimumHeight(60);sources_->setMaximumHeight(145);rb->addWidget(sources_);
    auto toolsScroll=new QScrollArea;toolsScroll->setObjectName("sceneEditorToolsScroll");toolsScroll->setWidgetResizable(true);toolsScroll->setFrameShape(QFrame::NoFrame);toolsScroll->setMinimumHeight(70);auto toolsBody=new QWidget;auto sourceTools=new QVBoxLayout(toolsBody);sourceTools->setContentsMargins(0,0,4,0);toolsScroll->setWidget(toolsBody);rb->addWidget(toolsScroll,1);
    auto st=new QHBoxLayout;auto front=button("↑","sourceFront",st);front->setToolTip("Raise layer");auto back=button("↓","sourceBack",st);back->setToolTip("Lower layer");auto del=button("−","sourceRemove",st);del->setToolTip("Remove source");auto props=button("…","sourceProperties",st);props->setToolTip("Source definition properties (shared)");sourceTools->addLayout(st);
    auto deleteKey=new QShortcut(QKeySequence::Delete,sources_);deleteKey->setContext(Qt::WidgetShortcut);connect(deleteKey,&QShortcut::activated,del,&QPushButton::click);
    auto flags=new QHBoxLayout;visible_=new QCheckBox("Visible");visible_->setObjectName("sourceVisible");locked_=new QCheckBox("Lock");locked_->setObjectName("sourceLocked");flags->addWidget(visible_);flags->addWidget(locked_);sourceTools->addLayout(flags);
    selectionState_=new QLabel("Select a source");selectionState_->setWordWrap(true);sourceTools->addWidget(selectionState_);
    auto editTools=new QVBoxLayout;auto advanced=button("Advanced Transform…","sceneAdvancedTransform",editTools);auto crop=button("Crop mode","sceneCropMode",editTools);crop->setCheckable(true);sourceTools->addLayout(editTools);
    auto help=new QLabel("Drag to move · handles to resize\nShift: free resize / 15° rotation snap\nAlt+handle: crop · Ctrl: bypass snap\nRight-click canvas: alignment\nEscape: leave crop / clear selection");help->setWordWrap(true);help->setStyleSheet("font-size:11px;");sourceTools->addWidget(help);sourceTools->addStretch();
    split->addWidget(right);split->setStretchFactor(1,1);split->setSizes({180,650,240});
    connect(advanced,&QPushButton::clicked,this,&Page::advancedTransform);connect(canvas_,&Canvas::advancedTransformRequested,this,&Page::advancedTransform);
    connect(crop,&QPushButton::toggled,canvas_,&Canvas::setCropMode);
    connect(canvas_,&Canvas::cropModeChanged,crop,[crop](bool enabled){QSignalBlocker block(crop);crop->setChecked(enabled);});
    connect(model,&Model::selectionChanged,this,[this,crop]{canvas_->setCropMode(false);QSignalBlocker block(crop);crop->setChecked(false);});
    auto audio=panel("sceneAudioPanel");auto ab=new QVBoxLayout(audio);ab->setContentsMargins(12,8,12,8);auto ah=new QHBoxLayout;ah->addWidget(title("Audio Sources"));auto audioNote=new QLabel("Scene mix · no speaker monitoring");audioNote->setStyleSheet("font-size:11px;");ah->addWidget(audioNote,1);auto addAudio=button("+ Add audio","sceneAddAudio",ah);ab->addLayout(ah);
    auto audioScroll=new QScrollArea;audioScroll->setObjectName("sceneAudioScroll");audioScroll->setWidgetResizable(true);audioScroll->setFrameShape(QFrame::NoFrame);audioBody_=new QWidget;audioLayout_=new QVBoxLayout(audioBody_);audioLayout_->setContentsMargins(0,0,4,0);audioLayout_->setSpacing(4);audioScroll->setWidget(audioBody_);audio->setMinimumHeight(130);audio->setMaximumHeight(180);ab->addWidget(audioScroll,1);outer->addWidget(audio);
    saveState_=new QLabel;saveState_->setObjectName("sceneSaveStatus");saveState_->setWordWrap(true);saveState_->setStyleSheet("font-size:11px;");outer->addWidget(saveState_);
    centerButton->setToolTip("Center selected source");fitButton->setToolTip("Fit selected source to canvas");
    for(auto control:{add,remove,up,down,addSource,front,back,del,props,centerButton,fitButton})control->setAccessibleName(control->toolTip());
    connect(create,&QPushButton::clicked,this,[this]{nameDialog(this,model_,false);});connect(add,&QPushButton::clicked,create,&QPushButton::click);connect(rename,&QPushButton::clicked,this,[this]{nameDialog(this,model_,true);});
    connect(duplicate,&QPushButton::clicked,this,[this]{if(auto s=model_->current())model_->duplicate(s->id);});connect(remove,&QPushButton::clicked,this,[this]{if(auto s=model_->current()){auto id=s->id;if(model_->data().scenes.size()==1)return;confirmRemove(this,"Delete “"+s->name+"”? This removes its instances, not shared definitions.",[this,id]{model_->remove(id);});}});
    connect(scenes_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* i){if(!updating_&&i)model_->select(i->data(Qt::UserRole).toString());});
    connect(up,&QPushButton::clicked,this,[this]{model_->reorder(model_->data().selected,scenes_->currentRow()-1);});connect(down,&QPushButton::clicked,this,[this]{model_->reorder(model_->data().selected,scenes_->currentRow()+1);});
    connect(sources_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* i){if(!updating_)model_->selectVisual(i?i->data(Qt::UserRole).toString():QString{});});
    connect(addSource,&QPushButton::clicked,this,[this,addSource]{addMenu(false,addSource);});connect(addAudio,&QPushButton::clicked,this,[this,addAudio]{addMenu(true,addAudio);});
    auto moveLayer=[this](int step){if(auto s=model_->current())for(int i=0;i<s->visuals.size();++i)if(s->visuals[i].id==model_->selection()){model_->moveVisual(model_->selection(),i+step);break;}};
    connect(front,&QPushButton::clicked,this,[moveLayer]{moveLayer(1);});connect(back,&QPushButton::clicked,this,[moveLayer]{moveLayer(-1);});
    connect(del,&QPushButton::clicked,this,[this]{auto id=model_->selection();if(!id.isEmpty())confirmRemove(this,"Remove this visual instance? Other scenes keep their copies.",[this,id]{model_->removeVisual(id);});});
    connect(props,&QPushButton::clicked,this,&Page::sourceProperties);connect(sources_,&QListWidget::itemDoubleClicked,this,[this]{sourceProperties();});
    connect(visible_,&QCheckBox::toggled,this,[this](bool value){if(!updating_)if(auto v=model_->visual(model_->selection())){auto copy=*v;copy.visible=value;model_->updateVisual(copy);selection();}});
    connect(locked_,&QCheckBox::toggled,this,[this](bool value){if(!updating_)if(auto v=model_->visual(model_->selection())){auto copy=*v;copy.locked=value;model_->updateVisual(copy);selection();}});
    connect(centerButton,&QPushButton::clicked,this,[this]{model_->center(model_->selection());});connect(fitButton,&QPushButton::clicked,this,[this]{model_->fit(model_->selection());});
    connect(resolution_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){if(updating_||i<0)return;const std::array<QSize,4> sizes={QSize(1920,1080),QSize(1280,720),QSize(2560,1440),QSize(3840,2160)};model_->setResolution(sizes[i].width(),sizes[i].height());});
    connect(model,&Model::changed,this,&Page::refresh);connect(model,&Model::selectionChanged,this,&Page::selection);connect(model,&Model::visualChanged,this,&Page::selection);connect(model,&Model::statusChanged,this,[this]{saveState_->setText(model_->status());});refresh();
}
void Page::refresh() {
    updating_=true;setEnabled(model_->ready()&&!model_->readOnly());saveState_->setText(model_->status());scenes_->clear();sources_->clear();
    for(auto& s:model_->data().scenes){auto i=new QListWidgetItem(s.name,scenes_);i->setData(Qt::UserRole,s.id);if(s.id==model_->data().selected)scenes_->setCurrentItem(i);}
    if(auto s=model_->current()) {
        heading_->setText(QString("%1 · %2 × %3").arg(s->name).arg(s->width).arg(s->height));
        for(auto it=s->visuals.crbegin();it!=s->visuals.crend();++it)if(auto d=model_->definition(it->sourceId)){auto i=new QListWidgetItem(d->name,sources_);i->setData(Qt::UserRole,it->id);i->setToolTip(kindName(d->kind)+(d->kind==Kind::Image||d->kind==Kind::Text?" · editor preview":" · scene source"));}
        const std::array<int,4> widths={1920,1280,2560,3840};auto pos=std::find(widths.begin(),widths.end(),s->width);resolution_->setCurrentIndex(pos==widths.end()?-1:int(pos-widths.begin()));
    }
    findChild<QPushButton*>("sceneDelete")->setEnabled(model_->data().scenes.size()>1);updating_=false;audioRows();selection();
}
void Page::selection() {
    if(updating_)return;updating_=true;auto v=model_->visual(model_->selection());
    for(int i=0;i<sources_->count();++i){auto item=sources_->item(i);auto value=model_->visual(item->data(Qt::UserRole).toString());if(value)if(auto d=model_->definition(value->sourceId)){item->setText(QString(value->visible?"":"Hidden · ")+(value->locked?"Locked · ":"")+d->name);}if(v&&item->data(Qt::UserRole).toString()==v->id)sources_->setCurrentItem(item);}
    if(!v)sources_->setCurrentRow(-1);selectionState_->setText(v?(v->locked?"Locked · unlock to transform":"Coordinates in scene pixels"):"Select a source");
    visible_->setEnabled(v);locked_->setEnabled(v);
    for(auto name:{"sceneCenter","sceneFit","sceneAdvancedTransform","sceneCropMode"})findChild<QPushButton*>(name)->setEnabled(v&&!v->locked);
    if(v){visible_->setChecked(v->visible);locked_->setChecked(v->locked);}
    updating_=false;
}
void Page::advancedTransform(){
    for(auto dialog:findChildren<QDialog*>("sceneAdvancedDialog"))if(dialog->isVisible())return;
    auto v=model_->visual(model_->selection());if(!v||v->locked)return;const auto id=v->id;const auto sceneId=model_->data().selected;
    auto dialog=new QDialog(this);dialog->setObjectName("sceneAdvancedDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);DesktopBackdrop::prepare(dialog);auto backdrop=new DesktopBackdrop(dialog);dialog->setWindowFlag(Qt::Window,true);dialog->setWindowFlag(Qt::FramelessWindowHint,true);dialog->setWindowTitle("ReDot · Advanced Transform");dialog->resize(460,550);dialog->setMinimumSize(360,330);new WorkspaceGlassLayer(dialog);
    auto box=new QVBoxLayout(dialog);box->setContentsMargins(10,0,10,12);box->addWidget(new ChromeTitle(dialog,"Advanced Transform"));auto scroll=new QScrollArea;scroll->setObjectName("sceneTransformScroll");scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);auto body=new QWidget;auto form=new QFormLayout(body);form->setRowWrapPolicy(QFormLayout::WrapLongRows);QHash<QString,QDoubleSpinBox*> fields;
    const double values[]={v->x,v->y,v->width,v->height,v->scale,v->rotation,v->cropLeft,v->cropTop,v->cropRight,v->cropBottom};int index=0;
    for(auto name:{"X","Y","Width","Height","Scale","Rotation","Crop left","Crop top","Crop right","Crop bottom"}){auto field=new QDoubleSpinBox;QString key=name;field->setObjectName("sceneTransform"+key.remove(' '));field->setDecimals(2);field->setKeyboardTracking(false);field->setRange(QString(name)=="X"||QString(name)=="Y"?-100000:QString(name)=="Rotation"?-36000:QString(name)=="Scale"?.01:QString(name)=="Width"||QString(name)=="Height"?1:0,QString(name)=="Scale"?100:QString(name)=="Width"||QString(name)=="Height"?16384:100000);field->setValue(values[index++]);form->addRow(name,field);fields[name]=field;}
    auto aspect=new QCheckBox("Preserve source aspect ratio and constrain resize handles");aspect->setChecked(v->aspectLocked);form->addRow(aspect);auto error=new QLabel("Scene pixels; rotation in degrees. Apply validates and saves automatically.");error->setWordWrap(true);form->addRow(error);scroll->setWidget(body);box->addWidget(scroll,1);auto input=new ScrollInput(scroll);for(auto child:body->findChildren<QWidget*>())child->installEventFilter(input);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close);box->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,dialog,[this,id,sceneId,fields,aspect,error]{auto current=model_->visual(id);if(!current||current->locked||model_->data().selected!=sceneId){error->setText("Source unavailable, locked, or scene changed. Reopen the dialog.");return;}auto copy=*current;copy.x=fields["X"]->value();copy.y=fields["Y"]->value();copy.width=fields["Width"]->value();copy.height=fields["Height"]->value();copy.scale=fields["Scale"]->value();copy.rotation=fields["Rotation"]->value();copy.cropLeft=fields["Crop left"]->value();copy.cropTop=fields["Crop top"]->value();copy.cropRight=fields["Crop right"]->value();copy.cropBottom=fields["Crop bottom"]->value();copy.aspectLocked=aspect->isChecked();error->setText(model_->updateVisual(copy)?"Applied · saving automatically":"Invalid dimensions / empty crop; last valid transform kept.");});
    backdrop->update(window()->property("applicationTransparencyEnabled").isValid()?window()->property("applicationTransparencyEnabled").toBool():true);dialog->open();
}
void Page::audioRows() {
    while(auto item=audioLayout_->takeAt(0)){if(auto w=item->widget()){w->hide();w->deleteLater();}delete item;}
    auto s=model_->current();if(!s)return;
    if(s->audio.isEmpty()){auto l=new QLabel("No audio assigned. Add microphone, desktop or application audio.");l->setWordWrap(true);audioLayout_->addWidget(l);}
    for(auto a:s->audio)if(auto d=model_->definition(a.sourceId)) {
        auto row=new QWidget;row->setObjectName("sceneAudioRow_"+a.id);auto box=new QHBoxLayout(row);box->setContentsMargins(0,1,0,1);auto name=new QLabel(d->name+"\n"+association(*d));name->setWordWrap(true);name->setMinimumWidth(120);box->addWidget(name,2);
        auto gain=new QSlider(Qt::Horizontal);gain->setObjectName("sceneAudioGain_"+a.id);gain->setRange(0,200);gain->setValue(int(std::lround(a.gain*100)));gain->setAccessibleName(d->name+" gain");box->addWidget(gain,2);auto value=new QLabel(QString::number(gain->value())+"%");value->setMinimumWidth(38);box->addWidget(value);
        auto mute=button("Mute","sceneAudioMute_"+a.id,box);mute->setCheckable(true);mute->setChecked(a.muted);auto meter=new QLabel("Idle · opens during recording/replay");meter->setObjectName("sceneAudioState_"+a.id);meter->setWordWrap(true);meter->setStyleSheet("font-size:11px;");box->addWidget(meter);
        auto properties=button("…","sceneAudioProperties_"+a.id,box);properties->setToolTip("Shared source/device properties");auto remove=button("−","sceneAudioRemove_"+a.id,box);remove->setToolTip("Remove audio assignment");audioLayout_->addWidget(row);
        connect(gain,&QSlider::valueChanged,this,[this,id=a.id,value](int n){value->setText(QString::number(n)+"%");if(auto s=model_->current())for(auto& audio:s->audio)if(audio.id==id){auto copy=audio;copy.gain=n/100.;model_->updateAudio(copy);break;}});
        connect(mute,&QPushButton::toggled,this,[this,id=a.id](bool muted){if(auto s=model_->current())for(auto& audio:s->audio)if(audio.id==id){auto copy=audio;copy.muted=muted;model_->updateAudio(copy);break;}});
        connect(properties,&QPushButton::clicked,this,[this,id=d->id,kind=d->kind]{configure(kind,id);});connect(remove,&QPushButton::clicked,this,[this,id=a.id]{confirmRemove(this,"Remove this audio assignment? Other scenes are unchanged.",[this,id]{model_->removeAudio(id);});});
    }audioLayout_->addStretch();
}
void Page::addMenu(bool audio,QWidget* anchor) {
    auto menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);auto addType=[this,menu](Kind kind){auto label=kindName(kind);menu->addAction(label,this,[this,kind]{configure(kind);});};
    if(audio){addType(Kind::Microphone);addType(Kind::DesktopAudio);addType(Kind::ApplicationAudio);}else for(auto kind:{Kind::Game,Kind::Window,Kind::Display,Kind::Camera,Kind::Image,Kind::Text})addType(kind);
    auto existing=menu->addMenu("Reuse shared source");for(auto& d:model_->data().definitions)if(isAudio(d.kind)==audio){auto id=d.id;existing->addAction(d.name,this,[this,id,audio]{if(audio)model_->addAudio(id);else model_->addVisual(id);});}existing->setEnabled(!existing->actions().isEmpty());
    connect(menu,&QMenu::aboutToHide,anchor,[anchor]{anchor->setFocus(Qt::PopupFocusReason);});menu->popup(anchor->mapToGlobal(QPoint(0,anchor->height())));
}
void Page::sourceProperties() { if(auto v=model_->visual(model_->selection()))if(auto d=model_->definition(v->sourceId))configure(d->kind,d->id); }
void Page::configure(Kind kind,const QString& existing) {
    const auto destination=model_->data().selected;
    Definition value;if(auto d=model_->definition(existing))value=*d;else {value.kind=kind;value.name=kindName(kind);value.settings["text"]="ReDot";value.settings["fontSize"]=64;value.intrinsic=kind==Kind::Text?QSizeF(640,160):QSizeF(1920,1080);}
    auto dialog=new QDialog(this);dialog->setObjectName("sceneSourceDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);DesktopBackdrop::prepare(dialog);auto backdrop=new DesktopBackdrop(dialog);dialog->setWindowFlag(Qt::Window,true);dialog->setWindowFlag(Qt::FramelessWindowHint,true);dialog->setWindowTitle("ReDot · Source properties");dialog->resize(530,470);dialog->setMinimumSize(420,350);new WorkspaceGlassLayer(dialog);
    auto outer=new QVBoxLayout(dialog);outer->setContentsMargins(10,0,10,12);auto caption=new ChromeTitle(dialog,"Source · "+kindName(kind));outer->addWidget(caption);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto body=new QWidget;auto form=new QFormLayout(body);form->setRowWrapPolicy(QFormLayout::WrapLongRows);auto name=new QLineEdit(value.name);name->setObjectName("sceneDefinitionName");name->setMaxLength(128);form->addRow("Name (shared)",name);
    auto stage=new QLabel("Sources feed the selected scene compositor. Unavailable sources stay blank; other sources continue.");stage->setWordWrap(true);form->addRow(stage);
    QLineEdit *path=nullptr,*target=nullptr,*executable=nullptr,*color=nullptr,*family=nullptr;QTextEdit* text=nullptr;QSpinBox* font=nullptr;QComboBox *device=nullptr,*videoDevice=nullptr,*alignment=nullptr;QDoubleSpinBox* sync=nullptr;ApplicationPicker* applications=nullptr;
    if(kind==Kind::Text){text=new QTextEdit(value.settings["text"].toString("ReDot"));text->setMaximumHeight(130);form->addRow("Text",text);font=new QSpinBox;font->setRange(8,400);font->setValue(value.settings["fontSize"].toInt(64));form->addRow("Font size (px)",font);color=new QLineEdit(value.settings["color"].toString("#F5F0FF"));form->addRow("Text color",color);}
    else if(kind==Kind::Image){auto row=new QWidget;auto r=new QHBoxLayout(row);r->setContentsMargins(0,0,0,0);path=new QLineEdit(value.settings["path"].toString());auto browse=button("Browse…","sceneImageBrowse",r);r->insertWidget(0,path,1);form->addRow("Image",row);connect(browse,&QPushButton::clicked,dialog,[dialog,path]{auto f=QFileDialog::getOpenFileName(dialog,"Choose image",{},"Images (*.png *.jpg *.jpeg *.bmp *.webp)");if(!f.isEmpty())path->setText(f);});}
    else if(kind==Kind::Microphone||kind==Kind::DesktopAudio) {
        device=new QComboBox;device->setObjectName("sceneAudioDevice");device->addItem(kind==Kind::Microphone?"Windows default recording input (eConsole)":"Windows default playback output (eConsole)",QString{});
        auto pinned=value.settings["deviceId"].toString();if(!pinned.isEmpty()){device->addItem(value.settings["deviceName"].toString("Saved endpoint (not enumerated)"),pinned);device->setCurrentIndex(1);}form->addRow("Device association",device);
        auto devicesStatus=new QLabel("Click Refresh to discover endpoints. No endpoint is opened.");devicesStatus->setWordWrap(true);form->addRow(devicesStatus);auto refresh=new QPushButton("Refresh devices");form->addRow(refresh);
        // Reuse the existing bounded catalog. Enumeration is off the UI thread;
        // it never captures samples and never picks an alternative endpoint.
        auto catalog=std::make_shared<AudioDeviceCatalog>();auto timer=new QTimer(dialog);timer->setInterval(100);
        connect(refresh,&QPushButton::clicked,dialog,[catalog,timer,devicesStatus]{catalog->request();timer->start();devicesStatus->setText("Loading devices…");});
        connect(timer,&QTimer::timeout,dialog,[catalog,timer,device,devicesStatus,kind]{AudioDeviceCatalog::Result result;if(!catalog->take(result))return;timer->stop();auto selected=device->currentData().toString();QSignalBlocker block(device);while(device->count()>1)device->removeItem(1);
            for(auto& item:kind==Kind::Microphone?result.inputs:result.outputs)device->addItem(QString::fromStdWString(item.name),QString::fromStdWString(item.id));
            int index=device->findData(selected);if(index<0&&!selected.isEmpty()){device->addItem("Selected endpoint unavailable",selected);index=device->count()-1;}device->setCurrentIndex(std::max(0,index));auto err=kind==Kind::Microphone?result.inputError:result.outputError;devicesStatus->setText(err.empty()?"Endpoint list updated · selection used by this scene":QString::fromStdWString(err));});
        connect(dialog,&QDialog::finished,dialog,[catalog,timer]{timer->stop();catalog->cancel();});
        if(kind==Kind::DesktopAudio){auto note=new QLabel("Whole-system audio is explicitly selected for this definition; never a fallback.");note->setWordWrap(true);form->addRow(note);}
    } else if(kind==Kind::Game||kind==Kind::Window||kind==Kind::ApplicationAudio){
        applications=new ApplicationPicker(value.settings);form->addRow(applications);dialog->resize(640,600);
        connect(applications,&ApplicationPicker::selectionChanged,dialog,[applications,name,existing]{if(existing.isEmpty()&&!name->isModified())if(auto selected=applications->selected())name->setText(selected->title.trimmed().left(128));});
    } else if(kind==Kind::Display||kind==Kind::Camera){
        videoDevice=new QComboBox;auto saved=value.settings["target"].toString();if(!saved.isEmpty())videoDevice->addItem(value.settings["deviceName"].toString("Saved device"),saved);
        form->addRow(kind==Kind::Display?"Monitor":"Camera",videoDevice);auto refresh=new QPushButton("Refresh devices");form->addRow(refresh);auto resultLabel=new QLabel("Discovering devices…");resultLabel->setWordWrap(true);form->addRow(resultLabel);
        auto watcher=new QFutureWatcher<QPair<QVector<Device>,QString>>(dialog);
        connect(refresh,&QPushButton::clicked,dialog,[watcher,kind,resultLabel]{if(watcher->isRunning())return;resultLabel->setText("Discovering devices…");watcher->setFuture(QtConcurrent::run([kind]{QString error;return qMakePair(kind==Kind::Display?displays():cameras(error),error);}));});
        connect(watcher,&QFutureWatcherBase::finished,dialog,[watcher,videoDevice,resultLabel]{auto result=watcher->result();auto selected=videoDevice->currentData().toString();QSignalBlocker block(videoDevice);videoDevice->clear();for(auto& d:result.first)videoDevice->addItem(d.name,d.id);auto index=videoDevice->findData(selected);if(index<0&&!selected.isEmpty()){videoDevice->addItem("Selected device unavailable",selected);index=videoDevice->count()-1;}videoDevice->setCurrentIndex(index>=0?index:videoDevice->count()?0:-1);resultLabel->setText(result.second.isEmpty()?QString("%1 device(s). Exact selection is retained if unavailable.").arg(result.first.size()):result.second);});refresh->click();
    }
    if(kind==Kind::Text){family=new QLineEdit(value.settings["font"].toString("Segoe UI"));form->addRow("Font family",family);alignment=new QComboBox;alignment->addItems({"left","center","right"});alignment->setCurrentText(value.settings["alignment"].toString("left"));form->addRow("Alignment",alignment);}
    if(isAudio(kind)){sync=new QDoubleSpinBox;sync->setRange(-1000,1000);sync->setSuffix(" ms");double adjustment=0;if(auto s=model_->current())for(auto& a:s->audio)if(a.sourceId==existing)adjustment=a.syncMs;sync->setValue(adjustment);form->addRow("Scene sync",sync);auto n=new QLabel("Positive delays, negative advances. Sync/device/track changes apply at the next encoding session. Gain and mute apply live.");n->setWordWrap(true);form->addRow(n);}
    auto error=new QLabel;error->setWordWrap(true);form->addRow(error);scroll->setWidget(body);outer->addWidget(scroll,1);auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);outer->addWidget(buttons);
    if(applications&&existing.isEmpty())buttons->button(QDialogButtonBox::Save)->setText("Add Source");
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,value,name,path,target,executable,text,font,color,family,alignment,device,videoDevice,sync,error,applications,destination]()mutable {
        if(model_->data().selected!=destination){error->setText("Scene changed. Close this dialog and add to the intended scene.");return;}
        value.name=name->text().trimmed();if(value.name.isEmpty()){error->setText("Enter a source name.");return;}
        if(path){value.settings["path"]=path->text();if(path->text().isEmpty()){error->setText("Select an image path.");return;}}
        if(applications){auto selected=applications->selected();if(!selected){if(value.id.isEmpty()||value.settings["executable"].toString().isEmpty()){error->setText("Select a running application first.");return;}}else {if(!applicationStillAvailable(*selected)){error->setText("This application closed or changed. Refresh and select again.");applications->refresh();return;}if(!isAudio(value.kind)&&selected->clientSize.isValid())value.intrinsic=selected->clientSize;auto identity=selected->identity();for(auto it=identity.begin();it!=identity.end();++it)value.settings[it.key()]=it.value();}}
        if(target)value.settings["target"]=target->text();if(executable)value.settings["executable"]=executable->text();if(text){if(text->toPlainText().size()>8192){error->setText("Text is limited to 8192 characters.");return;}if(!QColor(color->text()).isValid()){error->setText("Enter a valid color, such as #F5F0FF.");return;}value.settings["text"]=text->toPlainText();value.settings["fontSize"]=font->value();value.settings["color"]=color->text();}
        if(device){value.settings["deviceId"]=device->currentData().toString();value.settings["deviceName"]=device->currentText();}
        if(videoDevice){if(videoDevice->currentIndex()<0){error->setText("Select an available device.");return;}value.settings["target"]=videoDevice->currentData().toString();value.settings["deviceName"]=videoDevice->currentText();}
        if(family)value.settings["font"]=family->text();if(alignment)value.settings["alignment"]=alignment->currentText();
        QString sourceId=value.id;
        if(value.id.isEmpty()){auto scene=model_->current();if(!scene||(isAudio(value.kind)?scene->audio.size()>=16:scene->visuals.size()>=64)){error->setText("This scene has reached its source limit.");return;}sourceId=model_->addDefinition(value.kind,value.name,value.settings,value.intrinsic);if(sourceId.isEmpty()){error->setText("Source limit reached or configuration is read-only.");return;}if(isAudio(value.kind))model_->addAudio(sourceId);else model_->addVisual(sourceId);}
        else if(!model_->updateDefinition(value)){error->setText("Source changes could not be saved.");return;}
        if(sync)if(auto s=model_->current())for(auto a:s->audio)if(a.sourceId==sourceId){a.syncMs=sync->value();model_->updateAudio(a);break;}
        dialog->accept();
    });
    backdrop->update(window()->property("applicationTransparencyEnabled").isValid()?window()->property("applicationTransparencyEnabled").toBool():true);dialog->open();
}
}
