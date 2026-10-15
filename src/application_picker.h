#pragma once
#include <QWidget>
#include <QImage>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <optional>

namespace Scenes {
struct ApplicationWindow {
    quintptr handle{};
    quint32 processId{};
    quint64 processStart{};
    QString title, executable, windowClass;
    QSizeF clientSize;
    QImage icon;
    QJsonObject identity() const;
};
struct ApplicationDiscovery {
    QVector<ApplicationWindow> windows;
    QString error;
    int inaccessible{};
};
// Desktop is captured on the caller's UI thread, so owned-desktop tests discover
// their own desktop too. No activation, capture, or process mutation occurs.
// withIcons=false skips the per-window WM_GETICON round trip (up to 15 ms each, blocking) and icon conversion; the scene compositor
// only needs executable/title/class and must never stall its frame-pacing thread on other applications.
ApplicationDiscovery discoverApplications(quintptr desktop, quint32 excludedProcess, bool withIcons = true);
bool applicationStillAvailable(const ApplicationWindow& window);
class ApplicationPicker final : public QWidget {
    Q_OBJECT
public:
    explicit ApplicationPicker(const QJsonObject& saved={}, QWidget* parent=nullptr);
    void refresh();
    std::optional<ApplicationWindow> selected() const;
    const ApplicationDiscovery& result() const { return result_; }
    bool busy() const { return worker_.isRunning(); }
Q_SIGNALS:
    void selectionChanged();
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    void filter();
    QJsonObject saved_;
    quintptr selectedHandle_{};
    QLineEdit* search_;
    QListWidget* list_;
    QLabel* status_;
    QTimer timer_;
    QFutureWatcher<ApplicationDiscovery> worker_;
    ApplicationDiscovery result_;
    quintptr desktop_{};
    bool filtering_{};
};
}
