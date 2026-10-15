#include "scene_model.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace Scenes {
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
Snapshot initial() { Scene s; s.id=uuid();s.name="Scene 1";return {{s},{},s.id}; }
bool number(double n,double low,double high) { return std::isfinite(n)&&n>=low&&n<=high; }
bool validVisual(const Visual& v) {
    return number(v.x,-100000,100000)&&number(v.y,-100000,100000)&&number(v.width,1,16384)&&number(v.height,1,16384)
        &&number(v.scale,.01,100)&&number(v.rotation,-36000,36000)
        &&number(v.cropLeft,0,v.width)&&number(v.cropRight,0,v.width)&&v.cropLeft+v.cropRight<v.width
        &&number(v.cropTop,0,v.height)&&number(v.cropBottom,0,v.height)&&v.cropTop+v.cropBottom<v.height;
}
bool validAudio(const Audio& a) { return number(a.gain,0,2)&&number(a.syncMs,-1000,1000); }
QJsonObject toJson(const Visual& v) {
    return {{"id",v.id},{"source",v.sourceId},{"x",v.x},{"y",v.y},{"width",v.width},{"height",v.height},
        {"scale",v.scale},{"rotation",v.rotation},{"crop",QJsonArray{v.cropLeft,v.cropTop,v.cropRight,v.cropBottom}},
        {"visible",v.visible},{"locked",v.locked},{"aspectLocked",v.aspectLocked}};
}
bool identifier(const QString& id) { return !QUuid(id).isNull(); }
bool write(const QString& path,const QByteArray& bytes, QString& error) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) {error="Cannot create scene configuration folder.";return false;}
    QSaveFile f(path);f.setDirectWriteFallback(false);
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()) {error="Scene save failed: "+f.errorString();return false;}
    return true;
}
}
QString kindName(Kind kind) {
    static const QStringList names={"Game Capture","Window Capture","Display Capture","Video Capture Device","Image","Text","Microphone","Desktop Audio","Application Audio"};
    return names.at(int(kind));
}
bool isAudio(Kind kind) { return kind>=Kind::Microphone; }
QTransform Visual::transform() const { QTransform t;t.translate(x,y);t.rotate(rotation);t.scale(scale,scale);return t; }
QString configurationPath() {
    auto root=qEnvironmentVariable("LOCALAPPDATA");
    return QDir(root).filePath("PandaClip/scenes-v1.json");
}
QByteArray encode(const Snapshot& value) {
    QJsonArray defs, scenes;
    for(auto& d:value.definitions)defs.append(QJsonObject{{"id",d.id},{"name",d.name},{"kind",int(d.kind)},{"settings",d.settings},
        {"intrinsicWidth",d.intrinsic.width()},{"intrinsicHeight",d.intrinsic.height()}});
    for(auto& s:value.scenes) {
        QJsonArray visuals,audio;for(auto& v:s.visuals)visuals.append(toJson(v));
        for(auto& a:s.audio)audio.append(QJsonObject{{"id",a.id},{"source",a.sourceId},{"gain",a.gain},{"muted",a.muted},{"syncMs",a.syncMs},{"monitoring",a.monitoring}});
        scenes.append(QJsonObject{{"id",s.id},{"name",s.name},{"width",s.width},{"height",s.height},{"visuals",visuals},{"audio",audio}});
    }
    return QJsonDocument(QJsonObject{{"format","PandaClipScenes"},{"version",1},{"migrationVersion",value.migrationVersion},{"selected",value.selected},{"definitions",defs},{"scenes",scenes}}).toJson();
}
bool decode(const QByteArray& bytes,Snapshot& value,QString& error) {
    auto fail=[&](const QString& why){error=why;return false;};
    if(bytes.size()>4*1024*1024)return fail("Scene configuration exceeds the 4 MiB safety limit.");
    QJsonParseError parse;auto doc=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject())return fail("Invalid scene JSON: "+parse.errorString());
    auto root=doc.object();if(root["format"]!="PandaClipScenes")return fail("Unknown scene configuration format.");
    if(root["version"].toInt(-1)!=1)return fail("Unsupported scene configuration version; file preserved read-only.");
    if(!root["definitions"].isArray()||!root["scenes"].isArray())return fail("Missing scene arrays.");
    auto defs=root["definitions"].toArray(),scenes=root["scenes"].toArray();
    if(defs.size()>256||scenes.isEmpty()||scenes.size()>64)return fail("Invalid scene/source count.");
    Snapshot result;QSet<QString> ids;QMap<QString,Kind> kinds;
    auto newId=[&](const QString& id){if(!identifier(id)||ids.contains(id))return false;ids.insert(id);return true;};
    for(auto e:defs) {
        auto j=e.toObject();Definition d;d.id=j["id"].toString();d.name=j["name"].toString();int kind=j["kind"].toInt(-1);
        if(!newId(d.id)||d.name.trimmed().isEmpty()||d.name.size()>128||kind<0||kind>8||!j["settings"].isObject())return fail("Invalid source definition.");
        d.kind=Kind(kind);d.settings=j["settings"].toObject();d.intrinsic={j["intrinsicWidth"].toDouble(),j["intrinsicHeight"].toDouble()};
        if(QJsonDocument(d.settings).toJson().size()>32768)return fail("Source properties exceed the 32 KiB safety limit.");
        if(!number(d.intrinsic.width(),1,16384)||!number(d.intrinsic.height(),1,16384))return fail("Invalid source dimensions.");
        kinds[d.id]=d.kind;result.definitions.append(d);
    }
    for(auto e:scenes) {
        auto j=e.toObject();Scene s;s.id=j["id"].toString();s.name=j["name"].toString();s.width=j["width"].toInt();s.height=j["height"].toInt();
        if(!newId(s.id)||s.name.trimmed().isEmpty()||s.name.size()>128||s.width<320||s.width>7680||s.height<180||s.height>4320||s.width*9!=s.height*16)return fail("Invalid scene identity or 16:9 dimensions.");
        if(!j["visuals"].isArray()||!j["audio"].isArray()||j["visuals"].toArray().size()>64||j["audio"].toArray().size()>16)return fail("Invalid scene instances.");
        for(auto e2:j["visuals"].toArray()) {
            auto vj=e2.toObject();Visual v;v.id=vj["id"].toString();v.sourceId=vj["source"].toString();
            v.x=vj["x"].toDouble(NAN);v.y=vj["y"].toDouble(NAN);v.width=vj["width"].toDouble(NAN);v.height=vj["height"].toDouble(NAN);
            v.scale=vj["scale"].toDouble(NAN);v.rotation=vj["rotation"].toDouble(NAN);auto crop=vj["crop"].toArray();
            if(crop.size()!=4)return fail("Missing crop coordinates.");
            v.cropLeft=crop[0].toDouble(NAN);v.cropTop=crop[1].toDouble(NAN);v.cropRight=crop[2].toDouble(NAN);v.cropBottom=crop[3].toDouble(NAN);
            if(!vj["visible"].isBool()||!vj["locked"].isBool()||!vj["aspectLocked"].isBool())return fail("Missing source flags.");
            v.visible=vj["visible"].toBool();v.locked=vj["locked"].toBool();v.aspectLocked=vj["aspectLocked"].toBool();
            if(!newId(v.id)||!kinds.contains(v.sourceId)||isAudio(kinds[v.sourceId])||!validVisual(v))return fail("Invalid visual transform or source reference.");
            s.visuals.append(v);
        }
        for(auto e2:j["audio"].toArray()) {
            auto aj=e2.toObject();Audio a;a.id=aj["id"].toString();a.sourceId=aj["source"].toString();a.gain=aj["gain"].toDouble(NAN);
            a.syncMs=aj["syncMs"].toDouble(NAN);a.muted=aj["muted"].toBool();a.monitoring=aj["monitoring"].toBool();
            if(!newId(a.id)||!kinds.contains(a.sourceId)||!isAudio(kinds[a.sourceId])||!validAudio(a)||!aj["muted"].isBool()||!aj["monitoring"].isBool())return fail("Invalid audio assignment.");
            for(auto& existing:s.audio)if(existing.sourceId==a.sourceId)return fail("Duplicate audio source assignment.");
            s.audio.append(a);
        }
        result.scenes.append(s);
    }
    result.selected=root["selected"].toString();
    result.migrationVersion=root["migrationVersion"].toInt();
    if(std::none_of(result.scenes.begin(),result.scenes.end(),[&](auto& s){return s.id==result.selected;}))return fail("Selected scene does not exist.");
    value=std::move(result);return true;
}
Model::Model(QString path,QObject* parent):QObject(parent),data_(initial()),path_(std::move(path)) {
    debounce_.setSingleShot(true);debounce_.setInterval(350);
    connect(&debounce_,&QTimer::timeout,this,&Model::enqueueSave);
    worker_=std::thread([this]{work();});
}
Model::~Model() {
    if(debounce_.isActive())enqueueSave();
    {std::lock_guard lock(mutex_);stopping_=true;}wake_.notify_one();worker_.join();
}
bool Model::idle() const { std::lock_guard lock(mutex_);return !working_&&!pending_&&!debounce_.isActive(); }
void Model::flush() { if(debounce_.isActive()){debounce_.stop();enqueueSave();} }
void Model::migrateCapture(const QJsonObject& previous) {
    if(!writable()||data_.migrationVersion>=2)return;
    auto number=[&](const char* key,int fallback){bool ok{};auto n=previous[key].toString().toInt(&ok);return ok?n:fallback;};
    auto savedSelection=data_.selected;bool existing=false;for(auto& s:data_.scenes)if(!s.visuals.isEmpty()||!s.audio.isEmpty())existing=true;
    bool hasTarget=!previous["sourceExe"].toString().isEmpty();
    if(!previous.isEmpty()){
        int count=int(hasTarget)+int(number("micEnabled",0)!=0)+int(number("gameEnabled",1)&&number("gameMode",1)!=0&&(hasTarget||number("gameMode",1)==2));
        if((existing&&data_.scenes.size()>=64)||data_.definitions.size()+count>256){status_="Capture migration blocked by scene/source limits. Existing scenes and capture preferences are preserved; remove unused definitions and relaunch.";Q_EMIT statusChanged();return;}
        data_.migrationVersion=2; // Mark before emitting changed from individual migration edits.
        if(existing)create("Migrated Capture");else if(auto s=editScene())s->name="Gameplay";
        if(hasTarget){auto id=addDefinition(Kind::Game,previous["sourceTitle"].toString("Previous game"),{{"executable",previous["sourceExe"]},{"target",previous["sourceTitle"]}},QSizeF(1920,1080));addVisual(id);}
        auto add=[&](Kind kind,QString name,QJsonObject settings,const char* gain,const char* mute,const char* sync){auto id=addDefinition(kind,name,settings);auto instance=addAudio(id);if(auto s=current())for(auto a:s->audio)if(a.id==instance){a.gain=number(gain,100)/100.;a.muted=number(mute,0)!=0;a.syncMs=number(sync,0);updateAudio(a);break;}};
        if(number("gameEnabled",1)&&number("gameMode",1)!=0){if(number("gameMode",1)==2)add(Kind::DesktopAudio,"Desktop Audio",{{"deviceId",previous["outputId"]}},"gameGain","gameMute","gameSync");else if(hasTarget)add(Kind::ApplicationAudio,"Game audio",{{"executable",previous["sourceExe"]},{"target",previous["sourceTitle"]}},"gameGain","gameMute","gameSync");}
        if(number("micEnabled",0))add(Kind::Microphone,"Microphone",{{"deviceId",previous["micId"]}},"micGain","micMute","micSync");
        if(existing)data_.selected=savedSelection;
    }
    data_.migrationVersion=2;dirty();
    status_=hasTarget?"Previous capture settings migrated. Unavailable targets remain blank; select sources in Scenes.":"Scenes active. Add/select your capture source in Scenes; no previous target could be resolved.";Q_EMIT statusChanged();
}
void Model::enqueueSave() {
    if(!writable())return;
    auto bytes=encode(data_);
    if(bytes.size()>4*1024*1024){status_="Scene configuration exceeds 4 MiB; reduce source properties before saving.";Q_EMIT statusChanged();return;}
    {std::lock_guard lock(mutex_);pending_=std::move(bytes);}
    status_="Saving scenes…";Q_EMIT statusChanged();wake_.notify_one();
}
void Model::work() {
    Snapshot loaded;QString error;QFile file(path_);bool corrupt=false,future=false,exists=file.exists();
    if(exists) {
        if(!file.open(QIODevice::ReadOnly)) {error="Cannot read scene configuration: "+file.errorString();future=true;}
        else {auto bytes=file.read(4*1024*1024+1);if(!decode(bytes,loaded,error)){corrupt=true;future=error.startsWith("Unsupported")||error.startsWith("Unknown");}}
    }
    file.close();
    auto initialValue=exists&&!corrupt&&!future?loaded:initial();
    // Persist the first scene's identity even when the user has not edited it.
    // This is an atomic background write; corrupt/unknown existing files are
    // never replaced merely by launching the application.
    if(!exists)write(path_,encode(initialValue),error);
    QMetaObject::invokeMethod(this,[this,value=std::move(initialValue),error,future,exists]()mutable{
        data_=std::move(value);ready_=true;readOnly_=future;
        status_=error.isEmpty()?"Scenes ready":error+(exists?" Default scene loaded; original file preserved.":" Default scene is in memory; save is unavailable.");
        Q_EMIT changed();Q_EMIT statusChanged();
    },Qt::QueuedConnection);
    {std::lock_guard lock(mutex_);working_=false;}
    for(;;) {
        QByteArray bytes;{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||pending_;});
            if(!pending_&&stopping_)break;bytes=std::move(*pending_);pending_.reset();working_=true;}
        QString failure;
        if(corrupt) {
            // Archive only on the first explicit edit. Loading never overwrites
            // broken user data. A failed preservation blocks the replacement.
            const auto backup=path_+".corrupt-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmsszzz")+"-"+uuid()+".json";
            if(!QFile::copy(path_,backup))failure="Cannot preserve corrupt scene file; changes were not saved.";
            else corrupt=false;
        }
        if(failure.isEmpty())write(path_,bytes,failure);
        QMetaObject::invokeMethod(this,[this,failure]{status_=failure.isEmpty()?"Scenes saved":failure;Q_EMIT statusChanged();},Qt::QueuedConnection);
        {std::lock_guard lock(mutex_);working_=false;}
    }
}
const Scene* Model::current() const { for(auto& s:data_.scenes)if(s.id==data_.selected)return &s;return nullptr; }
Scene* Model::editScene() { for(auto& s:data_.scenes)if(s.id==data_.selected)return &s;return nullptr; }
const Definition* Model::definition(const QString& id) const { for(auto& d:data_.definitions)if(d.id==id)return &d;return nullptr; }
const Visual* Model::visual(const QString& id) const { if(auto s=current())for(auto& v:s->visuals)if(v.id==id)return &v;return nullptr; }
void Model::dirty(bool structural) { debounce_.start();if(structural)Q_EMIT changed();else Q_EMIT visualChanged(); }
QString Model::create(QString name) { if(!writable()||data_.scenes.size()>=64)return {};Scene s;s.id=uuid();s.name=name.trimmed().left(128);if(s.name.isEmpty())s.name=QString("Scene %1").arg(data_.scenes.size()+1);data_.scenes.append(s);data_.selected=s.id;selection_.clear();dirty();return s.id; }
bool Model::rename(const QString& id,QString name) { name=name.trimmed().left(128);if(!writable()||name.isEmpty())return false;for(auto& s:data_.scenes)if(s.id==id){s.name=name;dirty();return true;}return false; }
QString Model::duplicate(const QString& id) { if(!writable()||data_.scenes.size()>=64)return {};for(auto& s:data_.scenes)if(s.id==id){auto copy=s;copy.id=uuid();copy.name=(s.name+" copy").left(128);for(auto& v:copy.visuals)v.id=uuid();for(auto& a:copy.audio)a.id=uuid();data_.selected=copy.id;selection_.clear();data_.scenes.append(copy);dirty();return copy.id;}return {}; }
bool Model::remove(const QString& id) { if(!writable()||data_.scenes.size()==1)return false;for(int i=0;i<data_.scenes.size();++i)if(data_.scenes[i].id==id){data_.scenes.removeAt(i);if(data_.selected==id)data_.selected=data_.scenes[std::min(i,int(data_.scenes.size())-1)].id;selection_.clear();dirty();return true;}return false; }
bool Model::select(const QString& id) { if(!writable())return false;for(auto& s:data_.scenes)if(s.id==id){if(data_.selected==id)return true;data_.selected=id;selection_.clear();dirty();return true;}return false; }
bool Model::reorder(const QString& id,int index) { if(!writable()||index<0||index>=data_.scenes.size())return false;for(int i=0;i<data_.scenes.size();++i)if(data_.scenes[i].id==id){data_.scenes.move(i,index);dirty();return true;}return false; }
void Model::selectVisual(const QString& id) { auto v=visual(id);auto next=v?id:QString{};if(next!=selection_){selection_=next;Q_EMIT selectionChanged();} }
QString Model::addDefinition(Kind kind,QString name,QJsonObject settings,QSizeF intrinsic) {
    if(!writable()||data_.definitions.size()>=256||int(kind)<0||int(kind)>8||QJsonDocument(settings).toJson().size()>32768||!number(intrinsic.width(),1,16384)||!number(intrinsic.height(),1,16384))return {};
    Definition d;d.id=uuid();d.name=name.trimmed().left(128);if(d.name.isEmpty())d.name=kindName(kind);d.kind=kind;d.settings=std::move(settings);d.intrinsic=intrinsic;data_.definitions.append(d);dirty();return d.id;
}
bool Model::updateDefinition(const Definition& value) { if(!writable()||value.name.trimmed().isEmpty()||value.name.size()>128||!number(value.intrinsic.width(),1,16384)||!number(value.intrinsic.height(),1,16384)||QJsonDocument(value.settings).toJson().size()>32768)return false;for(auto& d:data_.definitions)if(d.id==value.id&&d.kind==value.kind){d=value;dirty();return true;}return false; }
QString Model::addVisual(const QString& definitionId) {
    if(!writable())return {};auto d=definition(definitionId);auto s=editScene();if(!d||!s||isAudio(d->kind)||s->visuals.size()>=64)return {};
    Visual v;v.id=uuid();v.sourceId=definitionId;v.width=d->intrinsic.width();v.height=d->intrinsic.height();
    auto fitScale=std::min({1.0,s->width/v.width,s->height/v.height});v.width*=fitScale;v.height*=fitScale;v.x=(s->width-v.width)/2;v.y=(s->height-v.height)/2;
    s->visuals.append(v);selection_=v.id;dirty();return v.id;
}
bool Model::updateVisual(const Visual& value) { if(!writable()||!validVisual(value))return false;if(auto s=editScene())for(auto& v:s->visuals)if(v.id==value.id&&v.sourceId==value.sourceId){v=value;dirty(false);return true;}return false; }
bool Model::removeVisual(const QString& id) { if(!writable())return false;if(auto s=editScene())for(int i=0;i<s->visuals.size();++i)if(s->visuals[i].id==id){s->visuals.removeAt(i);if(selection_==id)selection_.clear();dirty();return true;}return false; }
bool Model::moveVisual(const QString& id,int index) { if(!writable())return false;if(auto s=editScene()){if(index<0||index>=s->visuals.size())return false;for(int i=0;i<s->visuals.size();++i)if(s->visuals[i].id==id){s->visuals.move(i,index);dirty();return true;}}return false; }
bool Model::center(const QString& id) { if(auto v=visual(id);v&&!v->locked){auto copy=*v;auto bounds=copy.transform().mapRect(copy.localRect().adjusted(copy.cropLeft,copy.cropTop,-copy.cropRight,-copy.cropBottom));auto s=current();copy.x+=s->width/2.-bounds.center().x();copy.y+=s->height/2.-bounds.center().y();return updateVisual(copy);}return false; }
bool Model::fit(const QString& id) { if(auto v=visual(id);v&&!v->locked){auto copy=*v;auto s=current();copy.rotation=0;copy.scale=1;copy.aspectLocked=true;auto d=definition(copy.sourceId);double ratio=d?d->intrinsic.width()/d->intrinsic.height():copy.width/copy.height;copy.width=std::min(double(s->width),s->height*ratio);copy.height=copy.width/ratio;copy.x=(s->width-copy.width)/2;copy.y=(s->height-copy.height)/2;copy.cropLeft=copy.cropTop=copy.cropRight=copy.cropBottom=0;return updateVisual(copy);}return false; }
bool Model::setResolution(int width,int height) { if(!writable()||width<320||width>7680||height<180||height>4320||width*9!=height*16)return false;if(auto s=editScene()){s->width=width;s->height=height;dirty();return true;}return false; }
QString Model::addAudio(const QString& definitionId) { if(!writable())return {};auto d=definition(definitionId);auto s=editScene();if(!d||!s||!isAudio(d->kind)||s->audio.size()>=16)return {};for(auto& a:s->audio)if(a.sourceId==definitionId)return {};Audio a;a.id=uuid();a.sourceId=definitionId;s->audio.append(a);dirty();return a.id; }
bool Model::updateAudio(const Audio& value) { if(!writable()||!validAudio(value))return false;if(auto s=editScene())for(auto& a:s->audio)if(a.id==value.id&&a.sourceId==value.sourceId){a=value;dirty(false);return true;}return false; }
bool Model::removeAudio(const QString& id) { if(!writable())return false;if(auto s=editScene())for(int i=0;i<s->audio.size();++i)if(s->audio[i].id==id){s->audio.removeAt(i);dirty();return true;}return false; }
}
