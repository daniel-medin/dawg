#include "ui/VideoCanvas.h"

#include <algorithm>
#include <cmath>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace
{
QString audioPathFromMimeData(const QMimeData* mimeData)
{
    if (!mimeData)
    {
        return {};
    }

    if (mimeData->hasFormat("application/x-dawg-audio-path"))
    {
        return QString::fromUtf8(mimeData->data("application/x-dawg-audio-path"));
    }

    if (mimeData->hasUrls())
    {
        const auto urls = mimeData->urls();
        if (!urls.isEmpty() && urls.front().isLocalFile())
        {
            return urls.front().toLocalFile();
        }
    }

    if (mimeData->hasText())
    {
        return mimeData->text().trimmed();
    }

    return {};
}

class VideoSurfaceLayer final : public QWidget
{
public:
    explicit VideoSurfaceLayer(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_NativeWindow);
    }

    void setFrame(const QImage& frame)
    {
        m_frame = frame;
        scheduleNativeProbe();
        update();
    }

    void setVideoFrame(const VideoFrame& frame)
    {
        m_videoFrame = frame;
        scheduleNativeProbe();
        update();
    }

    void setSourceFrameSize(const QSize& sourceSize)
    {
        if (m_sourceFrameSize == sourceSize)
        {
            return;
        }

        m_sourceFrameSize = sourceSize;
        scheduleNativeProbe();
        update();
    }

    void setRenderService(RenderService* renderService)
    {
        m_renderService = renderService;
        scheduleNativeProbe();
        update();
    }

    [[nodiscard]] QRectF imageRenderRect() const
    {
        if (m_frame.isNull())
        {
            const auto width = std::max(320, this->width() - 64);
            const auto height = std::max(180, this->height() - 64);
            return QRectF{
                (this->width() - width) / 2.0,
                (this->height() - height) / 2.0,
                static_cast<double>(width),
                static_cast<double>(height)
            };
        }

        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_frame.size();
        const auto bounded = size();
        const auto scaled = sourceSize.scaled(bounded, Qt::KeepAspectRatio);
        return QRectF{
            (width() - scaled.width()) / 2.0,
            (height() - scaled.height()) / 2.0,
            static_cast<double>(scaled.width()),
            static_cast<double>(scaled.height())
        };
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto frameRect = imageRenderRect();
        runNativeProbe(frameRect);
        QPainter painter(this);
        painter.fillRect(rect(), QColor{12, 14, 18});
        painter.fillRect(frameRect, QColor{24, 27, 32});
        if (!m_frame.isNull())
        {
            painter.drawImage(frameRect, m_frame);
        }
    }

private:
    void scheduleNativeProbe()
    {
        m_nativeProbePending = true;
    }

    void runNativeProbe(const QRectF& frameRect)
    {
        if (!m_nativeProbePending || !m_renderService || !m_renderService->canPresentToNativeWindow())
        {
            return;
        }

        m_nativeProbePending = false;
        if (!m_videoFrame.isValid())
        {
            return;
        }

        m_renderService->presentToNativeWindow(this, size(), m_videoFrame, frameRect, QImage{});
    }

    QImage m_frame;
    QSize m_sourceFrameSize;
    VideoFrame m_videoFrame;
    RenderService* m_renderService = nullptr;
    bool m_nativeProbePending = true;
};

class VideoOverlayLayer final : public QWidget
{
public:
    explicit VideoOverlayLayer(VideoCanvas* owner)
        : QWidget(owner)
        , m_owner(owner)
        , m_emptyStateLogo(QStringLiteral(":/branding/logo.png"))
    {
        setMouseTracking(true);
        setAcceptDrops(true);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

    void setFrame(const QImage& frame)
    {
        m_presentedFrameSize = frame.size();
        m_hasFrame = !frame.isNull();
        update();
    }

    void setSourceFrameSize(const QSize& sourceSize)
    {
        if (m_sourceFrameSize == sourceSize)
        {
            return;
        }

        m_sourceFrameSize = sourceSize;
        update();
    }

    void setOverlays(const std::vector<TrackOverlay>& overlays)
    {
        m_overlays = overlays;
        update();
    }

    void setShowAllLabels(const bool enabled)
    {
        if (m_showAllLabels == enabled)
        {
            return;
        }

        m_showAllLabels = enabled;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const auto frameRect = imageRenderRect();
        if (!m_hasFrame)
        {
            painter.fillRect(rect(), QColor{12, 14, 18});
            painter.fillRect(frameRect, QColor{24, 27, 32});
            paintWelcomeState(painter, frameRect);
            return;
        }

        paintOverlayContent(painter, frameRect);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (m_hasFrame && !audioPathFromMimeData(event->mimeData()).isEmpty())
        {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
            return;
        }

        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (m_hasFrame && !audioPathFromMimeData(event->mimeData()).isEmpty())
        {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
            return;
        }

        event->ignore();
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!m_hasFrame)
        {
            event->ignore();
            return;
        }

        const auto assetPath = audioPathFromMimeData(event->mimeData());
        if (assetPath.isEmpty())
        {
            event->ignore();
            return;
        }

        m_owner->audioDropped(assetPath, widgetToImagePoint(event->position()));
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QWidget::mousePressEvent(event);

        if (!m_hasFrame)
        {
            return;
        }

        const auto frameRect = imageRenderRect();
        if (!frameRect.contains(event->position()))
        {
            return;
        }

        if (event->button() == Qt::RightButton)
        {
            const auto trackId = trackAt(event->position());
            if (!trackId.isNull())
            {
                m_owner->trackContextMenuRequested(trackId, event->globalPosition().toPoint());
            }
            return;
        }

        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        const auto trackId = trackAt(event->position());
        if (!trackId.isNull())
        {
            m_draggedTrackId = trackId;
            m_owner->trackSelected(trackId);
            return;
        }

        m_pressPosition = event->position();
        m_selectionRect = QRectF{m_pressPosition, m_pressPosition};
        m_pendingSeed = true;
        m_isMarqueeSelecting = false;
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QWidget::mouseMoveEvent(event);

        if (m_draggedTrackId.isNull() || !m_hasFrame)
        {
            if (!m_pendingSeed)
            {
                return;
            }

            constexpr qreal selectionThreshold = 6.0;
            if (!m_isMarqueeSelecting
                && QLineF{m_pressPosition, event->position()}.length() >= selectionThreshold)
            {
                m_isMarqueeSelecting = true;
            }

            if (m_isMarqueeSelecting)
            {
                m_selectionRect = QRectF{m_pressPosition, event->position()}.normalized();
                update();
            }
            return;
        }

        m_owner->selectedTrackMoved(widgetToImagePoint(event->position()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QWidget::mouseReleaseEvent(event);

        if (event->button() == Qt::LeftButton)
        {
            if (!m_draggedTrackId.isNull())
            {
                m_draggedTrackId = {};
                return;
            }

            if (m_isMarqueeSelecting)
            {
                m_owner->tracksSelected(tracksInRect(m_selectionRect.normalized()));
                m_isMarqueeSelecting = false;
                m_pendingSeed = false;
                m_selectionRect = {};
                update();
                return;
            }

            if (m_pendingSeed)
            {
                m_pendingSeed = false;
                m_owner->seedPointRequested(widgetToImagePoint(event->position()));
            }

            m_draggedTrackId = {};
        }
    }

private:
    [[nodiscard]] QRectF imageRenderRect() const
    {
        if (!m_hasFrame)
        {
            const auto width = std::max(320, this->width() - 64);
            const auto height = std::max(180, this->height() - 64);
            return QRectF{
                (this->width() - width) / 2.0,
                (this->height() - height) / 2.0,
                static_cast<double>(width),
                static_cast<double>(height)
            };
        }

        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_presentedFrameSize;
        const auto bounded = size();
        const auto scaled = sourceSize.scaled(bounded, Qt::KeepAspectRatio);
        return QRectF{
            (width() - scaled.width()) / 2.0,
            (height() - scaled.height()) / 2.0,
            static_cast<double>(scaled.width()),
            static_cast<double>(scaled.height())
        };
    }

    [[nodiscard]] QPointF widgetToImagePoint(const QPointF& widgetPoint) const
    {
        const auto frameRect = imageRenderRect();
        const auto xRatio = (widgetPoint.x() - frameRect.left()) / frameRect.width();
        const auto yRatio = (widgetPoint.y() - frameRect.top()) / frameRect.height();
        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_presentedFrameSize;

        return QPointF{
            std::clamp(xRatio, 0.0, 1.0) * sourceSize.width(),
            std::clamp(yRatio, 0.0, 1.0) * sourceSize.height()
        };
    }

    [[nodiscard]] QUuid trackAt(const QPointF& widgetPoint) const
    {
        if (!m_hasFrame)
        {
            return {};
        }

        const auto frameRect = imageRenderRect();
        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_presentedFrameSize;
        const auto scaleX = frameRect.width() / static_cast<double>(std::max(1, sourceSize.width()));
        const auto scaleY = frameRect.height() / static_cast<double>(std::max(1, sourceSize.height()));
        constexpr double hitRadius = 14.0;
        constexpr double hitRadiusSquared = hitRadius * hitRadius;

        for (auto it = m_overlays.rbegin(); it != m_overlays.rend(); ++it)
        {
            const QPointF canvasPoint{
                frameRect.left() + it->imagePoint.x() * scaleX,
                frameRect.top() + it->imagePoint.y() * scaleY
            };
            const auto delta = widgetPoint - canvasPoint;
            const auto distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
            if (distanceSquared <= hitRadiusSquared)
            {
                return it->id;
            }
        }

        return {};
    }

    [[nodiscard]] QList<QUuid> tracksInRect(const QRectF& widgetRect) const
    {
        QList<QUuid> trackIds;
        if (!m_hasFrame || widgetRect.isEmpty())
        {
            return trackIds;
        }

        const auto frameRect = imageRenderRect();
        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_presentedFrameSize;
        const auto scaleX = frameRect.width() / static_cast<double>(std::max(1, sourceSize.width()));
        const auto scaleY = frameRect.height() / static_cast<double>(std::max(1, sourceSize.height()));
        const auto normalizedRect = widgetRect.normalized();

        for (const auto& overlay : m_overlays)
        {
            const QPointF canvasPoint{
                frameRect.left() + overlay.imagePoint.x() * scaleX,
                frameRect.top() + overlay.imagePoint.y() * scaleY
            };

            if (normalizedRect.contains(canvasPoint))
            {
                trackIds.push_back(overlay.id);
            }
        }

        return trackIds;
    }

    void paintOverlayContent(QPainter& painter, const QRectF& frameRect) const
    {
        const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_presentedFrameSize;
        const auto scaleX = frameRect.width() / static_cast<double>(std::max(1, sourceSize.width()));
        const auto scaleY = frameRect.height() / static_cast<double>(std::max(1, sourceSize.height()));

        painter.setPen(QPen(QColor{255, 255, 255, 30}, 1.0));
        painter.drawRect(frameRect);
        painter.setRenderHint(QPainter::Antialiasing);

        for (const auto& overlay : m_overlays)
        {
            const QPointF canvasPoint{
                frameRect.left() + overlay.imagePoint.x() * scaleX,
                frameRect.top() + overlay.imagePoint.y() * scaleY
            };

            painter.setBrush(overlay.color);
            painter.setPen(QPen(Qt::black, 2.0));
            painter.drawEllipse(canvasPoint, 7.0, 7.0);

            if (overlay.autoPanEnabled)
            {
                painter.setBrush(QColor{8, 9, 12});
                painter.setPen(QPen(QColor{255, 255, 255, 180}, 1.0));
                painter.drawEllipse(canvasPoint, 2.5, 2.5);
            }

            if (overlay.highlightOpacity > 0.0F)
            {
                painter.setBrush(Qt::NoBrush);
                auto ringColor = overlay.color.lighter(145);
                ringColor.setAlphaF(std::clamp(overlay.highlightOpacity, 0.0F, 1.0F));
                painter.setPen(QPen(ringColor, 2.0));
                painter.drawEllipse(canvasPoint, 12.0, 12.0);
            }

            if (m_showAllLabels || overlay.showLabel)
            {
                auto label = overlay.label;
                if (overlay.hasAttachedAudio)
                {
                    label += QStringLiteral(" [snd]");
                }

                auto labelFont = painter.font();
                labelFont.setPointSizeF(9.0);
                painter.setFont(labelFont);
                const QFontMetricsF metrics{painter.font()};
                const auto textWidth = metrics.horizontalAdvance(label);
                const auto labelWidth = std::max(34.0, textWidth + 16.0);
                const auto labelHeight = std::max(22.0, metrics.height() + 6.0);
                const auto labelRect = QRectF{
                    canvasPoint.x() + 10.0,
                    canvasPoint.y() - (labelHeight * 0.5 + 7.0),
                    labelWidth,
                    labelHeight
                };
                QPainterPath path;
                path.addRoundedRect(labelRect, 6.0, 6.0);
                painter.fillPath(
                    path,
                    overlay.isSelected ? QColor{34, 42, 57, 235} : QColor{16, 18, 24, 220});
                painter.setPen(Qt::white);
                painter.drawText(labelRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            }
        }

        if (m_isMarqueeSelecting)
        {
            const auto marqueeRect = m_selectionRect.normalized().intersected(frameRect);
            painter.setBrush(QColor{120, 170, 220, 38});
            painter.setPen(QPen(QColor{150, 198, 255, 180}, 1.0, Qt::DashLine));
            painter.drawRect(marqueeRect);
        }
    }

    void paintWelcomeState(QPainter& painter, const QRectF& frameRect) const
    {
        QPainterPath panelPath;
        panelPath.addRoundedRect(frameRect.adjusted(20.0, 20.0, -20.0, -20.0), 22.0, 22.0);
        painter.fillPath(panelPath, QColor{16, 18, 24, 232});
        painter.setPen(QPen(QColor{70, 86, 108, 180}, 1.0));
        painter.drawPath(panelPath);

        const auto logoMaxWidth = std::min(frameRect.width() * 0.62, 680.0);
        const auto logoMaxHeight = std::min(frameRect.height() * 0.36, 240.0);
        const auto logoSize = m_emptyStateLogo.isNull()
            ? QSize{}
            : m_emptyStateLogo.size().scaled(
                  static_cast<int>(logoMaxWidth),
                  static_cast<int>(logoMaxHeight),
                  Qt::KeepAspectRatio);

        const auto contentWidth = std::max<double>(420.0, logoSize.width());
        const auto topY = frameRect.center().y() - 130.0;
        const QRectF contentRect{
            frameRect.center().x() - contentWidth * 0.5,
            topY,
            contentWidth,
            260.0
        };

        if (!m_emptyStateLogo.isNull())
        {
            const QRectF logoRect{
                contentRect.center().x() - logoSize.width() * 0.5,
                contentRect.top(),
                static_cast<double>(logoSize.width()),
                static_cast<double>(logoSize.height())
            };
            painter.drawPixmap(
                logoRect.toAlignedRect(),
                QPixmap::fromImage(m_emptyStateLogo.scaled(
                    logoRect.size().toSize(),
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation)));
        }

        auto headlineFont = painter.font();
        headlineFont.setPointSizeF(16.0);
        headlineFont.setBold(true);
        painter.setFont(headlineFont);
        painter.setPen(QColor{232, 238, 244});
        const QRectF headlineRect{
            contentRect.left(),
            contentRect.top() + logoSize.height() + 18.0,
            contentRect.width(),
            28.0
        };
        painter.drawText(headlineRect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Video-first sound staging"));

        auto bodyFont = painter.font();
        bodyFont.setPointSizeF(11.0);
        bodyFont.setBold(false);
        painter.setFont(bodyFont);
        painter.setPen(QColor{182, 191, 203});
        const QRectF bodyRect{
            contentRect.left(),
            headlineRect.bottom() + 8.0,
            contentRect.width(),
            44.0
        };
        painter.drawText(
            bodyRect,
            Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            QStringLiteral("Open a clip to start placing nodes, attach sound, and stage movement directly on the film."));

        auto promptFont = painter.font();
        promptFont.setPointSizeF(10.0);
        painter.setFont(promptFont);
        painter.setPen(QColor{92, 175, 255});
        const QRectF promptRect{
            contentRect.left(),
            bodyRect.bottom() + 18.0,
            contentRect.width(),
            20.0
        };
        painter.drawText(promptRect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Open Video"));
    }

    VideoCanvas* m_owner = nullptr;
    QImage m_emptyStateLogo;
    QSize m_presentedFrameSize;
    QSize m_sourceFrameSize;
    std::vector<TrackOverlay> m_overlays;
    QUuid m_draggedTrackId;
    QPointF m_pressPosition;
    QRectF m_selectionRect;
    bool m_showAllLabels = false;
    bool m_hasFrame = false;
    bool m_pendingSeed = false;
    bool m_isMarqueeSelecting = false;
};
}

VideoCanvas::VideoCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(960, 540);
    m_surfaceLayer = new VideoSurfaceLayer(this);
    m_overlayLayer = new VideoOverlayLayer(this);
    m_surfaceLayer->setGeometry(rect());
    m_overlayLayer->setGeometry(rect());
    m_overlayLayer->raise();
}

void VideoCanvas::setPresentedFrame(const QImage& frame, const VideoFrame& videoFrame, const QSize& sourceSize)
{
    auto* surfaceLayer = static_cast<VideoSurfaceLayer*>(m_surfaceLayer);
    auto* overlayLayer = static_cast<VideoOverlayLayer*>(m_overlayLayer);
    surfaceLayer->setFrame(frame);
    surfaceLayer->setVideoFrame(videoFrame);
    surfaceLayer->setSourceFrameSize(sourceSize);
    overlayLayer->setFrame(frame);
    overlayLayer->setSourceFrameSize(sourceSize);
}

void VideoCanvas::setFrame(const QImage& frame)
{
    static_cast<VideoSurfaceLayer*>(m_surfaceLayer)->setFrame(frame);
    static_cast<VideoOverlayLayer*>(m_overlayLayer)->setFrame(frame);
}

void VideoCanvas::setVideoFrame(const VideoFrame& frame)
{
    static_cast<VideoSurfaceLayer*>(m_surfaceLayer)->setVideoFrame(frame);
}

void VideoCanvas::setRenderService(RenderService* renderService)
{
    static_cast<VideoSurfaceLayer*>(m_surfaceLayer)->setRenderService(renderService);
}

void VideoCanvas::setSourceFrameSize(const QSize& sourceSize)
{
    static_cast<VideoSurfaceLayer*>(m_surfaceLayer)->setSourceFrameSize(sourceSize);
    static_cast<VideoOverlayLayer*>(m_overlayLayer)->setSourceFrameSize(sourceSize);
}

void VideoCanvas::setDisplayScaleFactor(const double scaleFactor)
{
    Q_UNUSED(scaleFactor);
}

void VideoCanvas::setOverlays(const std::vector<TrackOverlay>& overlays)
{
    static_cast<VideoOverlayLayer*>(m_overlayLayer)->setOverlays(overlays);
}

void VideoCanvas::setShowAllLabels(const bool enabled)
{
    static_cast<VideoOverlayLayer*>(m_overlayLayer)->setShowAllLabels(enabled);
}

void VideoCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_surfaceLayer)
    {
        m_surfaceLayer->setGeometry(rect());
    }
    if (m_overlayLayer)
    {
        m_overlayLayer->setGeometry(rect());
        m_overlayLayer->raise();
    }
}
