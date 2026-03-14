#pragma once

#include <QList>
#include <QUuid>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/render/RenderService.h"
#include "core/tracking/TrackTypes.h"
#include "core/video/VideoFrame.h"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QResizeEvent;

class VideoCanvas final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCanvas(QWidget* parent = nullptr);

    void setPresentedFrame(const QImage& frame, const VideoFrame& videoFrame, const QSize& sourceSize);
    void setFrame(const QImage& frame);
    void setVideoFrame(const VideoFrame& frame);
    void setRenderService(RenderService* renderService);
    void setSourceFrameSize(const QSize& sourceSize);
    void setNativePresentationEnabled(bool enabled);
    void setDisplayScaleFactor(double scaleFactor);
    void setOverlays(const std::vector<TrackOverlay>& overlays);
    void setShowAllLabels(bool enabled);

signals:
    void seedPointRequested(const QPointF& imagePoint);
    void audioDropped(const QString& assetPath, const QPointF& imagePoint);
    void trackSelected(const QUuid& trackId);
    void trackActivated(const QUuid& trackId);
    void tracksSelected(const QList<QUuid>& trackIds);
    void selectedTrackMoved(const QPointF& imagePoint);
    void trackContextMenuRequested(const QUuid& trackId, const QPoint& globalPosition);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* m_surfaceLayer = nullptr;
    QWidget* m_overlayLayer = nullptr;
};
