#pragma once

#include <QUuid>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/tracking/TrackTypes.h"

class VideoCanvas final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCanvas(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void setOverlays(const std::vector<TrackOverlay>& overlays);

signals:
    void seedPointRequested(const QPointF& imagePoint);
    void trackSelected(const QUuid& trackId);
    void selectedTrackMoved(const QPointF& imagePoint);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QUuid trackAt(const QPointF& widgetPoint) const;
    [[nodiscard]] QRectF imageRenderRect() const;
    [[nodiscard]] QPointF widgetToImagePoint(const QPointF& widgetPoint) const;

    QImage m_frame;
    std::vector<TrackOverlay> m_overlays;
    QUuid m_draggedTrackId;
};
