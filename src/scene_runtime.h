#pragma once
#include "scene_model.h"
#include "recording.h"
#include "borderless_access.h"
#include <QHash>
#include <QPolygonF>
#include <memory>

namespace Scenes {
struct Device { QString id, name; QSize size; };
QVector<Device> displays();
QVector<Device> cameras(QString& error);
struct RuntimeStatus {
    QHash<QString,QString> sources;
    uint64_t frames{}, poolDrops{}, sourceFrames{}, sourceDrops{},lateSlots{};
    double composeMeanMs{}, composeMaxMs{};
    double gpuMeanMs{},gpuMaxMs{};
    uint64_t gpuSamples{},gpuBytes{};
#ifdef REDOT_TEST_FIXTURE
    std::array<uint64_t,401> verificationComposeHistogram{};
#endif
    QString error;
};
// One output stream, with leases shared by both presentation and the encoder.
// Immutable snapshots cross the UI/worker boundary; no UI objects cross it.
class Runtime final {
public:
    Runtime(std::shared_ptr<GpuResources> gpu, HWND notify, UINT message);
    ~Runtime();
    void configure(Snapshot snapshot, VideoConfig output);
    void attach(std::shared_ptr<RecordingIngress> ingress);
    void borderPermission(BorderPermission permission);
    void suspend(bool suspended);
    void cursorEnabled(bool enabled);
    bool active() const;
    void requestStop();
    bool busy() const;
    RecordedFrame latest() const;
    RuntimeStatus status() const;
    Snapshot snapshot() const;
#ifdef REDOT_UI_VERIFICATION
    void verificationFrame(const QString& source,RecordedFrame frame);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Per-view swapchain; composition is never repeated here. Editor outlines are
// drawn only onto this swapchain, after the canonical image.
class Presentation final {
public:
    Presentation();
    ~Presentation();
    bool render(HWND target, const std::shared_ptr<GpuResources>& gpu,
        const RecordedFrame& frame, const QRectF& physicalViewport,
        const QVector<QPolygonF>& editorLines={}, const QVector<QPointF>& handles={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
