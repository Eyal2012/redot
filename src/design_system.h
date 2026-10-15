#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QQmlContext>
#include <QQmlEngine>
namespace DesignSystem {
inline const QVariantMap& values(){
    static const auto data=[] {QFile f(":/PandaClip/design-system.json");if(!f.open(QIODevice::ReadOnly))return QVariantMap{};return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();}();
    return data;
}
inline QColor color(const char* key){return QColor(values().value(key).toString());}
inline QString rgba(const char* key){auto c=color(key);return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());}
inline void bind(QQmlEngine* engine){engine->rootContext()->setContextProperty("Design",values());}
inline QString style(){QFile f(":/PandaClip/controls.qss");if(!f.open(QIODevice::ReadOnly))return {};QString result=QString::fromUtf8(f.readAll());for(auto i=values().cbegin();i!=values().cend();++i)result.replace("@"+i.key()+"@",i.value().toString().startsWith('#')?rgba(i.key().toUtf8().constData()):i.value().toString());return result;}
}
