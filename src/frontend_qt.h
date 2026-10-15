#pragma once
#include "frontend_bridge.h"
#include <QAbstractListModel>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QTimer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>

class ClipImages final : public QQuickImageProvider
{
    QMutex mutex_;
    std::vector<std::shared_ptr<ClipThumb>> thumbs_;
    QCache<QString, QImage> images_{64};

  public:
    ClipImages() : QQuickImageProvider(Image)
    {
    }
    void reset(const std::vector<ClipInfo> &clips);
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};
class MediaController final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY playbackChanged)
    Q_PROPERTY(double position READ position NOTIFY playbackChanged)
    Q_PROPERTY(double duration READ duration NOTIFY playbackChanged)
    Q_PROPERTY(QStringList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(bool desktopGlass READ desktopGlass NOTIFY changed)
    Q_PROPERTY(QString backdropStatus READ backdropStatus NOTIFY changed)
    Q_PROPERTY(bool windowsTransparencyDisabled READ windowsTransparencyDisabled NOTIFY changed)
    Q_PROPERTY(bool motion READ motion NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY playbackChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY playbackChanged)
    Q_PROPERTY(QMediaPlayer* player READ player CONSTANT)
    Q_PROPERTY(quint64 frameSerial READ frameSerial NOTIFY frameAvailable)
    Q_PROPERTY(double playbackVolume READ playbackVolume NOTIFY playbackChanged)
    Q_PROPERTY(int activeTrack READ activeTrack NOTIFY playbackChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY changed)
    FrontendBackend *backend_{};
    QQuickView *view_{};
    ClipImages *images_{};
    std::vector<ClipInfo> all_;
    std::vector<int> filtered_;
    ClipInfo selected_;
    QString search_, filter_{"All"}, sort_{"Newest"}, status_;
    QStringList tracks_;
    QMediaPlayer *player_{};
    QAudioOutput *audio_{};
    quint64 frameSerial_{}, gpuFrames_{}, cpuFrames_{};
    quint64 presentedSerial_{};
    qint64 videoStartTimeUs_{};
    QMetaObject::Connection sinkConnection_;
    uint64_t revision_{~uint64_t{0}};
    bool playing_{}, paused_{}, muted_{};
    float volume_{0.7f};
    bool motion_{true};
    bool desktopGlass_{};
    bool windowsTransparencyDisabled_{};
    QString backdropStatus_;
    void refilter();

  public:
    enum Roles
    {
        TitleRole = Qt::UserRole + 1,
        ThumbnailRole,
        MetadataRole,
        DateRole,
        AudioRole,
        ErrorRole
    };
    MediaController(FrontendBackend *backend, QQuickView *view, ClipImages *images);
    ~MediaController() override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void tick();
#ifdef REDOT_UI_VERIFICATION
    // Owned metadata fixtures only; absent from the shipping frontend.
    void verificationClips(const std::vector<ClipInfo>& clips) { all_=clips;refilter(); }
#endif
    bool playing() const
    {
        return playing_;
    }
    bool paused() const
    {
        return paused_;
    }
    bool muted() const
    {
        return muted_;
    }
    bool desktopGlass()const{return desktopGlass_;}
    bool windowsTransparencyDisabled()const{return windowsTransparencyDisabled_;}
    QString backdropStatus()const{return backdropStatus_;}
    bool motion() const
    {
        return motion_;
    }
    QString title() const;
    QString status() const
    {
        return status_;
    }
    double position() const;
    double duration() const;
    QStringList tracks() const
    {
        return tracks_;
    }
    QMediaPlayer *player() const { return player_; }
    quint64 frameSerial() const { return frameSerial_; }
    quint64 gpuFrames() const { return gpuFrames_; }
    quint64 cpuFrames() const { return cpuFrames_; }
    qint64 videoStartTimeUs() const { return videoStartTimeUs_; }
    double playbackVolume() const { return volume_; }
    int activeTrack() const { return player_->activeAudioTrack(); }
    bool maximized() const { return view_->visibility()==QWindow::Maximized; }
    Q_INVOKABLE void query(const QString &text, const QString &filter, const QString &sort);
    Q_INVOKABLE void open(int row);
    Q_INVOKABLE void back();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void volume(double gain);
    Q_INVOKABLE void mute();
    Q_INVOKABLE void track(int index);
    Q_INVOKABLE void attachSink(QVideoSink *sink);
    Q_INVOKABLE void fullscreen();
    Q_INVOKABLE void escape();
    Q_INVOKABLE void folder();
    Q_INVOKABLE void external();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openWindowsTransparencySettings();
    Q_INVOKABLE void windowAction(int action) {
        if(action==0)view_->showMinimized();
        else if(action==1){view_->visibility()==QWindow::Maximized?view_->showNormal():view_->showMaximized();}
        else if(action==2)view_->close();
    }
  Q_SIGNALS:
    void changed();
    void playbackChanged();
    void frameAvailable();
    void framePresented();
};
