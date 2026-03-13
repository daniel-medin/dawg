#pragma once

#include <QList>
#include <QUuid>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/tracking/TrackTypes.h"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPaintEvent;

class VideoCanvas final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCanvas(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void setOverlays(const std::vector<TrackOverlay>& overlays);
    void setShowAllLabels(bool enabled);

signals:
    void seedPointRequested(const QPointF& imagePoint);
    void audioDropped(const QString& assetPath, const QPointF& imagePoint);
    void trackSelected(const QUuid& trackId);
    void tracksSelected(const QList<QUuid>& trackIds);
    void selectedTrackMoved(const QPointF& imagePoint);
    void trackContextMenuRequested(const QUuid& trackId, const QPoint& globalPosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] static QString audioPathFromMimeData(const QMimeData* mimeData);
    [[nodiscard]] QUuid trackAt(const QPointF& widgetPoint) const;
    [[nodiscard]] QList<QUuid> tracksInRect(const QRectF& widgetRect) const;
    [[nodiscard]] QRectF imageRenderRect() const;
    [[nodiscard]] QPointF widgetToImagePoint(const QPointF& widgetPoint) const;
    void paintWelcomeState(QPainter& painter);

    QImage m_frame;
    QImage m_emptyStateLogo;
    std::vector<TrackOverlay> m_overlays;
    QUuid m_draggedTrackId;
    QPointF m_pressPosition;
    QRectF m_selectionRect;
    bool m_showAllLabels = false;
    bool m_pendingSeed = false;
    bool m_isMarqueeSelecting = false;
};
