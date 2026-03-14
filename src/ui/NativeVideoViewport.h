#pragma once

#include <memory>
#include <QString>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/tracking/TrackTypes.h"
#include "core/video/VideoFrame.h"

class QResizeEvent;
class RenderService;
class QPainter;
class QPaintEvent;
class QPaintEngine;

class NativeVideoViewport final : public QWidget
{
    Q_OBJECT

public:
    explicit NativeVideoViewport(QWidget* parent = nullptr);

    void setPresentedFrame(const QImage& frame, const VideoFrame& videoFrame, const QSize& sourceSize);
    void setRenderService(RenderService* renderService);
    void setOverlays(const std::vector<TrackOverlay>& overlays);
    void setShowAllLabels(bool enabled);

protected:
    [[nodiscard]] QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QRectF imageRenderRect() const;
    [[nodiscard]] QImage buildOverlayImage(const QRectF& frameRect) const;
    void paintOverlayContent(QPainter& painter, const QRectF& frameRect) const;
    [[nodiscard]] bool tryNativePresent(const QRectF& frameRect, const QImage& overlayImage);
    void logPresentationState(const QString& category, const QString& message, bool force = false);

    QImage m_frame;
    VideoFrame m_videoFrame;
    QSize m_sourceFrameSize;
    std::vector<TrackOverlay> m_overlays;
    RenderService* m_renderService = nullptr;
    std::unique_ptr<RenderService> m_ownedRenderService;
    bool m_showAllLabels = false;
    QString m_lastPresentationState;
};
