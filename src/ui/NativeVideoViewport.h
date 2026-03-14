#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/render/RenderService.h"
#include "core/tracking/TrackTypes.h"
#include "core/video/VideoFrame.h"

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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QRectF imageRenderRect() const;
    [[nodiscard]] QImage buildOverlayImage(const QRectF& frameRect) const;
    void paintOverlayContent(QPainter& painter, const QRectF& frameRect) const;
    void updateNativeTargetGeometry(const QRectF& frameRect);
    [[nodiscard]] bool tryNativePresent(const QRectF& frameRect, const QImage& overlayImage);

    QImage m_frame;
    VideoFrame m_videoFrame;
    QSize m_sourceFrameSize;
    std::vector<TrackOverlay> m_overlays;
    RenderService* m_renderService = nullptr;
    std::unique_ptr<RenderService> m_ownedRenderService;
    QWidget* m_nativeTarget = nullptr;
    QRect m_nativeTargetRect;
    bool m_showAllLabels = false;
};
