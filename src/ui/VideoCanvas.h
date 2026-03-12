#pragma once

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QRectF imageRenderRect() const;
    [[nodiscard]] QPointF widgetToImagePoint(const QPointF& widgetPoint) const;

    QImage m_frame;
    std::vector<TrackOverlay> m_overlays;
};

