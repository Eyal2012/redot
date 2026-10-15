#pragma once
#include <QObject>
#include <QJsonObject>
#include <QRectF>
#include <QTimer>
#include <QTransform>
#include <QVector>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>

namespace Scenes {
enum class Kind { Game, Window, Display, Camera, Image, Text, Microphone, DesktopAudio, ApplicationAudio };
QString kindName(Kind kind);
bool isAudio(Kind kind);
struct Definition {
    QString id, name;
    Kind kind{Kind::Text};
    QJsonObject settings;
    QSizeF intrinsic{640,360};
};
struct Visual {
    QString id, sourceId;
    double x{}, y{}, width{640}, height{360}, scale{1}, rotation{};
    double cropLeft{}, cropTop{}, cropRight{}, cropBottom{};
    bool visible{true}, locked{}, aspectLocked{true};
    // Layers are the ordered vector in Scene: back to front. No second order field.
    QTransform transform() const;
    QRectF localRect() const { return {0,0,width,height}; }
};
struct Audio {
    QString id, sourceId;
    double gain{1}, syncMs{};
    bool muted{}, monitoring{};
};
struct Scene {
    QString id, name;
    int width{1920}, height{1080};
    QVector<Visual> visuals;
    QVector<Audio> audio;
};
// Value snapshot for the compositor/source registry. It contains no live
// capture objects. A registry can resolve each definition ID exactly once.
struct Snapshot {
    QVector<Scene> scenes;
    QVector<Definition> definitions;
    QString selected;
    int migrationVersion{};
};
QByteArray encode(const Snapshot& value);
bool decode(const QByteArray& bytes, Snapshot& value, QString& error);
QString configurationPath();

class Model final : public QObject {
    Q_OBJECT
public:
    explicit Model(QString path=configurationPath(), QObject* parent=nullptr);
    ~Model() override;
    const Snapshot& data() const { return data_; }
    Snapshot snapshot() const { return data_; }
    const Scene* current() const;
    const Definition* definition(const QString& id) const;
    const Visual* visual(const QString& id) const;
    QString selection() const { return selection_; }
    bool ready() const { return ready_; }
    bool readOnly() const { return readOnly_; }
    QString status() const { return status_; }
    QString path() const { return path_; }
    bool idle() const;
    void flush();
    void migrateCapture(const QJsonObject& previous);
    QString create(QString name={});
    bool rename(const QString& id, QString name);
    QString duplicate(const QString& id);
    bool remove(const QString& id);
    bool select(const QString& id);
    bool reorder(const QString& id, int index);
    void selectVisual(const QString& id);
    QString addDefinition(Kind kind, QString name, QJsonObject settings={}, QSizeF intrinsic={640,360});
    bool updateDefinition(const Definition& value);
    QString addVisual(const QString& definitionId);
    bool updateVisual(const Visual& value);
    bool removeVisual(const QString& id);
    bool moveVisual(const QString& id, int index);
    bool center(const QString& id);
    bool fit(const QString& id);
    bool setResolution(int width, int height);
    QString addAudio(const QString& definitionId);
    bool updateAudio(const Audio& value);
    bool removeAudio(const QString& id);
Q_SIGNALS:
    void changed();
    void visualChanged();
    void selectionChanged();
    void statusChanged();
private:
    Scene* editScene();
    bool writable() const { return ready_&&!readOnly_; }
    void dirty(bool structural=true);
    void enqueueSave();
    void work();
    Snapshot data_;
    QString path_, selection_, status_{"Loading scene configuration…"};
    bool ready_{}, readOnly_{};
    QTimer debounce_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<QByteArray> pending_;
    bool stopping_{}, working_{true};
    std::thread worker_;
};
}
