#pragma once
#include <windows.h>
#include "scene_model.h"
#include <QWidget>
#include <QImage>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QListWidget>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSplitter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QRegion>
#include <functional>

namespace Scenes {
class Canvas final : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(Model* model,QWidget* parent=nullptr);
    QRectF viewport() const;
    QPointF toCanvas(QPointF point) const;
    QPointF toWidget(QPointF point) const;
    QPolygonF corners(const Visual& value) const;
    QString hit(QPointF point) const;
    QPointF rotationHandle(const Visual& value) const;
    bool cropMode() const { return cropMode_; }
    void setCropMode(bool enabled);
    void transformAction(const QString& action);
    void bindPresentation(std::function<bool(HWND,const QRectF&,const QVector<QPolygonF>&,const QVector<QPointF>&)> render);
Q_SIGNALS:
    void advancedTransformRequested();
    void cropModeChanged(bool enabled);
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    void refreshImages();
    void nextImage();
    void updatePresentationMask(const QRectF& viewport,const QVector<QPolygonF>& lines,const QVector<QPointF>& handles);
    QRectF maskedViewport_;
    qreal maskedScale_{};
    QVector<QPolygonF> maskedLines_;
    QVector<QPointF> maskedHandles_;
    Model* model_;
    QString dragging_;
    Visual start_;
    QPointF press_,anchor_;
    int corner_{-1};
    bool rotating_{},cropping_{},cropMode_{};
    double guideX_{-1},guideY_{-1};
    QHash<QString,QImage> images_;
    QHash<QString,QString> imageErrors_;
    QStringList pendingImages_;
    QSet<QString> requestedImages_;
    QFutureWatcher<QImage> imageWorker_;
    QString loadingImage_;
    std::function<bool(HWND,const QRectF&,const QVector<QPolygonF>&,const QVector<QPointF>&)> render_;
};
class Page final : public QWidget {
    Q_OBJECT
public:
    explicit Page(Model* model,QWidget* parent=nullptr);
    Model* model() const { return model_; }
    Canvas* canvas() const { return canvas_; }
    QListWidget* sceneList() const { return scenes_; }
    QListWidget* sourceList() const { return sources_; }
    void refresh();
    void runtimeStatus(const QHash<QString,QString>& sources);
private:
    void selection();
    void audioRows();
    void addMenu(bool audio,QWidget* anchor);
    void configure(Kind kind,const QString& existing={});
    void sourceProperties();
    void advancedTransform();
    Model* model_;
    Canvas* canvas_;
    QListWidget *scenes_,*sources_;
    QLabel *heading_,*saveState_,*selectionState_;
    QComboBox* resolution_;
    QVBoxLayout* audioLayout_;
    QWidget* audioBody_;
    QCheckBox *visible_,*locked_;
    bool updating_{};
};
}
