#pragma once
#include "clip_library.h"
#include "audio_meter.h"
#include <functional>
#include <memory>
#include "scene_runtime.h"

enum class FrontendControlKind
{
    Text,
    Edit,
    Choice,
    Toggle,
    Slider,
    Meter,
    Action,
    Shortcut
};
struct FrontendControl
{
    int id{}, category{}, value{}, selection{-1};
    bool enabled{}, checked{}, editable{};
    FrontendControlKind kind{FrontendControlKind::Text};
    std::wstring label, text;
    std::vector<std::wstring> choices;
};
struct FrontendState
{
    bool recording{}, encoding{}, replay{}, capture{}, closing{}, recovery{}, closeToTray{}, minimizeToTray{}, transparency{}, motion{},
        previewEnabled{};
    unsigned taskbarMode{};
    unsigned gameLevel{}, micLevel{}, pendingSaves{};
    AudioMeterSnapshot gameMeter, micMeter;
    bool pendingStart{}, gameOpen{}, micOpen{}, gameEnabled{}, micEnabled{}, encoderReady{}, previewReady{};
    std::wstring borderStatus, openedGame;
    double historySeconds{};
    uint64_t replayBytes{}, replayCap{}, libraryRevision{};
    std::wstring captureStatus, recordStatus, replayStatus, elapsed, gameStatus, micStatus, settingsStatus, folder,
        selectedSource, videoDescription, diagnostics, openedMic;
};
// The frontend owns presentation. The adapter owns the existing capture lifecycle,
// endpoint IDs, settings validation, registered hotkeys and compressed packet paths.
class FrontendBackend
{
  public:
    virtual ~FrontendBackend() = default;
    virtual void scene(const Scenes::Snapshot& value) = 0;
    virtual Scenes::RuntimeStatus sceneStatus() = 0;
    virtual bool scenePreview(HWND window, const QRectF& viewport, const QVector<QPolygonF>& lines, const QVector<QPointF>& handles) = 0;
    virtual QJsonObject legacySceneSettings() = 0;
    virtual void onSceneAudioEdit(std::function<void(int,int,bool)> callback) = 0;
    virtual FrontendState state(bool diagnostics = false) = 0;
    virtual std::vector<FrontendControl> settings() = 0;
    virtual FrontendControl control(int id) = 0;
    virtual void edit(int id, const std::wstring &text, int value, bool checked) = 0;
    virtual void action(int id) = 0;
    virtual void setFolder(const std::wstring &path) = 0;
    virtual void validationError(const std::wstring &message) = 0;
    virtual void previewResize() = 0;
    virtual void previewVisible(bool visible) = 0;
    virtual void requestLibrary() = 0;
    virtual const std::vector<ClipInfo> &clips() = 0;
    virtual void poll() = 0;
    virtual bool alive() const = 0;
    virtual void shutdown() = 0;
    virtual void onVisibilityShortcut(std::function<void()> fn) = 0;
    // Shortcut problems (registration failed, nothing to save) must reach the user even when ReDot is in the tray and a game has focus.
    virtual void onShortcutNotice(std::function<void(const std::wstring &)> fn) = 0;
    virtual void generatedSource(unsigned mode) = 0;
    virtual bool previewReadback(std::vector<uint32_t> &pixels, unsigned &width, unsigned &height) = 0;
    virtual std::wstring quietHotkeyProbe() = 0;
#ifdef REDOT_UI_VERIFICATION
    virtual bool verificationSettingsSettled() = 0;
    virtual uint64_t verificationWgcFrames() = 0;
    virtual uint64_t verificationEncodedFrames() = 0;
    virtual void verificationHotkey(unsigned action) = 0;
    virtual uint64_t verificationStartupSnapshots() = 0;
    virtual void verificationInterrupt(bool fail) = 0;
    virtual void verificationRecoveryFailure(bool fail) = 0;
    virtual unsigned verificationRecoveryAttempts() = 0;
    virtual unsigned verificationLiveMutes() = 0;
#endif
};
std::unique_ptr<FrontendBackend> makeFrontendBackend(HINSTANCE instance, HWND preview);
int runQtFrontend(HINSTANCE instance);
