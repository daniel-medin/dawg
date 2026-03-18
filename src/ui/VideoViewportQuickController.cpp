#include "ui/VideoViewportQuickController.h"

#include <algorithm>
#include <cmath>

#include <QRectF>
#include <QVariantMap>

namespace
{
QRectF scaledFrameRectForSize(
    const QSizeF& containerSize,
    const QSize& sourceSize,
    const double displayScaleFactor)
{
    if (sourceSize.isEmpty())
    {
        const auto width = std::max(320.0, containerSize.width() - 64.0);
        const auto height = std::max(180.0, containerSize.height() - 64.0);
        return QRectF{
            (containerSize.width() - width) / 2.0,
            (containerSize.height() - height) / 2.0,
            width,
            height};
    }

    const auto bounded = sourceSize.scaled(containerSize.toSize(), Qt::KeepAspectRatio);
    const auto clampedScale = std::clamp(displayScaleFactor, 0.1, 1.0);
    const auto scaledWidth = std::max(1.0, std::round(static_cast<double>(bounded.width()) * clampedScale));
    const auto scaledHeight = std::max(1.0, std::round(static_cast<double>(bounded.height()) * clampedScale));
    return QRectF{
        (containerSize.width() - scaledWidth) / 2.0,
        (containerSize.height() - scaledHeight) / 2.0,
        scaledWidth,
        scaledHeight};
}

QVariantMap rectToVariantMap(const QRectF& rect)
{
    QVariantMap value;
    value.insert(QStringLiteral("x"), rect.x());
    value.insert(QStringLiteral("y"), rect.y());
    value.insert(QStringLiteral("width"), rect.width());
    value.insert(QStringLiteral("height"), rect.height());
    return value;
}

QVariantMap pointToVariantMap(const QPointF& point)
{
    QVariantMap value;
    value.insert(QStringLiteral("x"), point.x());
    value.insert(QStringLiteral("y"), point.y());
    return value;
}
}

VideoViewportQuickController::VideoViewportQuickController(QObject* parent)
    : QObject(parent)
{
}

void VideoViewportQuickController::setPresentedFrame(
    const QImage& frame,
    const VideoFrame& videoFrame,
    const QSize& sourceSize)
{
    const auto previousSourceSize = effectiveSourceSize();
    m_frame = frame;
    m_videoFrame = videoFrame;
    m_sourceFrameSize = sourceSize;
    bumpFrameRevision();
    updateFrameState(videoFrame.isValid() || !frame.isNull());
    if (effectiveSourceSize() != previousSourceSize)
    {
        emit sourceGeometryChanged();
    }
}

void VideoViewportQuickController::setFrame(const QImage& frame)
{
    const auto previousSourceSize = effectiveSourceSize();
    m_frame = frame;
    bumpFrameRevision();
    updateFrameState(!m_frame.isNull() || m_videoFrame.isValid());
    if (effectiveSourceSize() != previousSourceSize)
    {
        emit sourceGeometryChanged();
    }
}

void VideoViewportQuickController::setVideoFrame(const VideoFrame& videoFrame)
{
    m_videoFrame = videoFrame;
    updateFrameState(videoFrame.isValid() || !m_frame.isNull());
}

void VideoViewportQuickController::setSourceFrameSize(const QSize& sourceSize)
{
    if (m_sourceFrameSize == sourceSize)
    {
        return;
    }

    const auto previousSourceSize = effectiveSourceSize();
    m_sourceFrameSize = sourceSize;
    if (effectiveSourceSize() != previousSourceSize)
    {
        emit sourceGeometryChanged();
    }
}

void VideoViewportQuickController::setOverlays(const std::vector<TrackOverlay>& overlays)
{
    m_overlayData = overlays;

    QVariantList nextModels;
    nextModels.reserve(static_cast<qsizetype>(overlays.size()));
    for (const auto& overlay : overlays)
    {
        QVariantMap item;
        item.insert(QStringLiteral("trackId"), overlay.id.toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("label"), overlay.label);
        item.insert(QStringLiteral("color"), overlay.color);
        item.insert(QStringLiteral("imageX"), overlay.imagePoint.x());
        item.insert(QStringLiteral("imageY"), overlay.imagePoint.y());
        item.insert(QStringLiteral("selected"), overlay.isSelected);
        item.insert(QStringLiteral("highlightOpacity"), overlay.highlightOpacity);
        item.insert(QStringLiteral("showLabel"), overlay.showLabel);
        item.insert(QStringLiteral("hasAttachedAudio"), overlay.hasAttachedAudio);
        item.insert(QStringLiteral("autoPanEnabled"), overlay.autoPanEnabled);
        nextModels.push_back(item);
    }

    m_overlayModels = nextModels;
    emit overlaysChanged();
}

void VideoViewportQuickController::setShowAllLabels(const bool enabled)
{
    if (m_showAllLabels == enabled)
    {
        return;
    }

    m_showAllLabels = enabled;
    emit showAllLabelsChanged();
}

void VideoViewportQuickController::setDisplayScaleFactor(const double scaleFactor)
{
    const auto clampedScale = std::clamp(scaleFactor, 0.1, 1.0);
    if (std::abs(m_displayScaleFactor - clampedScale) < 0.001)
    {
        return;
    }

    m_displayScaleFactor = clampedScale;
    emit displayScaleFactorChanged();
}

bool VideoViewportQuickController::hasFrame() const
{
    return m_hasFrame;
}

QString VideoViewportQuickController::frameSource() const
{
    return m_frameSource;
}

QVariantList VideoViewportQuickController::overlays() const
{
    return m_overlayModels;
}

bool VideoViewportQuickController::showAllLabels() const
{
    return m_showAllLabels;
}

double VideoViewportQuickController::displayScaleFactor() const
{
    return m_displayScaleFactor;
}

int VideoViewportQuickController::sourceWidth() const
{
    return effectiveSourceSize().width();
}

int VideoViewportQuickController::sourceHeight() const
{
    return effectiveSourceSize().height();
}

const QImage& VideoViewportQuickController::currentFrame() const
{
    return m_frame;
}

QVariantMap VideoViewportQuickController::frameRect(const qreal viewWidth, const qreal viewHeight) const
{
    return rectToVariantMap(scaledFrameRect(QSizeF{viewWidth, viewHeight}));
}

QVariantMap VideoViewportQuickController::widgetToImagePoint(
    const qreal widgetX,
    const qreal widgetY,
    const qreal viewWidth,
    const qreal viewHeight) const
{
    const auto frameRectValue = scaledFrameRect(QSizeF{viewWidth, viewHeight});
    const auto sourceSize = effectiveSourceSize();
    const auto xRatio = frameRectValue.width() > 0.0
        ? (widgetX - frameRectValue.left()) / frameRectValue.width()
        : 0.0;
    const auto yRatio = frameRectValue.height() > 0.0
        ? (widgetY - frameRectValue.top()) / frameRectValue.height()
        : 0.0;
    return pointToVariantMap(QPointF{
        std::clamp(xRatio, 0.0, 1.0) * sourceSize.width(),
        std::clamp(yRatio, 0.0, 1.0) * sourceSize.height()});
}

QString VideoViewportQuickController::trackIdAt(
    const qreal widgetX,
    const qreal widgetY,
    const qreal viewWidth,
    const qreal viewHeight) const
{
    if (!m_hasFrame)
    {
        return {};
    }

    const auto frameRectValue = scaledFrameRect(QSizeF{viewWidth, viewHeight});
    const auto sourceSize = effectiveSourceSize();
    const auto scaleX = frameRectValue.width() / static_cast<double>(std::max(1, sourceSize.width()));
    const auto scaleY = frameRectValue.height() / static_cast<double>(std::max(1, sourceSize.height()));
    constexpr double hitRadius = 14.0;
    constexpr double hitRadiusSquared = hitRadius * hitRadius;
    const QPointF widgetPoint{widgetX, widgetY};

    for (auto it = m_overlayData.rbegin(); it != m_overlayData.rend(); ++it)
    {
        const QPointF canvasPoint{
            frameRectValue.left() + it->imagePoint.x() * scaleX,
            frameRectValue.top() + it->imagePoint.y() * scaleY};
        const auto delta = widgetPoint - canvasPoint;
        const auto distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
        if (distanceSquared <= hitRadiusSquared)
        {
            return it->id.toString(QUuid::WithoutBraces);
        }
    }

    return {};
}

QVariantList VideoViewportQuickController::tracksInRect(
    const qreal rectX,
    const qreal rectY,
    const qreal rectWidth,
    const qreal rectHeight,
    const qreal viewWidth,
    const qreal viewHeight) const
{
    QVariantList trackIds;
    if (!m_hasFrame || rectWidth <= 0.0 || rectHeight <= 0.0)
    {
        return trackIds;
    }

    const auto frameRectValue = scaledFrameRect(QSizeF{viewWidth, viewHeight});
    const auto sourceSize = effectiveSourceSize();
    const auto scaleX = frameRectValue.width() / static_cast<double>(std::max(1, sourceSize.width()));
    const auto scaleY = frameRectValue.height() / static_cast<double>(std::max(1, sourceSize.height()));
    const QRectF normalizedRect = QRectF{rectX, rectY, rectWidth, rectHeight}.normalized();

    for (const auto& overlay : m_overlayData)
    {
        const QPointF canvasPoint{
            frameRectValue.left() + overlay.imagePoint.x() * scaleX,
            frameRectValue.top() + overlay.imagePoint.y() * scaleY};
        if (normalizedRect.contains(canvasPoint))
        {
            trackIds.push_back(overlay.id.toString(QUuid::WithoutBraces));
        }
    }

    return trackIds;
}

bool VideoViewportQuickController::overlayHasAttachedAudio(const QString& trackId) const
{
    const auto* overlay = overlayById(trackId);
    return overlay && overlay->hasAttachedAudio;
}

bool VideoViewportQuickController::overlayIsSelected(const QString& trackId) const
{
    const auto* overlay = overlayById(trackId);
    return overlay && overlay->isSelected;
}

QSize VideoViewportQuickController::effectiveSourceSize() const
{
    return !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_frame.size();
}

QRectF VideoViewportQuickController::scaledFrameRect(const QSizeF& viewSize) const
{
    return scaledFrameRectForSize(viewSize, effectiveSourceSize(), m_displayScaleFactor);
}

void VideoViewportQuickController::updateFrameState(const bool hasFrame)
{
    if (m_hasFrame == hasFrame)
    {
        return;
    }

    m_hasFrame = hasFrame;
    emit hasFrameChanged();
}

void VideoViewportQuickController::bumpFrameRevision()
{
    ++m_frameRevision;
    m_frameSource = QStringLiteral("image://videoViewport/frame/%1").arg(m_frameRevision);
    emit frameSourceChanged();
}

const TrackOverlay* VideoViewportQuickController::overlayById(const QString& trackId) const
{
    const auto uuid = QUuid(trackId);
    if (uuid.isNull())
    {
        return nullptr;
    }

    const auto it = std::find_if(
        m_overlayData.cbegin(),
        m_overlayData.cend(),
        [&uuid](const TrackOverlay& overlay)
        {
            return overlay.id == uuid;
        });
    return it != m_overlayData.cend() ? &(*it) : nullptr;
}
