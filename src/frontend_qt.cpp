#include "frontend_qt.h"
#include "settings_store.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMutexLocker>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QSystemTrayIcon>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWindowStateChangeEvent>
#include <QIntValidator>
#include <QVideoFrame>
#include <QMediaMetaData>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QLibrary>
#include <cmath>
#include <algorithm>
#include <thread>
#include <commctrl.h>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include "window_chrome.h"
#include "brand_icon.h"
#include <shobjidl.h>
#include <appmodel.h>
#include "design_system.h"
#include "dashboard_icons.h"
#include "scene_editor.h"
#include "application_picker.h"
#include "logging.h"
#ifdef REDOT_UI_VERIFICATION
#include "../tests/qt_render_probe.h"
#include <psapi.h>
#endif

static QString q(const std::wstring &s)
{
    return QString::fromStdWString(s);
}
static bool quiet()
{
    return qEnvironmentVariableIntValue("PANDACLIP_TEST_QUIET_UI") == 1;
}
static QString trayOperation(const FrontendState& s)
{
    if(s.closing)return "Finalizing and exiting";
    if(s.recovery)return "Recovering";
    const auto mode=static_cast<Branding::Mode>(s.taskbarMode);
    if(mode==Branding::Mode::Recording)return s.replay?"Recording and replay running":"Recording";
    if(mode==Branding::Mode::Replay)return "Replay running";
    if(s.encoding)return s.encoderReady?"Stopping / finalizing":"Starting encoder";
    if(s.pendingStart)return "Waiting to start";
    return "Idle";
}
static int nativeKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + key - Qt::Key_F1;
    if (key >= Qt::Key_0 && key <= Qt::Key_9 && modifiers.testFlag(Qt::KeypadModifier))
        return VK_NUMPAD0 + key - Qt::Key_0;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        return key;
    switch (key)
    {
    case Qt::Key_Left:
        return VK_LEFT;
    case Qt::Key_Right:
        return VK_RIGHT;
    case Qt::Key_Up:
        return VK_UP;
    case Qt::Key_Down:
        return VK_DOWN;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    case Qt::Key_PageUp:
        return VK_PRIOR;
    case Qt::Key_PageDown:
        return VK_NEXT;
    case Qt::Key_Insert:
        return VK_INSERT;
    case Qt::Key_Delete:
        return VK_DELETE;
    case Qt::Key_Backspace:
        return VK_BACK;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Pause:
        return VK_PAUSE;
    default:
        break;
    }
    if (key > 0 && key < 256)
    {
        SHORT vk = VkKeyScanW(static_cast<wchar_t>(key));
        if (vk != -1)
            return LOBYTE(vk);
    }
    return 0;
}
static QKeySequence nativeSequence(WORD code)
{
    int key = LOBYTE(code);
    Qt::KeyboardModifiers mods;
    if (HIBYTE(code) & HOTKEYF_CONTROL)
        mods |= Qt::ControlModifier;
    if (HIBYTE(code) & HOTKEYF_SHIFT)
        mods |= Qt::ShiftModifier;
    if (HIBYTE(code) & HOTKEYF_ALT)
        mods |= Qt::AltModifier;
    if (key >= VK_F1 && key <= VK_F24)
        key = Qt::Key_F1 + key - VK_F1;
    else if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
    {
        key = Qt::Key_0 + key - VK_NUMPAD0;
        mods |= Qt::KeypadModifier;
    }
    else
    {
        switch (key)
        {
        case VK_LEFT:
            key = Qt::Key_Left;
            break;
        case VK_RIGHT:
            key = Qt::Key_Right;
            break;
        case VK_UP:
            key = Qt::Key_Up;
            break;
        case VK_DOWN:
            key = Qt::Key_Down;
            break;
        case VK_HOME:
            key = Qt::Key_Home;
            break;
        case VK_END:
            key = Qt::Key_End;
            break;
        case VK_PRIOR:
            key = Qt::Key_PageUp;
            break;
        case VK_NEXT:
            key = Qt::Key_PageDown;
            break;
        case VK_INSERT:
            key = Qt::Key_Insert;
            break;
        case VK_DELETE:
            key = Qt::Key_Delete;
            break;
        case VK_BACK:
            key = Qt::Key_Backspace;
            break;
        case VK_ESCAPE:
            key = Qt::Key_Escape;
            break;
        case VK_SPACE:
            key = Qt::Key_Space;
            break;
        case VK_TAB:
            key = Qt::Key_Tab;
            break;
        case VK_RETURN:
            key = Qt::Key_Return;
            break;
        case VK_PAUSE:
            key = Qt::Key_Pause;
            break;
        default:
            break;
        }
    }
    return key ? QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key))) : QKeySequence();
}
static void labelText(QLabel *label, const QString &text)
{
    if (label->text() != text)
        label->setText(text);
}
static void showOwned(QWidget *w)
{
    // Verification can use the current compositor for its own moving source
    // while keeping every frontend/popup hidden (no native popup input grab).
#ifdef REDOT_UI_VERIFICATION
    if(quiet()&&qEnvironmentVariableIntValue("PANDACLIP_QT_HIDDEN_CHECK")){w->hide();return;}
#endif
    w->setAttribute(Qt::WA_ShowWithoutActivating, quiet());
    if(quiet()&&qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW")){
        const auto hwnd=reinterpret_cast<HWND>(w->winId());
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);
    }
    w->show();
    if (!quiet())
    {
        w->raise();
        w->activateWindow();
    }
}

void ClipImages::reset(const std::vector<ClipInfo> &clips)
{
    QMutexLocker lock(&mutex_);
    thumbs_.clear();
    images_.clear();
    for (auto &c : clips)
        thumbs_.push_back(c.thumbnail);
}
QImage ClipImages::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    std::shared_ptr<ClipThumb> thumb;
    QString key = id.section('/', 0, 0);
    {
        QMutexLocker lock(&mutex_);
        if (auto image = images_.object(key))
        {
            if (size)
                *size = image->size();
            return *image;
        }
        bool ok = false;
        int row = key.toInt(&ok);
        if (ok && row >= 0 && row < static_cast<int>(thumbs_.size()))
            thumb = thumbs_[row];
    }
    QImage image(160, 90, QImage::Format_RGB32);
    image.fill(QColor("#293445"));
    if (thumb && thumb->bitmap)
    {
        BITMAP b{};
        GetObjectW(thumb->bitmap, sizeof(b), &b);
        image = QImage(b.bmWidth, b.bmHeight, QImage::Format_RGB32);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = b.bmWidth;
        info.bmiHeader.biHeight = -b.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        auto dc = GetDC(nullptr);
        GetDIBits(dc, thumb->bitmap, 0, b.bmHeight, image.bits(), &info, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        for (int y = 0; y < image.height(); ++y)
        {
            auto pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                pixels[x] |= 0xff000000;
        }
    }
    {
        QMutexLocker lock(&mutex_);
        images_.insert(key, new QImage(image));
    }
    if (size)
        *size = image.size();
    (void)requested;
    return image;
}
static QFunctionPointer playbackTraceFunction(const char* name) {
#ifdef QT_DEBUG
    return QLibrary::resolve("Qt6Multimediad",name);
#else
    return QLibrary::resolve("Qt6Multimedia",name);
#endif
}
static void playbackTraceCommand(const char* action, qint64 value) {
    if (!qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_TRACE")) return;
    static auto function=reinterpret_cast<void(*)(const char*,long long)>(playbackTraceFunction("pandaclipPlaybackCommandTrace"));
    if (function) function(action,value);
}
MediaController::MediaController(FrontendBackend *b, QQuickView *v, ClipImages *images)
    : QAbstractListModel(v), backend_(b), view_(v), images_(images)
{
    player_ = new QMediaPlayer(this);
    if (qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_TRACE")) {
        auto trace=reinterpret_cast<void(*)(const char*,int,long long)>(playbackTraceFunction("pandaclipDecodedPlaybackTrace"));
        if (trace) {
            QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Float);
            auto buffers=new QAudioBufferOutput(format,this);player_->setAudioBufferOutput(buffers);
            // Direct delivery stays on Qt's audio worker. The DLL copies into a
            // bounded writer queue; no file I/O or UI access occurs here.
            QObject::connect(buffers,&QAudioBufferOutput::audioBufferReceived,this,[trace](const QAudioBuffer& b){if(b.isValid())trace(b.constData<char>(),b.byteCount(),b.startTime());},Qt::DirectConnection);
        }
    }
    audio_ = new QAudioOutput(this);
    audio_->setMuted(quiet());
    audio_->setVolume(volume_);
    player_->setAudioOutput(audio_);
    QObject::connect(view_,&QWindow::visibilityChanged,this,[this]{Q_EMIT changed();});
    QObject::connect(view_,&QQuickWindow::frameSwapped,this,[this]{
        if(frameSerial_!=presentedSerial_){presentedSerial_=frameSerial_;Q_EMIT framePresented();}
    });
    QObject::connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) {
        status_ = "Playback failed: " + error;
        Q_EMIT playbackChanged();
    });
    QObject::connect(player_, &QMediaPlayer::playbackStateChanged, this, [this] {
        paused_ = player_->playbackState() != QMediaPlayer::PlayingState;
        Q_EMIT playbackChanged();
    });
    QObject::connect(player_, &QMediaPlayer::activeTracksChanged, this, [this] { Q_EMIT playbackChanged(); });
}
static QQuickItem* visualItem(QQuickItem* root,const QString& name){
    if(!root)return nullptr;if(root->objectName()==name)return root;
    for(auto child:root->childItems())if(auto match=visualItem(child,name))return match;
    return nullptr;
}
MediaController::~MediaController()
{
    player_->stop();
    player_->setVideoSink(nullptr);
}
int MediaController::rowCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : static_cast<int>(filtered_.size());
}
QVariant MediaController::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() >= rowCount())
        return {};
    int original = filtered_[i.row()];
    const auto &c = all_[original];
    switch (role)
    {
    case TitleRole:
        return q(c.name);
    case ThumbnailRole:
        return QString("image://clips/%1/%2").arg(original).arg(revision_);
    case MetadataRole:
        return QString("%1  ·  %2 s  ·  %3 × %4").arg(q(c.type)).arg(c.duration, 0, 'f', 1).arg(c.width).arg(c.height);
    case DateRole:
        return QString("%1 · %2 MiB · H.264 · %3 FPS")
            .arg(q(c.date)).arg(c.bytes / 1048576.0, 0, 'f', 1)
            .arg(c.fpsDen ? double(c.fpsNum) / c.fpsDen : 0, 0, 'f', 1);
    case AudioRole:
        return static_cast<int>(c.audioStreams.size());
    case ErrorRole:
        return q(c.error);
    default:
        return {};
    }
}
QHash<int, QByteArray> MediaController::roleNames() const
{
    return {{TitleRole, "clipTitle"}, {ThumbnailRole, "thumbnail"}, {MetadataRole, "metadata"},
            {DateRole, "dateText"},   {AudioRole, "audioCount"},    {ErrorRole, "clipError"}};
}
void MediaController::refilter()
{
    beginResetModel();
    filtered_.clear();
    for (int i = 0; i < static_cast<int>(all_.size()); ++i)
    {
        const auto &c = all_[i];
        if (!q(c.name).contains(search_, Qt::CaseInsensitive))
            continue;
        if (filter_ == "Replay" && c.type != L"Replay")
            continue;
        if (filter_ == "Recording" && c.type != L"Recording")
            continue;
        if (filter_ == "Has audio" && c.audioStreams.empty())
            continue;
        if (filter_ == "Unreadable" && c.error.empty())
            continue;
        filtered_.push_back(i);
    }
    std::stable_sort(filtered_.begin(), filtered_.end(), [&](int a, int b) {
        if (sort_ == "Size (largest first)" || sort_ == "Size (smallest first)") {
            // Metadata comes from the bounded library worker; never stat/read
            // files on the UI thread. Missing entries retain their last-known
            // size until Refresh removes them. Full paths break ties reliably.
            if (all_[a].bytes != all_[b].bytes)
                return sort_ == "Size (largest first)" ? all_[a].bytes > all_[b].bytes : all_[a].bytes < all_[b].bytes;
            return all_[a].path < all_[b].path;
        }
        if (sort_ == "Name")
            return all_[a].name < all_[b].name;
        if (sort_ == "Duration")
            return all_[a].duration > all_[b].duration;
        return sort_ == "Oldest" ? all_[a].stamp < all_[b].stamp : all_[a].stamp > all_[b].stamp;
    });
    endResetModel();
    Q_EMIT changed();
}
void MediaController::query(const QString &t, const QString &f, const QString &s)
{
    search_ = t;
    filter_ = f;
    sort_ = s;
    refilter();
}
void MediaController::tick()
{
    auto s = backend_->state();
    if (revision_ != s.libraryRevision)
    {
        revision_ = s.libraryRevision;
        all_ = backend_->clips();
        images_->reset(all_);
        refilter();
    }
    const bool desktop=s.transparency&&view_->property("desktopBackdropMaterial").toBool();
    const QString reason=view_->property("desktopBackdropStatus").toString();
    const bool windowsDisabled=view_->property("windowsTransparencyDisabled").toBool();
    if (desktopGlass_ != desktop || motion_ != s.motion || backdropStatus_!=reason || windowsTransparencyDisabled_!=windowsDisabled)
    {
        desktopGlass_=desktop;
        backdropStatus_=reason;windowsTransparencyDisabled_=windowsDisabled;
        motion_ = s.motion;
        Q_EMIT changed();
    }
    if (playing_)
    {
        if (player_->error() != QMediaPlayer::NoError)
            status_ = "Playback failed: " + player_->errorString();
        else
            status_ = QString("%1 / %2 seconds").arg(position(), 0, 'f', 1).arg(duration(), 0, 'f', 1);
        Q_EMIT playbackChanged();
    }
}
void MediaController::openWindowsTransparencySettings(){if(!quiet())QDesktopServices::openUrl(QUrl("ms-settings:easeofaccess-visualeffects"));}
QString MediaController::title() const
{
    return q(selected_.name);
}
double MediaController::position() const
{
    return player_->position() / 1000.0;
}
double MediaController::duration() const
{
    return player_->duration() / 1000.0;
}
void MediaController::open(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    selected_ = all_[filtered_[row]];
    if (!selected_.error.empty())
    {
        status_ = q(selected_.error);
        Q_EMIT playbackChanged();
        return;
    }
    playbackTraceCommand("open",row);
    player_->stop();
    tracks_.clear();
    for (auto &name : selected_.audioLabels)
        tracks_ << q(name);
    playing_ = true;
    paused_ = false;
    status_ = "Opening clip…";
    audio_->setVolume(volume_);
    audio_->setMuted(muted_ || quiet());
    player_->setSource(QUrl::fromLocalFile(q(selected_.path)));
    player_->play();
    Q_EMIT changed();
    Q_EMIT playbackChanged();
}
void MediaController::back()
{
    playbackTraceCommand("back",player_->position());
    player_->stop();
    player_->setSource(QUrl());
    playing_ = false;
    tracks_.clear();
    status_.clear();
    Q_EMIT changed();
    Q_EMIT playbackChanged();
}
void MediaController::toggle()
{
    playbackTraceCommand(player_->playbackState()==QMediaPlayer::PlayingState?"pause":"resume",player_->position());
    if (player_->playbackState() == QMediaPlayer::PlayingState)
        player_->pause();
    else
        player_->play();
    paused_ = player_->playbackState() != QMediaPlayer::PlayingState;
    Q_EMIT playbackChanged();
}
void MediaController::seek(double s)
{
    playbackTraceCommand("seek",qint64(s*1000));
    player_->setPosition(static_cast<qint64>(std::clamp(s, 0.0, duration()) * 1000.0));
}
void MediaController::volume(double v)
{
    volume_ = static_cast<float>(std::clamp(v, 0.0, 1.0));
    audio_->setVolume(volume_);
    Q_EMIT playbackChanged();
}
void MediaController::mute()
{
    muted_ = !muted_;
    audio_->setMuted(muted_ || quiet());
    Q_EMIT playbackChanged();
}
void MediaController::track(int i)
{
    playbackTraceCommand("track",i);
    if (i >= 0 && i < player_->audioTracks().size() && i != player_->activeAudioTrack())
    {
        // Qt's audio-track indexes are ordinal, independent of MP4/reader stream IDs.
        // The FFmpeg backend preserves its playback clock when changing tracks.
        // An additional seek flushes that newly opened renderer a second time.
        player_->setActiveAudioTrack(i);
        Q_EMIT playbackChanged();
    }
}
void MediaController::attachSink(QVideoSink *sink)
{
    QObject::disconnect(sinkConnection_);
    player_->setVideoSink(sink);
    if (sink)
        sinkConnection_ = QObject::connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            ++frameSerial_;
            videoStartTimeUs_=frame.startTime();
            if (frame.handleType() == QVideoFrame::RhiTextureHandle) ++gpuFrames_;
            else ++cpuFrames_;
            Q_EMIT frameAvailable();
        });
}
void MediaController::fullscreen()
{
    if (view_->visibility() == QWindow::FullScreen)
        view_->showNormal();
    else
        view_->showFullScreen();
}
void MediaController::folder()
{
    if (!quiet())
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(backend_->state().folder)));
}
void MediaController::escape()
{
    if (view_->visibility() == QWindow::FullScreen)
        view_->showNormal();
    else
        back();
}
void MediaController::external()
{
    if (!quiet() && !selected_.path.empty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(selected_.path)));
}
void MediaController::refresh()
{
    backend_->requestLibrary();
}

class NativeCanvas final : public QWidget
{
    QTimer resize_;
    QPointer<QWidget> overlay_;
    void updateClip(){
        QPainterPath outline;outline.addRoundedRect(QRectF(rect()),14,14);
        QRegion region(outline.toFillPolygon().toPolygon());
        if(overlay_&&overlay_->isVisible())region-=QRegion(QRect(mapFromGlobal(overlay_->mapToGlobal(QPoint())),overlay_->size()));
        setMask(region);
    }

  public:
    FrontendBackend *backend{};
    void occlude(QWidget* overlay){overlay_=overlay;updateClip();}
    NativeCanvas()
    {
        // Only the preview requires an HWND. Native intermediate containers
        // isolate their backing stores from the workspace GPU composition.
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        setMinimumSize(200, 120);
        resize_.setSingleShot(true);
        resize_.setInterval(35);
        QObject::connect(&resize_, &QTimer::timeout, this, [this] {
            if (backend)
                backend->previewResize();
        });
    }
    QPaintEngine *paintEngine() const override
    {
        return nullptr;
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
    }
    void resizeEvent(QResizeEvent *) override
    {
        updateClip();
        resize_.start();
    }
};
class LevelMeter final : public QWidget
{
    float level_{}, peak_{};

  public:
    LevelMeter()
    {
        setProperty("rawRms",0.0f);setProperty("rawPeak",0.0f);
        setMinimumHeight(45);
        setMaximumHeight(52);
        setToolTip("Raw RMS bars and peak marker, before recording gain/mute. -90 to 0 dBFS. Nonzero samples do not establish audible speech.");
    }
    void compact(){setProperty("compactMeter",true);setMinimumHeight(24);setMaximumHeight(24);setMinimumWidth(80);setMaximumWidth(180);}
    void level(unsigned v)
    {
        signal(v/1000.f,v/1000.f);
    }
    void signal(float rms,float peak) {
        if(level_==rms&&peak_==peak)return;
        level_=rms;peak_=peak;setProperty("rawRms",rms);setProperty("rawPeak",peak);update();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        double db = level_ > 0 ? 20 * std::log10(level_) : -90;
        double ratio = std::clamp((db + 90) / 90, 0.0, 1.0);
        int w = width() - 16;
        for (int i = 0; i < 24; ++i)
        {
            QColor c = i < 19 ? QColor("#50e781") : i < 22 ? QColor("#d6bb59") : QColor("#d36d70");
            if (i >= ratio * 24)
                c = QColor("#393340");
            p.setPen(Qt::NoPen);p.setBrush(c);p.drawRoundedRect(QRectF(8+i*w/24.0,7,std::max(1.0,w/24.0-3),14),3,3);
        }
        p.setPen(QColor("#c8cbd6"));
        p.setFont(QFont("Segoe UI", 8));
        const double peakDb=peak_>0?20*std::log10(peak_):-90;
        int peakX=8+int(std::clamp((peakDb+90)/90,0.0,1.0)*w);
        if(peak_>0){p.setPen(QColor("#f7e8c0"));p.drawLine(peakX,6,peakX,25);}
        if(!property("compactMeter").toBool())for (int n : {-90, -60, -36, -18, 0})
        {
            int x = 8 + static_cast<int>((n + 90) / 90.0 * w);
            p.drawText(QRect(x - 13, 28, 27, 15), Qt::AlignCenter, QString::number(n));
        }
    }
};
// Wheel input on closed editors belongs to the settings viewport. No value
// changes, device requests, settings writes or full-page layout occur here.
class SettingsWheel final : public QObject
{
    QScrollArea *area_{};

  public:
    SettingsWheel(QScrollArea *a) : QObject(a), area_(a)
    {
    }

  protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel)
        {
            auto wheel = static_cast<QWheelEvent *>(event);
            QWheelEvent forwarded(wheel->position(), wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                                  wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted());
            QApplication::sendEvent(area_->viewport(), &forwarded);
            return true;
        }
        return false;
    }
};

class SettingsWindow final : public QDialog
{
    DesktopBackdrop* backdrop_{};
    bool quick_{};
    QComboBox* trackLayout_{};
    QPointer<QWidget> returnFocus_;
    bool sessionBusy_{};
    ChromeTitle* titleBar_{};
    FrontendBackend *backend_{};
    QListWidget *categories_{};
    QStackedWidget *stack_{};
    QLabel *status_{};
    QLabel *audioStatus_{};
    QLabel* appearanceStatus_{};
    QLabel* trayStatus_{};
    std::map<int, QWidget *> controls_;
    QTimer tick_;
    void syncWidget(int id, QWidget *widget)
    {
        auto c = backend_->control(id);
        QSignalBlocker block(widget);
        widget->setEnabled(c.enabled&&!(quick_&&sessionBusy_&&(id==2030||id==2022||id==1009)));
        if(quick_&&id==2036)widget->setEnabled(c.enabled&&backend_->control(1014).selection==2);
        if(quick_&&id==1001&&controls_.contains(1014))widget->setEnabled(c.enabled&&backend_->control(1014).selection==1);
        if (auto cb = qobject_cast<QComboBox *>(widget))
        {
            if(id==2022&&quick_){cb->setCurrentIndex(QStringList{"30","60","120"}.indexOf(q(c.text)));return;}
            QStringList values;
            for (auto &item : c.choices)
                values << q(item);
            QStringList old;
            for (int i = 0; i < cb->count(); ++i)
                old << cb->itemText(i);
            if (old != values && !cb->view()->isVisible())
            {
                cb->clear();
                cb->addItems(values);
            }
            if (cb->isEditable())
            {
                if (!cb->hasFocus() && cb->currentText() != q(c.text))
                    cb->setEditText(q(c.text));
            }
            else if (!cb->view()->isVisible())
                cb->setCurrentIndex(c.selection);
        }
        else if (auto t = qobject_cast<QCheckBox *>(widget))
        {
            t->setChecked(c.id == 2072 || c.id == 2073 ? !c.checked : c.checked);
        }
        else if (auto slider = qobject_cast<QSlider *>(widget))
        {
            if (!slider->isSliderDown())
                slider->setValue(c.value);
        }
        else if (auto edit = qobject_cast<QLineEdit *>(widget))
        {
            if (!edit->hasFocus() && edit->text() != q(c.text))
                edit->setText(q(c.text));
        }
        else if (auto meter = dynamic_cast<LevelMeter *>(widget))
        {
            auto s=backend_->state();const auto& m=id==1021?s.gameMeter:s.micMeter;
            bool open=id==1021?s.gameOpen:s.micOpen;
            meter->signal(open?m.displayedRms:0,open?m.displayedPeak:0);
        }
        else if(auto action=qobject_cast<QPushButton*>(widget)) {
            auto text=action->property("compactFolderButton").toBool()?QString():q(c.text);if(quick_&&id==1002){auto state=backend_->state();text=state.pendingStart?"Cancel Start":state.capture?"Stop Capture":text;action->setEnabled(!state.encoding&&!state.closing&&(state.pendingStart||state.capture||c.enabled));}
            if(action->text()!=text)action->setText(text);
        }
        else if (auto label = qobject_cast<QLabel *>(widget)) {
            if(quick_&&id==1008){labelText(label,QDir::toNativeSeparators(QFileInfo(q(c.text)).fileName()));label->setToolTip(q(c.text));}
            else labelText(label,q(c.text));
        }
    }
    QWidget *editor(const FrontendControl &c, QScrollArea *area)
    {
        QWidget *widget = nullptr;
        int id = c.id;
        if(quick_&&id==2022){
            auto w=new QComboBox;w->addItems({"30 FPS","60 FPS","120 FPS"});w->setCurrentIndex(QStringList{"30","60","120"}.indexOf(q(c.text)));
            QObject::connect(w,&QComboBox::currentIndexChanged,this,[this](int index){if(index>=0)backend_->edit(2022,std::to_wstring(std::array{30,60,120}[index]),0,false);});
            w->setObjectName("setting2022");w->setEnabled(c.enabled);w->installEventFilter(new SettingsWheel(area));controls_[id]=w;return w;
        }
        switch (c.kind)
        {
        case FrontendControlKind::Choice: {
            auto w = new QComboBox;
            w->setEditable(c.editable);
            w->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            w->setMinimumContentsLength(12);
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            for (auto &item : c.choices)
                w->addItem(q(item));
            if (c.editable)
                w->setEditText(q(c.text));
            else
                w->setCurrentIndex(c.selection);
            QObject::connect(w, &QComboBox::currentIndexChanged, this, [this, w, id](int v) {
                if (!w->isEditable())
                    backend_->edit(id, w->currentText().toStdWString(), v, false);
            });
            if(quick_&&(id==1025||id==1028))w->lineEdit()->setValidator(new QIntValidator(id==1025?5:128,id==1025?600:4096,w->lineEdit()));
            if (w->isEditable())
                QObject::connect(w->lineEdit(), &QLineEdit::textEdited, this,
                                 [this, id, w](const QString &s) { if(!w->lineEdit()->validator()||w->lineEdit()->hasAcceptableInput())backend_->edit(id,s.toStdWString(),0,false);else backend_->validationError(id==1025?L"Clip duration must be 5-600 seconds; previous valid value retained.":L"Replay memory must be 128-4096 MiB; previous valid value retained."); });
            QObject::connect(w, &QComboBox::textActivated, this, [this, w, id](const QString &text) {
                if (w->isEditable()&&(!w->lineEdit()->validator()||w->lineEdit()->hasAcceptableInput()))
                    backend_->edit(id, text.toStdWString(), 0, false);
            });
            widget = w;
            break;
        }
        case FrontendControlKind::Edit: {
            auto w = new QLineEdit(q(c.text));
            QObject::connect(w, &QLineEdit::textEdited, this,
                             [this, id](const QString &s) { backend_->edit(id, s.toStdWString(), 0, false); });
            widget = w;
            break;
        }
        case FrontendControlKind::Toggle: {
            auto w = new QCheckBox(id == 2072   ? "Reduce transparency (solid controls)"
                                   : id == 2073 ? "Reduce motion (immediate interaction states)"
                                   : id == 2039 ? "Minimize to tray"
                                   : id == 2038 ? "Close to tray"
                                                : q(c.text));
                    w->setChecked(id == 2072 || id == 2073 ? !c.checked : c.checked);
            QObject::connect(w, &QCheckBox::toggled, this, [this, id](bool on) {
                backend_->edit(id, L"", 0, id == 2072 || id == 2073 ? !on : on);
            });
            widget = w;
            break;
        }
        case FrontendControlKind::Slider: {
            auto w = new QSlider(Qt::Horizontal);
            w->setRange(0, 200);
            w->setValue(c.value);
            QObject::connect(w, &QSlider::valueChanged, this, [this, id](int v) { backend_->edit(id, L"", v, false); });
            widget = w;
            break;
        }
        case FrontendControlKind::Meter: {
            auto w = new LevelMeter;
            w->level(c.value);
            widget = w;
            break;
        }
        case FrontendControlKind::Action: {
            auto w = new QPushButton(q(c.text));
            QObject::connect(w, &QPushButton::clicked, this, [this, id] {
                if(id==1002&&backend_->state().pendingStart)backend_->action(1003);
                else if (id == 1007)
                {
                    auto folder =
                        QFileDialog::getExistingDirectory(this, "Recording output folder", q(backend_->state().folder));
                    if (!folder.isEmpty())
                        backend_->setFolder(folder.toStdWString());
                }
                else
                    backend_->action(id);
            });
            widget = w;
            break;
        }
        case FrontendControlKind::Shortcut: {
            auto w = new QKeySequenceEdit;
            w->setKeySequence(nativeSequence(static_cast<WORD>(c.value)));
            w->setMaximumSequenceLength(1);
            QObject::connect(w, &QKeySequenceEdit::keySequenceChanged, this, [this, id, w](const QKeySequence &seq) {
                int code = 0;
                if (!seq.isEmpty())
                {
                    auto k = seq[0];
                    int v = nativeKey(k.key(), k.keyboardModifiers());
                    if (!v || k.keyboardModifiers().testFlag(Qt::MetaModifier))
                    {
                        backend_->validationError(L"Unsupported shortcut. Use Ctrl, Alt or Shift with a letter, "
                                                  L"number, arrow or F1-F24. Previous binding kept.");
                        QSignalBlocker block(w);
                        w->setKeySequence(nativeSequence(static_cast<WORD>(backend_->control(id).value)));
                        return;
                    }
                    int mods = 0;
                    if (k.keyboardModifiers() & Qt::ControlModifier)
                        mods |= HOTKEYF_CONTROL;
                    if (k.keyboardModifiers() & Qt::ShiftModifier)
                        mods |= HOTKEYF_SHIFT;
                    if (k.keyboardModifiers() & Qt::AltModifier)
                        mods |= HOTKEYF_ALT;
                    code = MAKEWORD(v, mods);
                }
                backend_->edit(id, L"", code, false);
            });
            widget = w;
            break;
        }
        default: {
            auto w = new QLabel(q(c.text));
            w->setWordWrap(true);
            w->setTextInteractionFlags(Qt::TextSelectableByMouse);
            widget = w;
            break;
        }
        }
        widget->setObjectName(QString("setting%1").arg(id));
        widget->setToolTip(q(c.label));
        widget->setEnabled(c.enabled);
        if (c.kind == FrontendControlKind::Choice || c.kind == FrontendControlKind::Edit ||
            c.kind == FrontendControlKind::Slider || c.kind == FrontendControlKind::Shortcut)
            widget->installEventFilter(new SettingsWheel(area));
        if (id)
            controls_[id] = widget;
        return widget;
    }

  public:
    std::function<void()> visibilityChanged;
    SettingsWindow(FrontendBackend* backend,QWidget* parent,const QString& title,const std::vector<int>& ids,int category,std::function<void(int)> advanced)
        :QDialog(parent,Qt::Widget),backend_(backend),quick_(true){
        // A non-native child overlay: no activation change or OS popup grab.
        setWindowFlags(Qt::Widget);
        setProperty("materialHost",true);setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAutoFillBackground(false);
        setObjectName("quickSettings");setAttribute(Qt::WA_NoMouseReplay);setWindowTitle(title);resize(410,460);
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(18,18,18,14);outer->setSpacing(10);
        auto heading=new QHBoxLayout;auto label=new QLabel(title+" · Quick Settings");label->setStyleSheet("font-size:13px;font-weight:600;");heading->addWidget(label,1);
        auto close=new QToolButton;close->setText("×");close->setAccessibleName("Close Quick Settings");heading->addWidget(close);outer->addLayout(heading);
        QObject::connect(close,&QToolButton::clicked,this,&QDialog::hide);
        auto scroll=new QScrollArea;scroll->setObjectName("quickScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        auto page=new QWidget;auto form=new QFormLayout(page);form->setContentsMargins(1,4,8,8);form->setSpacing(10);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        const auto rows=backend_->settings();
        for(int id:ids){auto c=backend_->control(id);for(auto& row:rows)if(row.id==id){c.label=row.label;break;}
            if(id==2034||id==2035||id==1007)continue;
            if(id==1008){auto field=new QWidget;auto row=new QHBoxLayout(field);row->setContentsMargins(0,0,0,0);row->setSpacing(8);auto folder=editor(c,scroll);folder->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);row->addWidget(folder,1);auto pick=editor(backend_->control(1007),scroll);pick->setProperty("compactFolderButton",true);auto action=qobject_cast<QPushButton*>(pick);action->setText("");action->setIcon(dashboardIcon(10));action->setIconSize(QSize(22,22));action->setStyleSheet("padding:4px;border-radius:10px;");action->setAccessibleName("Choose output folder");action->setFixedSize(36,32);row->addWidget(pick);form->addRow("Save to",field);continue;}
            if(id==2033){trackLayout_=new QComboBox;trackLayout_->setObjectName("quickTrackLayout");trackLayout_->addItems({"Mix + separate","Mix only","Separate only","Custom (Advanced)"});
                QObject::connect(trackLayout_,&QComboBox::currentIndexChanged,this,[this](int index){if(index<0||index==3)return;backend_->edit(2033,L"",0,index!=2);backend_->edit(2034,L"",0,index!=1);backend_->edit(2035,L"",0,index!=1);});
                trackLayout_->installEventFilter(new SettingsWheel(scroll));form->addRow("Audio tracks",trackLayout_);continue;}
            if(id==2030)c.label=L"Resolution";
            if(id==2022)c.label=L"Frame rate";
            if(id==1009)c.label=L"Quality";
            if(id==1025)c.label=L"Clip duration (seconds)";
            if(id==1028)c.label=L"RAM limit (MiB)";
            if(id==1008)c.label=L"Save to";
            if(id==1007)c.label=L"Choose folder";
            if(id==1001)c.label=L"Selected window";
            if(id==1004)c.label=L"Refresh windows";
            if(id==2200)c.label=L"Save Clip shortcut";
            if(id==2300)c.label=L"Hotkey status";
            if(id==1014)c.label=L"Mode (whole system is explicit)";
            if(id==1040)c.label=L"Enable game audio";
            if(id==1015)c.label=L"Enable microphone";
            form->addRow(q(c.label),editor(c,scroll));
        }
        scroll->setWidget(page);outer->addWidget(scroll,1);
        audioStatus_=new QLabel;audioStatus_->setWordWrap(true);audioStatus_->setMaximumHeight(100);outer->addWidget(audioStatus_);
        status_=new QLabel;status_->setWordWrap(true);status_->setMaximumHeight(88);outer->addWidget(status_);
        auto footer=new QHBoxLayout;auto apply=new QPushButton("Apply");apply->setObjectName("applyQuickSettings");auto link=new QPushButton("Advanced settings →");link->setObjectName("advancedSettingsLink");footer->addWidget(apply);footer->addWidget(link,1);outer->addLayout(footer);
        QObject::connect(apply,&QPushButton::clicked,this,[this,ids]{backend_->action(2010);if(std::find(ids.begin(),ids.end(),2200)!=ids.end())backend_->action(2080);refresh();});
        QObject::connect(link,&QPushButton::clicked,this,[this,category,advanced]{hide();advanced(category);});
        // The root already owns the GPU backing store. A second transparent
        // QQuickWidget nested in an overlay acquires a rectangular opaque
        // backing on this Qt/D3D path. Use the existing Widgets presentation
        // and canonical surface tokens here; no second compositor surface.
        qApp->installEventFilter(this);
        tick_.setInterval(300);QObject::connect(&tick_,&QTimer::timeout,this,[this]{refresh();});tick_.start();
    }
    void restoreFocus(QWidget* widget){returnFocus_=widget;}
    void refresh(){
        if(!isVisible())return;
        auto state=backend_->state();sessionBusy_=state.encoding||state.pendingStart;
        setProperty("desktopBackdropMaterial",parentWidget()->window()->property("desktopBackdropMaterial"));
        for(auto& [id,widget]:controls_)syncWidget(id,widget);
        if(trackLayout_){auto mix=backend_->control(2033),game=backend_->control(2034),mic=backend_->control(2035);QSignalBlocker block(trackLayout_);
            trackLayout_->setEnabled(!sessionBusy_&&mix.enabled&&game.enabled&&mic.enabled);if(!trackLayout_->view()->isVisible())trackLayout_->setCurrentIndex(mix.checked&&game.checked&&mic.checked?0:mix.checked&&!game.checked&&!mic.checked?1:!mix.checked&&game.checked&&mic.checked?2:3);}
        const auto settingsMessage=q(state.settingsStatus);labelText(status_,settingsMessage);
        status_->setVisible(!settingsMessage.isEmpty()&&!settingsMessage.startsWith("Settings saved")&&!settingsMessage.startsWith("Settings loaded"));
        labelText(audioStatus_,controls_.contains(1016)?q(state.micStatus)+"\nDevice/gain changes apply next session; mute is live. Raw level is measured before recording gain/mute.":controls_.contains(1014)?q(state.gameStatus)+"\nSource/gain changes apply next session; mute is live. Whole system is never an automatic fallback.":
            controls_.contains(1025)?QString("History %1 s · %2 / %3 MiB. Changes follow the current session boundary; replay warms after reset.").arg(state.historySeconds,0,'f',1).arg(state.replayBytes/1048576.0,0,'f',1).arg(state.replayCap/1048576.0,0,'f',0):
            QString("Encoder and track changes apply next session. Stop recording/replay to unlock those settings."));
        audioStatus_->setVisible(controls_.contains(1016)||controls_.contains(1014)||controls_.contains(1025)||sessionBusy_);
        setToolTip(audioStatus_->text()+"\n"+settingsMessage);
    }
    explicit SettingsWindow(FrontendBackend *backend, QWidget *parent) : QDialog(parent), backend_(backend)
    {
        DesktopBackdrop::prepare(this);backdrop_=new DesktopBackdrop(this);
        setWindowTitle("ReDot · Settings and source properties");setWindowIcon(Branding::icon());
        setWindowFlag(Qt::Window, true);
        setWindowFlag(Qt::FramelessWindowHint, true);
        resize(900, 700);
        setMinimumSize(640, 480);
        auto outer = new QVBoxLayout(this);
        outer->setContentsMargins(6,0,6,8);titleBar_=new ChromeTitle(this,"ReDot · Settings");outer->addWidget(titleBar_);new WorkspaceGlassLayer(this);
        auto body = new QHBoxLayout;
        categories_ = new QListWidget;
        categories_->addItems({"General", "Capture", "Recording", "Replay", "Audio", "Keybinds", "Appearance"});
        categories_->setFixedWidth(144);
        stack_ = new QStackedWidget;
        body->addWidget(categories_);
        body->addWidget(stack_, 1);
        outer->addLayout(body, 1);
        auto rows = backend_->settings();
        for (int cat = 0; cat < 7; ++cat)
        {
            auto scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setObjectName(QString("settingsScroll%1").arg(cat));
            auto page = new QWidget;
            auto form = new QFormLayout(page);
            form->setContentsMargins(20, 14, 20, 18);
            form->setHorizontalSpacing(24);
            form->setVerticalSpacing(12);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            if (cat == 1)
            {
                auto note=new QLabel("Capture content is configured in Scenes. The Capture scene selector chooses the one composition used by preview, recording and replay.");note->setWordWrap(true);form->addRow(note);
            }
            if(cat==4){auto note=new QLabel("Assign microphones, desktop or application audio in Scenes → Audio Sources. Each source has its own device, gain, mute and sync setting. Capture audio controls operate on the active scene.");note->setWordWrap(true);form->addRow(note);}
            if(cat==0){
                auto title=new QLabel("Window behavior");title->setObjectName("sectionHeading");form->addRow(title);
                for(int id:{2039,2038}){
                    auto row=std::find_if(rows.begin(),rows.end(),[id](const FrontendControl& c){return c.id==id;});
                    if(row!=rows.end())form->addRow(id==2039?"When minimized":"When closed",editor(*row,scroll));
                }
                trayStatus_=new QLabel;trayStatus_->setWordWrap(true);trayStatus_->setObjectName("notificationAreaStatus");form->addRow("System tray",trayStatus_);
            }
            for (auto &c : rows)
            {
                if (c.category != cat)
                    continue;
                if(cat==0&&(c.id==2038||c.id==2039))continue;
                if (!c.id && c.kind == FrontendControlKind::Text)
                {
                    auto text = q(c.label);
                    if (text.contains("  |  "))
                    {
                        auto title = new QLabel(text.section("  |  ", 0, 0));
                        title->setObjectName("sectionHeading");
                        form->addRow(title);
                    }
                    continue;
                }
                if (c.id == 2074)
                {
                    auto label =
                        new QLabel("Capture and View Media use Windows Desktop Acrylic behind dark-purple foreground surfaces. Windows or Reduce Transparency may select matching solid fallback. Text, thumbnails and video stay sharp; playback controls retain bounded local-video glass.");
                    label->setWordWrap(true);
                    form->addRow(label);
                    continue;
                }
                form->addRow(q(c.label), editor(c, scroll));
            }
            if(cat==6){
                appearanceStatus_=new QLabel;appearanceStatus_->setObjectName("appearanceBackdropStatus");appearanceStatus_->setWordWrap(true);
                form->addRow("Effective Windows backdrop",appearanceStatus_);
                auto open=new QPushButton("Open Windows transparency settings");open->setObjectName("openWindowsTransparencySettings");
                form->addRow(open);QObject::connect(open,&QPushButton::clicked,this,[]{if(!quiet())QDesktopServices::openUrl(QUrl("ms-settings:easeofaccess-visualeffects"));});
            }
            if (cat == 4)
            {
                audioStatus_ = new QLabel;
                audioStatus_->setWordWrap(true);
                audioStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
                form->addRow("Opened sources / input check", audioStatus_);
            }
            scroll->setWidget(page);
            stack_->addWidget(scroll);
        }
        auto footer = new QHBoxLayout;
        auto apply = new QPushButton("Apply settings");
        apply->setObjectName("applySettings");
        status_ = new QLabel;
        status_->setWordWrap(true);
        footer->addWidget(apply);
        footer->addWidget(status_, 1);
        outer->addLayout(footer);
        QObject::connect(apply, &QPushButton::clicked, this, [this] {
            backend_->action(2010);
            labelText(status_, q(backend_->state().settingsStatus));
        });
        QObject::connect(categories_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
        categories_->setCurrentRow(2);
        tick_.setInterval(500);
        QObject::connect(&tick_, &QTimer::timeout, this, [this] {
            if (!isVisible())
                return;
            for (auto &[id, w] : controls_)
                syncWidget(id, w);
            auto sources = backend_->state();
            labelText(trayStatus_,QSystemTrayIcon::isSystemTrayAvailable()?"Notification area available. Minimize and Close preferences are independent; tray Open restores the window.":"Notification area unavailable. Minimize stays on the taskbar; Close performs a safe exit. Recording/replay are never left hidden without a tray.");
            backdrop_->update(sources.transparency);
            labelText(appearanceStatus_,property("desktopBackdropStatus").toString()+"\nWindows controls the live blur behind this window; preview/text stay opaque. Desktop refraction is not implemented.");
            titleBar_->material(sources.transparency);
            labelText(audioStatus_, q(sources.gameStatus) + "\n" + q(sources.micStatus));
            labelText(status_, q(backend_->state().settingsStatus));
        });
        tick_.start();
    }
    void category(int index)
    {
        categories_->setCurrentRow(index);
        showOwned(this);
    }
protected:
    void paintEvent(QPaintEvent* event)override{
        if(!quick_){QDialog::paintEvent(event);return;}
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(DesignSystem::color("surfaceSolid"));painter.setPen(QPen(DesignSystem::color("outline"),1));
        const auto radius=DesignSystem::values().value("controlRadius").toInt();
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),radius,radius);
    }
    void hideEvent(QHideEvent* event)override{QDialog::hideEvent(event);if(visibilityChanged)visibilityChanged();if(quick_&&returnFocus_&&!qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW")){auto focus=returnFocus_;QPointer<SettingsWindow> popup=this;QTimer::singleShot(0,focus,[focus,popup]{if(popup&&!popup->isVisible()&&focus&&focus->window()->isActiveWindow()&&!QApplication::activePopupWidget())focus->setFocus(Qt::PopupFocusReason);});}}
    bool focusNextPrevChild(bool next)override{
        if(!quick_)return QDialog::focusNextPrevChild(next);
        auto current=QApplication::focusWidget();if(!current||!isAncestorOf(current))current=this;
        for(int count=0;count<256;++count){
            current=next?current->nextInFocusChain():current->previousInFocusChain();
            if(isAncestorOf(current)&&current->isVisibleTo(this)&&current->isEnabled()&&(current->focusPolicy()&Qt::TabFocus)&&!current->focusProxy()){
                current->setFocus(next?Qt::TabFocusReason:Qt::BacktabFocusReason);return true;
            }
        }
        return true;
    }
    void mousePressEvent(QMouseEvent* event)override{if(quick_&&!rect().contains(event->position().toPoint())){reject();event->accept();return;}QDialog::mousePressEvent(event);}
    bool eventFilter(QObject* object,QEvent* event)override{
        if(quick_&&isVisible()&&event->type()==QEvent::KeyPress){auto key=static_cast<QKeyEvent*>(event);auto widget=qobject_cast<QWidget*>(object);
            if(key->key()==Qt::Key_Escape&&widget&&(widget==this||isAncestorOf(widget))){reject();return true;}}
        if(quick_&&isVisible()&&event->type()==QEvent::MouseButtonPress){auto mouse=static_cast<QMouseEvent*>(event);auto widget=qobject_cast<QWidget*>(object);
            if(widget&&widget!=this&&!isAncestorOf(widget)&&!QRect(mapToGlobal(QPoint()),size()).contains(mouse->globalPosition().toPoint())){
                // A choice's native list is logically inside this overlay.
                bool choicePopup=false;for(auto p=widget;p;p=p->parentWidget())if(p==this){choicePopup=true;break;}
                if(!choicePopup){reject();return true;}}}
        if(quick_&&isVisible()&&event->type()==QEvent::WindowDeactivate&&object==parentWidget()->window()){
            // A native choice list is not on Qt's popup stack at the instant
            // Windows deactivates the parent. Wait for that transition to settle.
            QTimer::singleShot(100,this,[this]{if(isVisible()&&!QApplication::activePopupWidget()&&!parentWidget()->window()->isActiveWindow())hide();});
        }
        return QDialog::eventFilter(object,event);
    }
};

class DashboardDockTitle final : public QWidget {
public:
    explicit DashboardDockTitle(QWidget* parent):QWidget(parent){}
    QSize sizeHint()const override{return property("dashboardCollapsed").toBool()?QSize(0,0):QWidget::sizeHint();}
    QSize minimumSizeHint()const override{return property("dashboardCollapsed").toBool()?QSize(0,0):QWidget::minimumSizeHint();}
};
// Keep genuine QPushButton semantics/accessibility while arranging icon above
// label. No visual layer owns input or replaces the button's signal.
class DashboardNavigation final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent*)override{
        QStyleOptionButton option;initStyleOption(&option);option.text.clear();option.icon=QIcon();
        QPainter p(this);style()->drawControl(QStyle::CE_PushButton,&option,&p,this);
        const int y=text().isEmpty()?(height()-26)/2:17;
        icon().paint(&p,QRect((width()-26)/2,y,26,26),Qt::AlignCenter,isEnabled()?QIcon::Normal:QIcon::Disabled);
        p.setPen(isChecked()?DesignSystem::color("focus"):palette().color(QPalette::ButtonText));
        p.drawText(QRect(0,48,width(),24),Qt::AlignCenter,text());
    }
};
class SourceSelector final : public QPushButton {
public:using QPushButton::QPushButton;
protected:void paintEvent(QPaintEvent*)override{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    if(underMouse()||isDown()){p.setPen(Qt::NoPen);p.setBrush(isDown()?DesignSystem::color("pillPressed"):DesignSystem::color("pillHover"));p.drawRoundedRect(rect().adjusted(1,1,-2,-2),26,26);}
    icon().paint(&p,QRect(14,(height()-26)/2,26,26));p.setPen(palette().color(QPalette::ButtonText));
    p.drawText(QRect(50,0,std::max(10,width()-145),height()),Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(text(),Qt::ElideRight,std::max(10,width()-145)));
    if(hasFocus()){p.setPen(QPen(DesignSystem::color("focus"),1));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(2,2,-3,-3),24,24);}
}};
class Workspace final : public QMainWindow
{
    DesktopBackdrop* backdrop_{};
    QDockWidget* navigationDock_{};
    QWidget* navigation_{};
    QBoxLayout* audioLayout_{};
    QLabel *sourceSummary_{},*previewInfo_{},*shortcut_{},*appearanceBadge_{},*emptyHeading_{};
    QPushButton *sourceSelector_{},*previewToggle_{};
    QPushButton* transparencySettings_{};
    std::array<QPushButton*,5> quickGears_{};
    std::array<QPushButton*,4> navigationButtons_{};
    QStackedWidget* pages_{};
    Scenes::Model* sceneModel_{};
    Scenes::Page* scenes_{};
    bool scenesVisible_{};
    QByteArray captureDockState_;
    std::array<bool,3> captureDockVisible_{};
    void scenePage(bool on){
        if(on==scenesVisible_)return;
        if(quick_)quick_->hide();
        if(settings_)settings_->hide();
        if(diagnostics_)diagnostics_->hide();
        if(on){
            captureDockState_=saveState(1);
            const std::array<QDockWidget*,3> docks={sourceDock_,mixerDock_,controlDock_};
            for(int i=0;i<3;++i){captureDockVisible_[i]=docks[i]->isVisible();docks[i]->hide();}
            navigationDock_->show();pages_->setCurrentWidget(scenes_);
        }else{
            pages_->setCurrentIndex(0);restoreState(captureDockState_,1);
            const std::array<QDockWidget*,3> docks={sourceDock_,mixerDock_,controlDock_};
            for(int i=0;i<3;++i)docks[i]->setVisible(captureDockVisible_[i]);
            navigationDock_->setVisible(!advanced_);
        }
        scenesVisible_=on;
        navigationButtons_[0]->setChecked(!on);navigationButtons_[1]->setChecked(on);
        // NativeCanvas becomes invisible in the stack. App::draw already
        // checks its HWND visibility, without changing saved preview settings.
        layoutDebounce_.start();
    }
    QVBoxLayout* dashboardControlLayout_{};
    bool compactControlHeight_{};
    void updateControlDensity(){
        const bool compact=height()<780;
        if(!dashboardControlLayout_||compact==compactControlHeight_)return;
        compactControlHeight_=compact;
        dashboardControlLayout_->setContentsMargins(8,compact?22:78,22,compact?10:18);
        dashboardControlLayout_->setSpacing(compact?8:16);
        for(auto button:{record_,replay_,save_})if(button)button->setMinimumHeight(compact?48:62);
        for(int index=0;index<3;++index)if(quickGears_[index])quickGears_[index]->setFixedSize(50,compact?48:62);
        if(time_)time_->setStyleSheet(QString("font-size:%1px;font-weight:300;").arg(compact?28:34));
        if(saveStatus_)saveStatus_->setMaximumHeight(compact?56:110);
    }
    SettingsWindow* quick_{};
    QPointer<QWidget> quickAnchor_;
    bool advanced_{};
    QAction* advancedAction_{};
    QList<QWidget*> advancedAudio_;
    void clipPreviewForQuick(){if(canvas_)canvas_->occlude(quick_);}
    void quickSettings(int kind,QWidget* anchor){
        if(kind==5){sceneSelector();return;}
        if(quick_&&quick_->isVisible()&&quickAnchor_==anchor){quick_->hide();anchor->setFocus();return;}
        if(quick_) {quick_->hide();delete quick_;quick_=nullptr;}
        static const std::array<QString,5> titles={"Recording","Replay","Save Clip","Game audio","Microphone"};
        static const std::array<std::vector<int>,5> ids={std::vector<int>{2030,2022,1009,2033,2034,2035,1008,1007},
            std::vector<int>{1025,1028,1009},std::vector<int>{1025,1008,1007,2200,2300},
            std::vector<int>{1014,1019,1017},std::vector<int>{1016,1020,1018,1037}};
        const std::array<int,5> categories={2,3,0,4,4};
        quick_=new SettingsWindow(backend_.get(),this,titles[kind],ids[kind],categories[kind],[this,kind](int cat){if(kind==3||kind==4)scenePage(true);else properties(cat);});
        quick_->visibilityChanged=[this]{clipPreviewForQuick();};
        quickAnchor_=anchor;quick_->setProperty("quickKind",kind);
        quick_->restoreFocus(anchor);
        auto bounds=QRect(mapToGlobal(QPoint(8,titleBar_->height()+8)),QSize(width()-16,height()-titleBar_->height()-48));
        if(auto screen=QGuiApplication::screenAt(anchor->mapToGlobal(anchor->rect().center())))bounds=bounds.intersected(screen->availableGeometry().adjusted(8,8,-8,-8));
        quick_->resize(std::min(kind==0?370:420,bounds.width()),std::min(kind==0?350:kind==3?560:kind==4?510:470,bounds.height()));
        const QRect button(anchor->mapToGlobal(QPoint()),anchor->size());
        auto fit=[&](QPoint p){return QPoint(std::clamp(p.x(),bounds.left(),std::max(bounds.left(),bounds.right()-quick_->width()+1)),std::clamp(p.y(),bounds.top(),std::max(bounds.top(),bounds.bottom()-quick_->height()+1)));};
        std::array<QPoint,4> placements={QPoint(button.right()-quick_->width()+1,button.bottom()+10),QPoint(button.right()-quick_->width()+1,button.top()-quick_->height()-10),QPoint(button.left()-quick_->width()-12,bounds.top()),QPoint(button.right()+12,bounds.top())};
        QPoint position=fit(placements[0]);bool placed=false;
        for(auto candidate:placements){candidate=fit(candidate);if(!QRect(candidate,quick_->size()).intersects(button)){position=candidate;placed=true;break;}}
        if(!placed){
            // Small windows use the same scrollable content, with a shorter
            // viewport above/below the opener rather than covering it.
            int above=button.top()-bounds.top()-10,below=bounds.bottom()-button.bottom()-10;
            int height=std::max(above,below);
            if(height>=180){quick_->resize(quick_->width(),height);position=fit(QPoint(button.right()-quick_->width()+1,above>=below?bounds.top():button.bottom()+10));}
        }
        quick_->move(mapFromGlobal(position));quick_->show();quick_->raise();quick_->refresh();
        clipPreviewForQuick();
        if(!qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW"))quick_->findChild<QPushButton*>("applyQuickSettings")->setFocus(Qt::PopupFocusReason);
        QObject::connect(quick_,&QDialog::finished,this,[this]{clipPreviewForQuick();});
        auto current=quick_;QPointer<QWidget> focus=anchor;
        QObject::connect(quick_,&QDialog::finished,this,[focus]{if(focus&&!QApplication::activePopupWidget())focus->setFocus(Qt::PopupFocusReason);});
        Q_UNUSED(current);
    }
    void sceneSelector(){
        if(!sceneModel_->ready())return;
        auto menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
        for(auto& scene:sceneModel_->data().scenes){auto action=menu->addAction(scene.name);action->setCheckable(true);action->setChecked(scene.id==sceneModel_->data().selected);QObject::connect(action,&QAction::triggered,this,[this,id=scene.id]{sceneModel_->select(id);});}
        menu->addSeparator();menu->addAction("Edit scenes…",this,[this]{scenePage(true);});
        menu->popup(sourceSelector_->mapToGlobal(QPoint(0,sourceSelector_->height())));
    }
    QPushButton* gear(int kind){
        auto button=new QPushButton;button->setIcon(dashboardIcon(2));button->setIconSize(QSize(25,25));button->setObjectName(QString("quickGear%1").arg(kind));button->setFixedSize(42,42);
        const std::array<QString,5> names={"Recording Quick Settings","Replay Quick Settings","Save Clip Quick Settings","Game audio Quick Settings","Microphone Quick Settings"};
        button->setAccessibleName(names[kind]);button->setToolTip(names[kind]);button->setStyleSheet("padding:4px;border-radius:12px;");
        quickGears_[kind]=button;QObject::connect(button,&QPushButton::clicked,this,[this,kind,button]{quickSettings(kind,button);});return button;
    }
    void dashboard(bool advanced){
        if(scenesVisible_)scenePage(false);
        advanced_=advanced;
        for(auto d:{sourceDock_,mixerDock_,controlDock_}){d->setFloating(false);d->titleBarWidget()->setProperty("dashboardCollapsed",!advanced);d->titleBarWidget()->setVisible(advanced);d->titleBarWidget()->setFixedHeight(advanced?34:0);d->titleBarWidget()->updateGeometry();d->show();}
        navigationDock_->setVisible(!advanced);sourceDock_->setVisible(advanced);
        mixerDock_->setMinimumHeight(advanced?0:140);mixerDock_->setMaximumHeight(advanced?QWIDGETSIZE_MAX:140);
        audioLayout_->setDirection(advanced?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
        for(auto widget:advancedAudio_)widget->setVisible(advanced);
        if(advanced){addDockWidget(Qt::LeftDockWidgetArea,sourceDock_);addDockWidget(Qt::BottomDockWidgetArea,mixerDock_);addDockWidget(Qt::BottomDockWidgetArea,controlDock_);splitDockWidget(mixerDock_,controlDock_,Qt::Horizontal);resizeDocks({mixerDock_},{210},Qt::Vertical);}
        else{addDockWidget(Qt::LeftDockWidgetArea,navigationDock_);addDockWidget(Qt::BottomDockWidgetArea,mixerDock_);addDockWidget(Qt::RightDockWidgetArea,controlDock_);resizeDocks({controlDock_},{330},Qt::Horizontal);resizeDocks({mixerDock_},{140},Qt::Vertical);}
        layoutDebounce_.start();
    }
    ChromeTitle* titleBar_{};
    std::unique_ptr<FrontendBackend> backend_;
    std::unique_ptr<Branding::TaskbarIcon> brandIcon_;
    NativeCanvas *canvas_{};
    QStackedWidget *previewStack_{};
    QTreeWidget *sources_{};
    QDockWidget *sourceDock_{}, *mixerDock_{}, *controlDock_{};
    QPushButton *record_{}, *replay_{}, *save_{}, *capture_{};
    QLabel *time_{}, *history_{}, *saveStatus_{}, *status_{}, *gameName_{}, *micName_{};
    QSlider *gameGain_{}, *micGain_{};
    QPushButton *gameMute_{}, *micMute_{}, *micCheck_{};
    LevelMeter *gameMeter_{}, *micMeter_{};
    SettingsWindow *settings_{};
    QDialog *diagnostics_{};
    DesktopBackdrop* diagnosticsBackdrop_{};
    QPlainTextEdit *diagnosticText_{};
    QQuickView *media_{};
    DesktopBackdrop* mediaBackdrop_{};
    MediaController *mediaController_{};
    QSystemTrayIcon *tray_{};
    QAction *trayOpen_{}, *traySave_{}, *trayRecord_{}, *trayReplay_{}, *trayExit_{}, *trayState_{};
    Qt::WindowStates trayRestoreState_{};
    Qt::WindowStates minimizeRestoreState_{};
    QRect trayRestoreGeometry_;
    bool hiddenToTray_{}, minimizeHidePending_{};
#ifdef REDOT_UI_VERIFICATION
    std::optional<bool> verificationTrayAvailable_;
#endif
    bool trayAvailable() const {
#ifdef REDOT_UI_VERIFICATION
        if(verificationTrayAvailable_)return *verificationTrayAvailable_;
#endif
        return !quiet() && QSystemTrayIcon::isSystemTrayAvailable();
    }
    void hideToTray(Qt::WindowStates restoreState) {
        trayRestoreState_=restoreState & ~Qt::WindowMinimized & ~Qt::WindowActive;
        trayRestoreGeometry_=normalGeometry();hiddenToTray_=true;
        if(settings_)settings_->hide();if(quick_)quick_->hide();if(diagnostics_)diagnostics_->hide();
        hide(); // No encoder, source or hotkey lifecycle change.
    }
    void restoreFromTray() {
        if(hiddenToTray_ || isMinimized()) {
            const auto geometry=hiddenToTray_?trayRestoreGeometry_:normalGeometry();
            const auto state=hiddenToTray_?trayRestoreState_:(windowState()&~Qt::WindowMinimized&~Qt::WindowActive);
            hiddenToTray_=false;
            setWindowState(Qt::WindowNoState);
            if(geometry.isValid())setGeometry(geometry);
            setWindowState(state);
        }
        showOwned(this); // Activation only for an explicit user Open command.
    }
    void updateTray(const FrontendState& s) {
        if(!tray_)return;
        const bool available=trayAvailable();
        if(!quiet() && tray_->isVisible()!=available)tray_->setVisible(available);
        if(hiddenToTray_&&!available) {
            // Explorer/notification-area loss must not strand an invisible app.
            hiddenToTray_=false;setWindowState(trayRestoreState_);setAttribute(Qt::WA_ShowWithoutActivating,true);
            show();setAttribute(Qt::WA_ShowWithoutActivating,quiet());
        }
        const auto text=trayOperation(s);const auto tip="ReDot · "+text;
        if(tray_->toolTip()!=tip)tray_->setToolTip(tip);
        if(trayState_->text()!=text)trayState_->setText(text);
        trayRecord_->setText(s.recording?"Stop Recording":"Start Recording");
        trayReplay_->setText(s.replay?"Disable Replay Buffer":"Enable Replay Buffer");
        traySave_->setEnabled(save_->isEnabled()&&!s.closing);
        trayRecord_->setEnabled(record_->isEnabled()&&!s.closing);
        trayReplay_->setEnabled(replay_->isEnabled()&&!s.closing);
        trayOpen_->setEnabled(!s.closing);trayExit_->setEnabled(!s.closing);
        setProperty("trayOperationalState",text);setProperty("trayAvailable",available);
        setProperty("trayAvailabilityReason",available?"Notification area available":"Notification area unavailable: minimize stays on the taskbar; Close exits safely.");
    }
    QAction *lock_{}, *previewAction_{};
    QTimer tick_, layoutDebounce_;
    QElapsedTimer healthClock_;
    SettingsStore layoutStore_{L"workspace-layout-v1.txt"};
    SettingsStore::Values layoutValues_;
    bool previewEnabled_{true}, syncing_{}, exiting_{};
    uint64_t tickMaxUs_{}, tickCount_{}, tickTotalUs_{};
    QDockWidget *dock(const QString &title, const QString &name, QWidget *contents, Qt::DockWidgetArea area)
    {
        auto d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(contents);
        if(title!="Controls"&&title!="Audio Mixer"){auto panelMaterial=new WorkspaceMaterial(contents);contents->installEventFilter(new DockMaterialLayout(panelMaterial,contents));}
        auto floatingMaterial=new FloatingGlassHost(d);
        auto header=new DashboardDockTitle(d);header->setFixedHeight(34);header->setObjectName("glassDockHeader");
        auto material=new WorkspaceMaterial(header);material->information(title,"",true);
        auto headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(10,2,5,2);
        auto label=new QLabel(title);label->setAttribute(Qt::WA_TransparentForMouseEvents);headerLayout->addWidget(label);headerLayout->addStretch();
        auto floating=new QToolButton;floating->setText("↗");floating->setAccessibleName("Float / dock "+title);floating->setToolTip("Float / dock "+title);
        auto close=new QToolButton;close->setText("×");close->setAccessibleName("Hide "+title);headerLayout->addWidget(floating);headerLayout->addWidget(close);
        
        QObject::connect(floating,&QToolButton::clicked,d,[d,floatingMaterial]{if(d->features().testFlag(QDockWidget::DockWidgetFloatable)){if(d->isFloating())floatingMaterial->detach();d->setFloating(!d->isFloating());}});
        QObject::connect(close,&QToolButton::clicked,d,&QDockWidget::hide);
        d->setTitleBarWidget(header);
        
        header->installEventFilter(new DockMaterialLayout(material,header));
        d->setAllowedAreas(Qt::AllDockWidgetAreas);
        addDockWidget(area, d);
        QObject::connect(d, &QDockWidget::dockLocationChanged, this, [this] { layoutDebounce_.start(); });
        QObject::connect(d, &QDockWidget::topLevelChanged, this, [this] { layoutDebounce_.start(); });
        QObject::connect(d, &QDockWidget::visibilityChanged, this, [this] { layoutDebounce_.start(); });
        return d;
    }
    void saveLayout()
    {
        if (!backend_)
            return;
        auto values = layoutValues_;
        values["qtDockState"] = QString::fromLatin1((scenesVisible_?captureDockState_:saveState(1)).toBase64()).toStdWString();
        values["qtGeometry"] = QString::fromLatin1(saveGeometry().toBase64()).toStdWString();
        values["qtLocked"] = lock_ && lock_->isChecked() ? L"1" : L"0";
        values["dashboardV1"] = L"1";values["advancedDocks"]=advanced_?L"1":L"0";
        layoutStore_.save(values);
    }
    void properties(int category)
    {
        if (!settings_)
            settings_ = new SettingsWindow(backend_.get(), this);
        settings_->category(category);
    }
    void defaults()
    {
        if(!advanced_){dashboard(false);return;}
        for (auto d : {sourceDock_, mixerDock_, controlDock_})
        {
            d->setFloating(false);
            d->show();
        }
        addDockWidget(Qt::LeftDockWidgetArea, sourceDock_);
        addDockWidget(Qt::BottomDockWidgetArea, mixerDock_);
        splitDockWidget(mixerDock_, controlDock_, Qt::Horizontal);
        resizeDocks({sourceDock_}, {220}, Qt::Horizontal);
        resizeDocks({mixerDock_, controlDock_}, {660, 260}, Qt::Horizontal);
        resizeDocks({mixerDock_}, {205}, Qt::Vertical);
        layoutDebounce_.start();
    }
    void diagnostics()
    {
        if (!diagnostics_)
        {
            diagnostics_ = new QDialog(this);
            DesktopBackdrop::prepare(diagnostics_);diagnosticsBackdrop_=new DesktopBackdrop(diagnostics_);
            diagnostics_->setWindowFlag(Qt::Window, true);
            diagnostics_->setWindowFlag(Qt::FramelessWindowHint, true);
            diagnostics_->setWindowTitle("ReDot · Diagnostics");diagnostics_->setWindowIcon(Branding::icon());
            diagnostics_->resize(900, 650);
            auto box = new QVBoxLayout(diagnostics_);
            box->setContentsMargins(6,0,6,8);box->addWidget(new ChromeTitle(diagnostics_,"ReDot · Diagnostics"));new WorkspaceGlassLayer(diagnostics_);
            diagnosticText_ = new QPlainTextEdit;
            diagnosticText_->setReadOnly(true);
            box->addWidget(diagnosticText_);
            auto bar = new QHBoxLayout;
            auto exportButton = new QPushButton("Export report");
            auto logs = new QPushButton("Open logs folder");
            bar->addWidget(exportButton);
            bar->addWidget(logs);
            bar->addStretch();
            box->addLayout(bar);
            QObject::connect(exportButton, &QPushButton::clicked, this, [this] { backend_->action(1030); });
            QObject::connect(logs, &QPushButton::clicked, this, [this] {
                if (!quiet())
                    backend_->action(2061);
            });
        }
        showOwned(diagnostics_);
        diagnosticText_->setPlainText(q(backend_->state(true).diagnostics));
    }
    void media()
    {
        if (!media_)
        {
            media_ = new QQuickView;
            media_->setFlag(Qt::FramelessWindowHint,true);
            if(quiet()){
                media_->setFlag(Qt::WindowDoesNotAcceptFocus);
                if(qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW"))media_->setFlag(Qt::WindowTransparentForInput);
            }
            auto format=media_->format();format.setAlphaBufferSize(8);media_->setFormat(format);media_->setColor(Qt::transparent);
            media_->setTransientParent(windowHandle());
            mediaBackdrop_=new DesktopBackdrop(media_);
            media_->setTitle("ReDot · View Media");media_->setIcon(Branding::icon());
            media_->setResizeMode(QQuickView::SizeRootObjectToView);
            media_->resize(1120, 820);
            media_->setMinimumSize({720, 540});
            auto chrome=new WindowChrome(media_,media_,[this]{return QRect(34,6,std::max(0,media_->width()-190),34);},[this]{return QRect(media_->width()-99,6,43,32);},[this](bool hover,bool pressed){if(auto button=visualItem(media_->rootObject(),"mediaWindow1")){button->setProperty("nativeHover",hover);button->setProperty("nativePressed",pressed);}});
            (void)chrome;
            auto images = new ClipImages;
            media_->engine()->addImageProvider("clips", images);
            mediaController_ = new MediaController(backend_.get(), media_, images);
            media_->rootContext()->setContextProperty("mediaController", mediaController_);
            DesignSystem::bind(media_->engine());
        media_->setSource(QUrl("qrc:/PandaClip/Media.qml"));
            QObject::connect(media_, &QWindow::visibleChanged, this, [this](bool visible) {
                if (!visible)
                    mediaController_->back();
            });
        }
        backend_->requestLibrary();
        if (quiet())
        {
            if(qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW")){
                auto hwnd=reinterpret_cast<HWND>(media_->winId());
                SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);
            }
            media_->show();
        }
        else
        {
            media_->show();
            media_->raise();
            media_->requestActivate();
        }
        mediaBackdrop_->update(backend_->state().transparency);mediaController_->tick();
    }
    QWidget *mixer()
    {
        auto container = new QWidget;
        auto vertical = new QHBoxLayout(container);audioLayout_=vertical;
        vertical->setContentsMargins(24, 8, 24, 8);
        vertical->setSpacing(0);
        auto sharedSurface=new WorkspaceMaterial(container);container->installEventFilter(new DockMaterialLayout(sharedSurface,container));
        auto strip = [&](bool mic) {
            auto panel = new QWidget;
            auto box = new QVBoxLayout(panel);
            box->setContentsMargins(18, 16, 18, 12);
            box->setSpacing(8);
            auto title = new QHBoxLayout;
            auto name = new QLabel(mic ? "Microphone" : "Game audio");
            name->setObjectName("channelTitle");
            name->setStyleSheet("font-size:16px;font-weight:600;");
            auto endpoint = new QLabel;
            endpoint->setMinimumWidth(10);
            endpoint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            endpoint->setWordWrap(false);endpoint->setMaximumHeight(24);endpoint->setStyleSheet("font-size:11px;");
            auto names=new QVBoxLayout;names->setSpacing(2);names->addWidget(name);names->addWidget(endpoint);
            auto props = gear(mic?4:3);
            auto meter = new LevelMeter;
            meter->compact();
            auto audioIcon=new QLabel;audioIcon->setPixmap(dashboardIcon(mic?8:7).pixmap(28,28));title->addWidget(audioIcon);
            title->addLayout(names,2);title->addWidget(meter,1);
            auto mute = new QPushButton;mute->setIcon(dashboardIcon(7));mute->setIconSize(QSize(22,22));mute->setCheckable(true);mute->setFixedSize(34,34);mute->setStyleSheet("padding:4px;border-radius:17px;");mute->setAccessibleName(mic?"Mute microphone":"Mute game audio");title->addWidget(mute);
            props->setFixedSize(34,34);title->addWidget(props);box->addLayout(title);
            auto line = new QHBoxLayout;
            auto gain = new QSlider(Qt::Horizontal);
            gain->setRange(0, 200);
            gain->setValue(100);
            auto gainText = new QLabel("100%");
            gainText->setFixedWidth(40);
            line->addWidget(gain, 1);
            line->addWidget(gainText);
            gain->setObjectName(mic?"microphoneGain":"gameGain");gain->setAccessibleName(mic?"Microphone recording gain":"Game recording gain");
            gain->setToolTip("Active scene gain, 0–200%. Applies live with a short gain transition; raw meters remain independent.");
            if (mic)
            {
                auto check = new QPushButton("Check Mic");
                check->setObjectName("checkMic");
                micCheck_ = check;
                line->addWidget(check);
                advancedAudio_<<check;
                QObject::connect(check, &QPushButton::clicked, this, [this] { backend_->action(1037); });
            }
            box->addLayout(line);
            QObject::connect(gain, &QSlider::valueChanged, this, [this, mic, gainText](int v) {
                gainText->setText(QString::number(v) + "%");
                if (!syncing_)
                    backend_->edit(mic ? 1020 : 1019, L"", v, false);
            });
            QObject::connect(mute, &QPushButton::toggled, this, [this, mic](bool on) {
                if (!syncing_)
                    backend_->edit(mic ? 1018 : 1017, L"", 0, on);
            });
            if (mic)
            {
                micName_ = endpoint;
                micGain_ = gain;
                micMute_ = mute;
                micMeter_ = meter;
            }
            else
            {
                gameName_ = endpoint;
                gameGain_ = gain;
                gameMute_ = mute;
                gameMeter_ = meter;
            }
            return panel;
        };
        vertical->addWidget(strip(false));
        auto divider=new QFrame;divider->setFrameShape(QFrame::VLine);divider->setFixedWidth(1);divider->setStyleSheet(QString("background:%1;margin-top:18px;margin-bottom:18px;").arg(DesignSystem::rgba("outline")));vertical->addWidget(divider);
        vertical->addWidget(strip(true));
        return container;
    }
    QWidget *controls()
    {
        auto container = new QWidget;auto outer=new QVBoxLayout(container);outer->setContentsMargins(0,0,0,0);
        auto card=new QWidget;card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);outer->addWidget(card);outer->addStretch();
        auto box = new QVBoxLayout(card);
        dashboardControlLayout_=box;
        box->setContentsMargins(8,78,22,18);box->setSpacing(16);
        record_=new GlassAction("Start Recording");record_->setObjectName("recordAction");
        replay_=new GlassAction("Enable Replay Buffer");replay_->setObjectName("replayAction");
        save_=new GlassAction("Save Clip");save_->setObjectName("saveAction");
        record_->setIcon(dashboardIcon(4));replay_->setIcon(dashboardIcon(5));save_->setIcon(dashboardIcon(6));
        std::array<QPushButton*,3> actions={record_,replay_,save_};
        for(int index=0;index<3;++index){
            auto row=new QHBoxLayout;row->setSpacing(12);actions[index]->setIconSize(QSize(24,24));actions[index]->setMinimumHeight(62);actions[index]->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            actions[index]->setStyleSheet("padding:10px 12px;border-radius:24px;");row->addWidget(actions[index],1);auto settings=gear(index);settings->setFixedSize(50,62);row->addWidget(settings);box->addLayout(row);
        }
        time_=new QLabel("Recording idle");history_=new QLabel("Replay off");shortcut_=new QLabel;
        time_->setStyleSheet("font-size:34px;font-weight:300;");time_->setWordWrap(false);history_->setWordWrap(true);shortcut_->setStyleSheet("font-size:12px;");
        box->addSpacing(12);box->addWidget(history_);box->addWidget(shortcut_);
        auto divider=new QFrame;divider->setFixedHeight(1);divider->setStyleSheet(QString("background:%1;").arg(DesignSystem::rgba("outline")));box->addSpacing(14);box->addWidget(divider);box->addWidget(time_);
        saveStatus_=new QLabel;saveStatus_->setWordWrap(true);saveStatus_->setMaximumHeight(110);box->addWidget(saveStatus_);
        appearanceBadge_=new QLabel;appearanceBadge_->setWordWrap(true);appearanceBadge_->setStyleSheet("font-size:11px;");
        box->addStretch();box->addWidget(appearanceBadge_);appearanceBadge_->hide();
        transparencySettings_=new QPushButton("Open Windows transparency settings");transparencySettings_->setObjectName("openWindowsTransparencySettings");transparencySettings_->setStyleSheet("font-size:11px;padding:2px 8px;border-radius:11px;");transparencySettings_->setFixedHeight(22);transparencySettings_->hide();
        QObject::connect(transparencySettings_,&QPushButton::clicked,this,[]{if(!quiet())QDesktopServices::openUrl(QUrl("ms-settings:easeofaccess-visualeffects"));});
        auto start=[this](int id){
            auto state=backend_->state();
            backend_->action(id);tick();
        };
        QObject::connect(record_,&QPushButton::clicked,this,[start]{start(1010);});
        QObject::connect(replay_,&QPushButton::clicked,this,[start]{start(1023);});
        QObject::connect(save_,&QPushButton::clicked,this,[this]{backend_->action(1024);tick();});
        return container;
    }
    void tick()
    {
        if (!healthClock_.isValid() || healthClock_.elapsed() >= 500)
        {
            backend_->poll();
            healthClock_.start();
        }
        if (!backend_->alive())
        {
            if(sceneModel_){sceneModel_->flush();if(!sceneModel_->idle())return;}
#ifdef REDOT_UI_VERIFICATION
            if (checkReport_.isOpen())
            {
                checkReport_.write("EXIT controller destroyed; Qt quits\n");
                checkReport_.flush();
            }
#endif
            qApp->quit();
            return;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        auto s = backend_->state();
        if(brandIcon_)brandIcon_->update(static_cast<Branding::Mode>(s.taskbarMode));
#ifdef REDOT_UI_VERIFICATION
        verifyBrand(s);
#endif
        setProperty("applicationTransparencyEnabled",s.transparency);
        backdrop_->update(s.transparency);
        qApp->setProperty("solidMaterials",!s.transparency);
        titleBar_->material(s.transparency);
        QString materialState=s.recording?"Recording":s.replay?"Replay":s.pendingStart?"Waiting for consent":"Ready";
        titleBar_->information(s.selectedSource.empty()?"ReDot":q(s.selectedSource),materialState,s.transparency);
        for(auto button:controlDock_->findChildren<QPushButton*>())if(auto glass=dynamic_cast<GlassAction*>(button))glass->information(q(s.selectedSource),materialState,s.transparency);
        for(auto dock:{sourceDock_,mixerDock_,controlDock_})for(auto material:dock->titleBarWidget()->findChildren<QWidget*>())if(auto surface=dynamic_cast<WorkspaceMaterial*>(material))surface->information(q(s.selectedSource),materialState,s.transparency);
        syncing_ = true;
        if (previewAction_->isChecked() != s.previewEnabled)
        {
            QSignalBlocker block(previewAction_);
            previewAction_->setChecked(s.previewEnabled);
        }
        previewStack_->setCurrentIndex(s.selectedSource.empty()?2:!s.previewEnabled?1:s.previewReady?0:2);
        labelText(previewInfo_,q(s.videoDescription));previewInfo_->setToolTip(q(s.videoDescription));previewToggle_->setText(s.previewEnabled?"Hide preview":"Show preview");
        if(scenes_)scenes_->runtimeStatus(backend_->sceneStatus().sources);
        const auto selected=s.selectedSource.empty()?QString("Select scene"):q(s.selectedSource);
        sourceSelector_->setText(sourceSelector_->fontMetrics().elidedText(selected,Qt::ElideRight,std::max(120,sourceSelector_->width()-24)));sourceSelector_->setToolTip(selected);
        labelText(sourceSummary_,s.recovery?"Recovering…":s.capture?"● Live":s.encoding?"● Encoding":s.pendingStart?"Starting…":s.selectedSource.empty()?"No source":"Ready");
        if(emptyHeading_)labelText(emptyHeading_,s.selectedSource.empty()?"Select a scene":s.recovery?"Waiting for the selected source":"Preview awaits a capture frame");
        const bool requested=property("desktopBackdropMaterial").toBool();
        labelText(appearanceBadge_,requested?"Native Acrylic requested":s.transparency?(property("desktopBackdropStatus").toString().contains("Transparency effects are off")?"Windows transparency off · solid appearance":"Solid backdrop fallback"):"Reduced transparency");appearanceBadge_->setToolTip(property("desktopBackdropStatus").toString());
        appearanceBadge_->setVisible(!requested&&s.transparency&&!compactControlHeight_);
        transparencySettings_->setVisible(property("windowsTransparencyDisabled").toBool()&&s.transparency);
        bool draining = s.recording && !backend_->control(1011).enabled;
        record_->setText(s.pendingStart ? "Starting…" : draining      ? "Finalizing..."
                         : s.recording&&!s.encoderReady?"Starting recording…"
                         : s.recording ? "Stop Recording"
                         : s.closing   ? "Finalizing…"
                                       : "Start Recording");
        record_->setEnabled(!s.pendingStart && !s.closing && !draining && (s.recording || backend_->control(1010).enabled));
        if (record_->property("recording").toBool() != s.recording)
        {
            record_->setProperty("recording", s.recording);
            record_->style()->unpolish(record_);
            record_->style()->polish(record_);
        }
        replay_->setText(s.pendingStart?"Starting…":s.replay ? "Stop Replay Buffer" : "Enable Replay Buffer");
        replay_->setEnabled(!s.pendingStart && !s.closing && backend_->control(1023).enabled);
        bool canSave = s.replay && s.historySeconds > 0 && s.pendingSaves < 2 && !s.closing;
        save_->setEnabled(canSave);
        save_->setToolTip(canSave               ? "Save available compressed history without interrupting capture"
                          : !s.replay           ? "Enable Replay before saving a clip"
                          : s.pendingSaves >= 2 ? "Two exports are pending. Wait for an export to finish."
                                                : "Replay is warming up. Wait for encoded history.");
        labelText(time_,s.recording?q(s.elapsed):"00:00:00");time_->setToolTip(draining?"Finalizing recording":s.recording?"Recording elapsed time":"Recording idle");
        const auto requestedSeconds=backend_->control(1025).text;bool validSeconds{};const int targetSeconds=q(requestedSeconds).toInt(&validSeconds);
        const bool fullHistory=validSeconds&&s.historySeconds+2>=targetSeconds;
        labelText(history_,s.recovery?"Replay recovery · warming after restart":s.replay?(fullHistory?QString("● Replay ready · %1 s").arg(s.historySeconds,0,'f',1):QString("Replay warming · %1 / %2 s").arg(s.historySeconds,0,'f',1).arg(validSeconds?targetSeconds:60)):"Replay off");
        if(canSave)save_->setToolTip(QString("Save available history (%1 s); requested %2 s. Keyframes/AAC boundaries determine actual duration; capture continues.").arg(s.historySeconds,0,'f',1).arg(validSeconds?targetSeconds:60));
        labelText(shortcut_,nativeSequence(WORD(backend_->control(2200).value)).isEmpty()?"Save shortcut not assigned":nativeSequence(WORD(backend_->control(2200).value)).toString(QKeySequence::NativeText));
        shortcut_->setToolTip(q(backend_->control(2300).text));
        history_->setToolTip(QString("%1 / %2 MiB compressed packets, including retained saves")
                                 .arg(s.replayBytes / 1048576.0, 0, 'f', 1)
                                 .arg(s.replayCap / 1048576.0, 0, 'f', 0));
        labelText(saveStatus_, s.pendingSaves ? QString("Saving %1 clip(s)…").arg(s.pendingSaves)
                               : q(s.replayStatus).startsWith("Saved clip:") ? "Clip saved"
                               : q(s.replayStatus).contains("failed",Qt::CaseInsensitive)?"Clip save failed · "+q(s.replayStatus)
                               : s.recovery                                  ? "Recovering · replay will warm again"
                                                                             : "");
        saveStatus_->setToolTip(q(s.replayStatus));
        QString mode = s.recording ? "REC " + q(s.elapsed)
                       : s.replay  ? "REPLAY · " + QString::number(s.historySeconds, 'f', 1) + " s"
                                   : "Idle";
        QString message = (s.recovery || (!s.recording && !s.replay)) ? q(s.captureStatus) : q(s.recordStatus);
        if (!s.recording && !s.replay)
        {
            if (message.startsWith("Saved MP4") || message.startsWith("Saved MKV"))
                message = "Recording saved";
        }
        if(s.pendingStart)message="Waiting for Windows consent / microphone shutdown. Cancel in Capture source. "+q(s.borderStatus);
        if(!s.encoding&&!s.pendingStart&&q(s.captureStatus).startsWith("Choose an open window"))message=q(s.captureStatus);
        if(s.replay&&!s.recording&&!s.pendingStart){auto replayText=q(s.replayStatus);message=replayText.startsWith("Saved clip:")?"Clip saved":replayText.contains("fail",Qt::CaseInsensitive)?replayText:s.historySeconds>0?"Replay capture active":"Replay warming up";}
        const bool saved=q(s.recordStatus).startsWith("Saved MP4")||q(s.recordStatus).startsWith("Saved MKV")||q(s.replayStatus).startsWith("Saved clip:");
        const bool failure=message.contains("fail",Qt::CaseInsensitive)||message.contains("error",Qt::CaseInsensitive)||message.startsWith("Choose an open window");
        labelText(status_,failure||s.pendingStart||s.recovery?message:s.pendingSaves?"Saving clip…":saved&&!s.recording?"● All files saved":s.recording?"● Recording":s.replay?"● Replay active":"Ready");
        status_->setToolTip(q(s.captureStatus) + "\n" + q(s.recordStatus) + "\n" + q(s.replayStatus)+"\n"+property("desktopBackdropStatus").toString());
        const auto game = backend_->control(1040), mic = backend_->control(1015);
        sources_->topLevelItem(0)->setText(0, s.selectedSource.empty() ? "No window selected" : q(s.selectedSource));
        sources_->topLevelItem(0)->setToolTip(0, q(s.selectedSource));
        sources_->topLevelItem(0)->setText(1, s.capture ? "Capturing" : s.selectedSource.empty() ? "Select" : "Ready");
        sources_->topLevelItem(1)->setDisabled(!game.enabled);
        sources_->topLevelItem(2)->setDisabled(!mic.enabled);
        sources_->topLevelItem(1)->setCheckState(0, game.checked ? Qt::Checked : Qt::Unchecked);
        sources_->topLevelItem(2)->setCheckState(0, mic.checked ? Qt::Checked : Qt::Unchecked);
        sources_->topLevelItem(1)->setText(1, game.checked ? "Enabled" : "Off");
        sources_->topLevelItem(2)->setText(1, mic.checked ? "Enabled" : "Off");
        sources_->topLevelItem(1)->setToolTip(0, q(s.gameStatus));
        sources_->topLevelItem(2)->setToolTip(0, q(s.micStatus));
        capture_->setText(s.pendingStart ? "Cancel Start" : s.capture ? "Stop Capture" : "Start Capture");
        capture_->setEnabled(!s.encoding && !s.closing && (s.pendingStart||s.capture || backend_->control(1002).enabled));
        gameMeter_->signal(s.gameOpen?s.gameMeter.displayedRms:0,s.gameOpen?s.gameMeter.displayedPeak:0);
        micMeter_->signal(s.micOpen?s.micMeter.displayedRms:0,s.micOpen?s.micMeter.displayedPeak:0);
#ifdef REDOT_UI_VERIFICATION
        appliedMicMeter_=s.micMeter;appliedGameMeter_=s.gameMeter;appliedMicOpen_=s.micOpen;appliedGameOpen_=s.gameOpen;
#endif
        auto gain = backend_->control(1019), mgain = backend_->control(1020);
        gameGain_->setEnabled(gain.enabled);
        micGain_->setEnabled(mgain.enabled);
        if (!gameGain_->isSliderDown())
            gameGain_->setValue(gain.value);
        if (!micGain_->isSliderDown())
            micGain_->setValue(mgain.value);
        gameMute_->setChecked(backend_->control(1017).checked);
        micMute_->setChecked(backend_->control(1018).checked);
        if(gameMute_->icon().cacheKey()!=dashboardIcon(7,gameMute_->isChecked()).cacheKey())gameMute_->setIcon(dashboardIcon(7,gameMute_->isChecked()));gameMute_->setToolTip(gameMute_->isChecked()?"Game audio muted · click to unmute":"Mute game audio");
        if(micMute_->icon().cacheKey()!=dashboardIcon(7,micMute_->isChecked()).cacheKey())micMute_->setIcon(dashboardIcon(7,micMute_->isChecked()));micMute_->setToolTip(micMute_->isChecked()?"Microphone muted · click to unmute":"Mute microphone");
        auto micCheck = backend_->control(1037);
        micCheck_->setText(q(micCheck.text));
        micCheck_->setEnabled(micCheck.enabled);
        gameMute_->setEnabled(backend_->control(1017).enabled);
        micMute_->setEnabled(backend_->control(1018).enabled);
        auto sourceActivity=[](bool enabled,bool active,bool open,const AudioMeterSnapshot& m){
            if(!enabled&&!open)return QString("Disabled");
            if(!active&&!open)return QString("Capture inactive");
            if(!open)return QString("Device unavailable / opening");
            return m.displayedPeak>0.00003f?QString("Signal detected"):QString("Open · silent");
        };
        labelText(gameName_, q(backend_->control(1014).text).remove("Game audio: ")+" · "+sourceActivity(s.gameEnabled,s.encoding,s.gameOpen,s.gameMeter));
        auto micDevice = backend_->control(1016);
        labelText(micName_, (s.openedMic.empty() ? q(micDevice.text) : "Open: " + q(s.openedMic))+" · "+sourceActivity(s.micEnabled,s.encoding,s.micOpen,s.micMeter));
        micName_->setToolTip(q(s.micStatus));
        gameName_->setToolTip(q(s.gameStatus)+"\nGame meter requires recording or replay; preview alone does not open game audio.");
        syncing_ = false;
        updateTray(s);
        if (media_ && media_->isVisible()){
            mediaBackdrop_->update(s.transparency);mediaController_->tick();
        }
        if (diagnostics_ && diagnostics_->isVisible())
        {
            diagnosticsBackdrop_->update(s.transparency);
            auto text = q(backend_->state(true).diagnostics)+"\nCapture appearance: "+property("desktopBackdropStatus").toString();
            if(media_)text+="\nMedia appearance: "+media_->property("desktopBackdropStatus").toString();
            if (diagnosticText_->toPlainText() != text)
                diagnosticText_->setPlainText(text);
        }
        if (exiting_ && s.closing)
            status_->setText("Finalizing recording, replay exports and settings…");
        auto us = static_cast<uint64_t>(elapsed.nsecsElapsed() / 1000);
        tickMaxUs_ = std::max(tickMaxUs_, us);
        tickTotalUs_ += us;
        ++tickCount_;
    }

  public:
    explicit Workspace(HINSTANCE instance)
    {
        DesktopBackdrop::prepare(this);backdrop_=new DesktopBackdrop(this);
        if(quiet()&&qEnvironmentVariableIntValue("PANDACLIP_TEST_PASSIVE_WINDOW")){
            setWindowFlag(Qt::WindowTransparentForInput);setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        }
        setWindowFlag(Qt::FramelessWindowHint,true);
        setWindowTitle("ReDot");
        setObjectName("workspace");
        setMinimumSize(860, 620);
        resize(1404, 828);
        // Establish the RHI backing-store mode before creating the preview HWND
        // or binding custom chrome to the root native window.
        new WorkspaceGlassLayer(this);
        setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
        setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);setCorner(Qt::BottomRightCorner,Qt::BottomDockWidgetArea);
        canvas_ = new NativeCanvas;
        previewStack_ = new QStackedWidget;
        previewStack_->addWidget(canvas_);
        auto placeholder = new QLabel("Preview hidden\nRecording and replay continue independently.");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(QString("background:%1;color:%2;").arg(DesignSystem::rgba("videoBlack"),DesignSystem::rgba("secondary")));
        previewStack_->addWidget(placeholder);
        auto empty=new QWidget;auto emptyBox=new QVBoxLayout(empty);emptyBox->addStretch();auto emptyHelp=new QLabel("Choose a scene; configure its sources in Scenes.");emptyHeading_=emptyHelp;emptyHelp->setAlignment(Qt::AlignCenter);emptyHelp->setWordWrap(true);emptyHelp->setStyleSheet("font-size:18px;");emptyBox->addWidget(emptyHelp);
        auto choose=new QPushButton("Select scene");choose->setObjectName("emptySourceSelect");emptyBox->addWidget(choose,0,Qt::AlignHCenter);emptyBox->addStretch();previewStack_->addWidget(empty);
        auto capturePage=new QWidget;auto pageBox=new QVBoxLayout(capturePage);pageBox->setContentsMargins(24,0,16,8);pageBox->setSpacing(16);
        auto selected=new SourceSelector("Select scene");auto sourceRow=new QHBoxLayout(selected);sourceRow->setContentsMargins(12,3,8,3);auto material=new WorkspaceMaterial(selected);selected->installEventFilter(new DockMaterialLayout(material,selected));
        sourceSelector_=selected;sourceSelector_->setObjectName("sourceSelector");sourceSelector_->setProperty("materialSuppressed",true);sourceSelector_->setStyleSheet("font-size:14px;font-weight:500;background:transparent;border:0;");sourceSelector_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);sourceSelector_->setMinimumHeight(40);sourceRow->addStretch();
        sourceSelector_->setIcon(dashboardIcon(0));sourceSelector_->setIconSize(QSize(26,26));
        sourceSummary_=new QLabel("No source");sourceSummary_->setAttribute(Qt::WA_TransparentForMouseEvents);sourceSummary_->setStyleSheet("font-size:12px;");sourceRow->addWidget(sourceSummary_);auto sourceArrow=new QLabel("⌄");sourceArrow->setStyleSheet("font-size:20px;");sourceArrow->setAttribute(Qt::WA_TransparentForMouseEvents);sourceRow->addWidget(sourceArrow);selected->setFixedSize(340,56);pageBox->addWidget(selected,0,Qt::AlignHCenter);
        QObject::connect(sourceSelector_,&QPushButton::clicked,this,[this]{quickSettings(5,sourceSelector_);});QObject::connect(choose,&QPushButton::clicked,sourceSelector_,&QPushButton::click);
        pageBox->addWidget(previewStack_,1);
        previewInfo_=new QLabel;previewInfo_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);previewToggle_=new QPushButton("Hide preview");previewToggle_->setObjectName("previewVisibility");previewToggle_->setMaximumHeight(26);pageBox->addWidget(previewToggle_,0,Qt::AlignRight);
        pages_=new QStackedWidget;pages_->addWidget(capturePage);setCentralWidget(pages_);
        sceneModel_=new Scenes::Model(Scenes::configurationPath(),this);
        scenes_=new Scenes::Page(sceneModel_);pages_->addWidget(scenes_);
        backend_ = makeFrontendBackend(instance, reinterpret_cast<HWND>(canvas_->winId()));
        canvas_->backend = backend_.get();
        backend_->onSceneAudioEdit([this](int id,int value,bool checked){
            bool mic=id==1018||id==1020;auto scene=sceneModel_->current();if(!scene)return;auto assignments=scene->audio;
            for(auto a:assignments)if(auto definition=sceneModel_->definition(a.sourceId);definition&&(definition->kind==Scenes::Kind::Microphone)==mic){if(id==1017||id==1018)a.muted=checked;else a.gain=value/100.;sceneModel_->updateAudio(a);}
        });
        auto configureScene=[this]{
            if(!sceneModel_->ready())return;
            if(sceneModel_->data().migrationVersion<2){sceneModel_->migrateCapture(backend_->legacySceneSettings());return;}
            backend_->scene(sceneModel_->snapshot());
        };
        QObject::connect(sceneModel_,&Scenes::Model::changed,this,configureScene);
        QObject::connect(sceneModel_,&Scenes::Model::visualChanged,this,configureScene);
        scenes_->canvas()->bindPresentation([this](HWND target,const QRectF& viewport,const QVector<QPolygonF>& lines,const QVector<QPointF>& handles){return backend_->scenePreview(target,viewport,lines,handles);});
        if(sceneModel_->ready())configureScene();
        backend_->onVisibilityShortcut([this] {
            if (quiet())
                return;
            if (isVisible())
                hide();
            else
                restoreFromTray();
        });
        auto sourcePanel = new QWidget;
        auto sourceBox = new QVBoxLayout(sourcePanel);
        sourceBox->setContentsMargins(6, 6, 6, 6);
        sources_ = new QTreeWidget;
        sources_->setObjectName("sourceList");
        sources_->setColumnCount(2);
        sources_->setHeaderLabels({"Source", "State"});
        sources_->setRootIsDecorated(false);
        sources_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sources_->setColumnWidth(0, 148);
        sources_->setColumnWidth(1, 68);
        for (auto name : {"Selected scene", "Game audio", "Microphone"})
        {
            auto item = new QTreeWidgetItem(sources_, {name, "Off"});
            if (QString(name) != "Selected window")
                item->setCheckState(0, Qt::Unchecked);
        }
        sources_->setMaximumHeight(150);
        sources_->setTextElideMode(Qt::ElideRight);
        sources_->header()->setStretchLastSection(false);
        sources_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        sources_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        sourceBox->addWidget(sources_);
        auto props = new QPushButton("Source Properties");
        capture_ = new QPushButton("Start Capture");
        sourceBox->addWidget(props);
        sourceBox->addWidget(capture_);
        sourceBox->addStretch(1);
        QObject::connect(props, &QPushButton::clicked, this,
                         [this] { properties(sources_->indexOfTopLevelItem(sources_->currentItem()) > 0 ? 4 : 1); });
        QObject::connect(sources_, &QTreeWidget::itemDoubleClicked, this,
                         [this](QTreeWidgetItem *i, int) { properties(sources_->indexOfTopLevelItem(i) > 0 ? 4 : 1); });
        QObject::connect(sources_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *i, int) {
            if (syncing_)
                return;
            int row = sources_->indexOfTopLevelItem(i);
            if (row > 0)
                backend_->edit(row == 1 ? 1040 : 1015, L"", 0, i->checkState(0) == Qt::Checked);
        });
        QObject::connect(capture_, &QPushButton::clicked, this, [this] { backend_->action(backend_->state().pendingStart?1003:1002);tick(); });
        sourceDock_ = dock("Sources", "sourcesDock", sourcePanel, Qt::LeftDockWidgetArea);
        mixerDock_ = dock("Audio Mixer", "mixerDock", mixer(), Qt::BottomDockWidgetArea);
        controlDock_ = dock("Controls", "controlsDock", controls(), Qt::BottomDockWidgetArea);
        navigation_=new QWidget;navigation_->setObjectName("dashboardNavigation");auto navigationBox=new QVBoxLayout(navigation_);navigationBox->setContentsMargins(10,18,10,12);navigationBox->setSpacing(12);
        const std::array<QString,4> names={"Capture","Scenes","Media","Settings"};
        const std::array<int,4> iconKinds={0,12,1,2};
        for(int index=0;index<4;++index){auto button=new DashboardNavigation(names[index]);button->setObjectName(QString("navigation%1").arg(index));button->setAccessibleName(names[index]);button->setIcon(dashboardIcon(iconKinds[index]));button->setProperty("materialSuppressed",true);button->setIconSize(QSize(26,26));button->setMinimumHeight(82);button->setProperty("dashboardNavigation",true);button->setCheckable(index<2);button->setChecked(index==0);navigationButtons_[index]=button;navigationBox->addWidget(button);}
        navigationBox->addStretch();navigationDock_=new QDockWidget(this);navigationDock_->setObjectName("navigationDock");navigationDock_->setTitleBarWidget(new DashboardDockTitle(navigationDock_));navigationDock_->titleBarWidget()->setProperty("dashboardCollapsed",true);navigationDock_->titleBarWidget()->setFixedHeight(0);navigationDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);navigationDock_->setWidget(navigation_);navigationDock_->setFixedWidth(126);addDockWidget(Qt::LeftDockWidgetArea,navigationDock_);
        QObject::connect(navigationButtons_[0],&QPushButton::clicked,this,[this]{if(settings_)settings_->hide();if(diagnostics_)diagnostics_->hide();scenePage(false);navigationButtons_[0]->setChecked(true);});
        QObject::connect(navigationButtons_[1],&QPushButton::clicked,this,[this]{scenePage(true);navigationButtons_[1]->setChecked(true);});
        QObject::connect(navigationButtons_[2],&QPushButton::clicked,this,[this]{media();});QObject::connect(navigationButtons_[3],&QPushButton::clicked,this,[this]{properties(0);});
        auto file = menuBar()->addMenu("File");
        file->addAction("Open output folder", this, [this] {
            if (!quiet())
                QDesktopServices::openUrl(QUrl::fromLocalFile(q(backend_->state().folder)));
        });
        file->addAction("Settings…", this, [this] { properties(0); });
        file->addSeparator();
        file->addAction("Exit", this, [this] {
            exiting_ = true;
            close();
        });
        auto view = menuBar()->addMenu("View");
        view->addAction("Capture",this,[this]{scenePage(false);});
        view->addAction("Scenes",this,[this]{scenePage(true);});
        view->addAction("View Media", this, [this] { media(); });
        previewAction_ = view->addAction("Enable preview");
        previewAction_->setCheckable(true);
        previewAction_->setChecked(backend_->control(2028).checked);
        previewStack_->setCurrentIndex(previewAction_->isChecked() ? 0 : 1);
        QObject::connect(previewAction_, &QAction::toggled, this, [this](bool on) {
            previewEnabled_ = on;
            previewStack_->setCurrentIndex(on ? 0 : 1);
            backend_->previewVisible(on);
        });
        QObject::connect(previewToggle_,&QPushButton::clicked,this,[this]{previewAction_->setChecked(!previewAction_->isChecked());});
        view->addAction("Preview scaling: fit to canvas", this, [this] { backend_->previewResize(); });
        auto docks = menuBar()->addMenu("Docks");
        advancedAction_=docks->addAction("Advanced dock layout");advancedAction_->setCheckable(true);
        QObject::connect(advancedAction_,&QAction::toggled,this,[this](bool value){dashboard(value);});
        for (auto d : {sourceDock_, mixerDock_, controlDock_})
            docks->addAction(d->toggleViewAction());
        lock_ = docks->addAction("Lock Layout");
        lock_->setCheckable(true);
        QObject::connect(lock_, &QAction::toggled, this, [this](bool lock) {
            unsetCursor();
            for (auto d : {sourceDock_, mixerDock_, controlDock_})
                d->setFeatures(lock ? QDockWidget::DockWidgetClosable
                                    : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                                          QDockWidget::DockWidgetFloatable);
            layoutDebounce_.start();
        });
        docks->addAction("Reset Layout", this, [this] {
            lock_->setChecked(false);
            defaults();
        });
        auto tools = menuBar()->addMenu("Tools");
        tools->addAction("Diagnostics…", this, [this] { diagnostics(); });
        tools->addAction("Check selected microphone", this, [this] { backend_->action(1037); });
        tools->addAction("Refresh audio devices", this, [this] { backend_->action(2037); });
        auto help = menuBar()->addMenu("Help");
        help->addAction("README", this, [] {
            if (!quiet())
                QDesktopServices::openUrl(QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/README.md"));
        });
        help->addAction("About ReDot", this, [this] {
            if (quiet())
                return;
            QMessageBox::about(
                this, "About ReDot",
                "<b>ReDot " REDOT_VERSION "</b><br>ReDot: custom WGC / Direct3D 11 / NVIDIA NVENC / WASAPI engine.<br><br>Frontend: dynamically "
                "linked Qt " QT_VERSION_STR
                " under LGPLv3; View Media uses Qt Multimedia and dynamically linked FFmpeg 7.1 under LGPL2.1+. "
                "Capture/encoding do not use FFmpeg. You may replace compatible libraries.<br>License texts, corresponding source "
                "archives and replacement instructions are included alongside the executable in licenses/Qt and "
                "QT-NOTICE.txt.<br><a href='https://doc.qt.io/qt-6/licensing.html'>Qt licensing</a><br><br>The View "
                "Media material is a Windows adaptation inspired by Apple Liquid Glass; it is rendered by Qt Quick and "
                "is not a native Apple material.");
        });
        auto menu=menuBar();menu->setObjectName("workspaceMenuBar");auto header=new QWidget;header->setStyleSheet("background:transparent;");auto headerBox=new QVBoxLayout(header);headerBox->setSpacing(0);headerBox->setContentsMargins(0,0,0,0);
        titleBar_=new ChromeTitle(this,"ReDot");headerBox->addWidget(titleBar_);headerBox->addWidget(menu);menu->hide();auto options=new QMenu(this);for(auto action:menu->actions())options->addAction(action);titleBar_->options(options);setMenuWidget(header);
        status_ = new QLabel("Ready");
        status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewInfo_->setContentsMargins(24,0,0,0);status_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);status_->setContentsMargins(0,0,24,0);
        statusBar()->addWidget(previewInfo_, 1);statusBar()->addPermanentWidget(transparencySettings_);statusBar()->addPermanentWidget(status_,2);
        statusBar()->setSizeGripEnabled(true);
        statusBar()->setMaximumHeight(28);
        layoutDebounce_.setInterval(750);
        layoutDebounce_.setSingleShot(true);
        QObject::connect(&layoutDebounce_, &QTimer::timeout, this, [this] { saveLayout(); });
        std::wstring warning;
        layoutValues_ = layoutStore_.load(warning);
        defaults();
        if(layoutValues_.contains("dashboardV1")&&layoutValues_["advancedDocks"]==L"1"){advancedAction_->setChecked(true);}
        if (advanced_&&layoutValues_.contains("dashboardV1")&&layoutValues_.contains("qtDockState"))
        {
            auto state = QByteArray::fromBase64(q(layoutValues_["qtDockState"]).toLatin1());
            if (state.size() < 65536)
                restoreState(state, 1);
        }
        if (layoutValues_.contains("qtGeometry"))
            restoreGeometry(QByteArray::fromBase64(q(layoutValues_["qtGeometry"]).toLatin1()));
        if (layoutValues_.contains("qtLocked"))
            lock_->setChecked(layoutValues_["qtLocked"] == L"1");
        {
            // One neutral icon for this workspace's lifetime. Quiet verification
            // exercises its commands without registering in the personal shell.
            tray_ = new QSystemTrayIcon(Branding::icon(), this);
            tray_->setObjectName("redotSystemTray");
            auto trayMenu = new QMenu(this);
            trayState_=trayMenu->addAction("Idle");trayState_->setEnabled(false);trayMenu->addSeparator();
            trayOpen_=trayMenu->addAction("Open ReDot", this, [this] { restoreFromTray(); });
            traySave_=trayMenu->addAction("Save Clip", this, [this] { backend_->action(1024); });
            trayRecord_=trayMenu->addAction("Start Recording", this, [this] { backend_->action(1010); });
            trayReplay_=trayMenu->addAction("Enable Replay Buffer", this, [this] { backend_->action(1023); });
            trayMenu->addSeparator();
            trayExit_=trayMenu->addAction("Exit", this, [this] {
                exiting_ = true;
                close();
            });
            tray_->setContextMenu(trayMenu);
            QObject::connect(trayMenu,&QMenu::aboutToShow,this,[this]{updateTray(backend_->state());});
            QObject::connect(tray_, &QSystemTrayIcon::activated, this,
                             [this](QSystemTrayIcon::ActivationReason reason) {
                                 if (reason == QSystemTrayIcon::DoubleClick)
                                      restoreFromTray();
                             });
            tray_->setToolTip("ReDot · Idle");
            if(trayAvailable())tray_->show();
            backend_->onShortcutNotice([this](const std::wstring &text) {
                if (!quiet() && tray_)
                    tray_->showMessage("ReDot", q(text), QSystemTrayIcon::Warning, 8000);
            });
        }
        brandIcon_=std::make_unique<Branding::TaskbarIcon>(this);
        tick_.setInterval(200);
        QObject::connect(&tick_, &QTimer::timeout, this, [this] { tick(); });
        tick_.start();
        tick();
#ifdef REDOT_UI_VERIFICATION
        if (quiet() && !qEnvironmentVariableIsEmpty("PANDACLIP_QT_TEST_ROOT"))
            QTimer::singleShot(900, this, [this] { beginChecks(); });
#endif
    }
#ifdef REDOT_UI_VERIFICATION
#include "../tests/qt_frontend_checks.inl"
#endif
    ~Workspace() override
    {
        if(quick_){quick_->visibilityChanged={};quick_->hide();delete quick_;quick_=nullptr;}
#ifdef REDOT_UI_VERIFICATION
        checkReport_.write("EXIT workspace destructor begin\n");
        checkReport_.flush();
#endif
        if (media_)
        {
            mediaController_->back();
            delete media_;
        }
#ifdef REDOT_UI_VERIFICATION
        checkReport_.write("EXIT media destroyed\n");
        checkReport_.flush();
#endif
        canvas_->backend = nullptr;
    }

  protected:
    void changeEvent(QEvent* event) override {
        QMainWindow::changeEvent(event);
        if(event->type()!=QEvent::WindowStateChange || !backend_ || exiting_)return;
        auto state=static_cast<QWindowStateChangeEvent*>(event);
        if(isMinimized() && backend_->state().minimizeToTray && trayAvailable()) {
            minimizeRestoreState_=state->oldState();
            // showMinimized() completes its show operation after delivering
            // WindowStateChange. Hiding inside that event can be undone by Qt.
            // Coalesce and defer until the show has completed; a quick restore,
            // changed preference or notification-area loss cancels the hide.
            if(!minimizeHidePending_){minimizeHidePending_=true;QTimer::singleShot(0,this,[this]{
                minimizeHidePending_=false;
                if(!exiting_&&backend_->alive()&&isMinimized()&&backend_->state().minimizeToTray&&trayAvailable())hideToTray(minimizeRestoreState_);
            });}
        }
    }
    bool event(QEvent *event) override
    {
        if (lock_ && lock_->isChecked() &&
            (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
             event->type() == QEvent::MouseButtonRelease))
            return true;
        return QMainWindow::event(event);
    }
    void resizeEvent(QResizeEvent* event)override{
        QMainWindow::resizeEvent(event);
        updateControlDensity();
        if(navigationDock_){const bool compact=width()<1100;navigationDock_->setFixedWidth(compact?66:126);
            const std::array<QString,4> names={"Capture","Scenes","Media","Settings"};for(int i=0;i<4;++i){navigationButtons_[i]->setText(compact?"":names[i]);navigationButtons_[i]->setToolTip(names[i]);}}
        if(quick_&&quick_->isVisible())quick_->hide();
    }
    void closeEvent(QCloseEvent *event) override
    {
        if (!backend_->alive())
        {
            event->accept();
            return;
        }
        if (!exiting_ && backend_->state().closeToTray && trayAvailable())
        {
            hideToTray(windowState());
            event->ignore();
            return;
        }
        exiting_ = true;
        if(sceneModel_)sceneModel_->flush();
        saveLayout();
        if (settings_)
            settings_->hide();
        if(quick_)quick_->hide();
        if (diagnostics_)
            diagnostics_->hide();
        if (media_)
        {
            mediaController_->back();
            media_->hide();
        }
        backend_->shutdown();
#ifdef REDOT_UI_VERIFICATION
        checkReport_.write(QString("EXIT backend shutdown requested alive=%1\n").arg(backend_->alive()).toUtf8());
        checkReport_.flush();
#endif
        event->ignore();
    }
};

int runQtFrontend(HINSTANCE instance)
{
    int argc = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<QByteArray> bytes;
    for (int i = 0; i < argc; ++i)
        bytes.push_back(QString::fromWCharArray(wide[i]).toUtf8());
    LocalFree(wide);
    std::vector<char *> argv;
    for (auto &b : bytes)
        argv.push_back(b.data());
    argv.push_back(nullptr);
    // The native D3D preview must not force every sibling (including the
    // QQuickWidget material) into an HWND. A native QQuickWidget can render its
    // offscreen texture while failing to present it in the real window.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication app(argc, argv.data());
    qInstallMessageHandler([](QtMsgType type,const QMessageLogContext&,const QString& message){logLine(L"Qt (%d): %s",int(type),message.toStdWString().c_str());});
#ifdef REDOT_UI_VERIFICATION
    if (quiet())
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            QFile file(qEnvironmentVariable("PANDACLIP_QT_TEST_ROOT") + "/qt-messages.txt");
            if (file.open(QIODevice::WriteOnly | QIODevice::Append))
            {
                file.write((QString::number(type) + " " + message + "\n").toUtf8());
            }
            if (type == QtFatalMsg)
                abort();
        });
#endif
    UINT32 packageLength{};if(GetCurrentPackageFullName(&packageLength,nullptr)==APPMODEL_ERROR_NO_PACKAGE)SetCurrentProcessExplicitAppUserModelID(L"ReDot.Desktop");
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("ReDot");app.setApplicationDisplayName("ReDot");app.setApplicationVersion(REDOT_VERSION);
    app.setFont(QFont("Segoe UI", 9));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle("Basic");
    QApplication::setStyle("Fusion");
    app.setWindowIcon(Branding::icon());
    app.setStyleSheet(DesignSystem::style());
    int result{};
    {
        Workspace window(instance);
        showOwned(&window);
        result=app.exec();
    } // Destroy the player/renderers before finalizing optional diagnostics.
    if (qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_TRACE")) {
        auto finish=reinterpret_cast<void(*)()>(playbackTraceFunction("pandaclipFlushPlaybackTrace"));
        if(finish)finish();
    }
    return result;
}
