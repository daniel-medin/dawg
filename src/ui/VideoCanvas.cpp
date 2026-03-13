#include "ui/VideoCanvas.h"

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

VideoCanvas::VideoCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(960, 540);
    setMouseTracking(true);
}

void VideoCanvas::setFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void VideoCanvas::setOverlays(const std::vector<TrackOverlay>& overlays)
{
    m_overlays = overlays;
    update();
}

void VideoCanvas::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{12, 14, 18});

    const auto frameRect = imageRenderRect();
    painter.fillRect(frameRect, QColor{24, 27, 32});

    if (m_frame.isNull())
    {
        painter.setPen(QColor{210, 214, 220});
        painter.drawText(
            rect(),
            Qt::AlignCenter,
            QStringLiteral("Open a clip, click to add a node, drag nodes to move them, and use Play (Space) to preview."));
        return;
    }

    painter.drawImage(frameRect, m_frame);

    painter.setPen(QPen(QColor{255, 255, 255, 30}, 1.0));
    painter.drawRect(frameRect);

    const auto scaleX = frameRect.width() / static_cast<double>(m_frame.width());
    const auto scaleY = frameRect.height() / static_cast<double>(m_frame.height());

    for (const auto& overlay : m_overlays)
    {
        const QPointF canvasPoint{
            frameRect.left() + overlay.imagePoint.x() * scaleX,
            frameRect.top() + overlay.imagePoint.y() * scaleY
        };

        painter.setBrush(overlay.color);
        painter.setPen(QPen(Qt::black, 2.0));
        painter.drawEllipse(canvasPoint, 7.0, 7.0);

        if (overlay.highlightOpacity > 0.0F)
        {
            painter.setBrush(Qt::NoBrush);
            auto ringColor = overlay.color.lighter(145);
            ringColor.setAlphaF(std::clamp(overlay.highlightOpacity, 0.0F, 1.0F));
            painter.setPen(QPen(ringColor, 2.0));
            painter.drawEllipse(canvasPoint, 12.0, 12.0);
        }

        const auto labelRect = QRectF{canvasPoint.x() + 10.0, canvasPoint.y() - 18.0, 120.0, 22.0};
        QPainterPath path;
        path.addRoundedRect(labelRect, 6.0, 6.0);
        painter.fillPath(
            path,
            overlay.isSelected ? QColor{34, 42, 57, 235} : QColor{16, 18, 24, 220});
        painter.setPen(Qt::white);

        auto label = overlay.label;
        if (overlay.hasAttachedAudio)
        {
            label += QStringLiteral(" [snd]");
        }

        painter.drawText(labelRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }
}

void VideoCanvas::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    if (event->button() != Qt::LeftButton || m_frame.isNull())
    {
        return;
    }

    const auto frameRect = imageRenderRect();
    if (!frameRect.contains(event->position()))
    {
        return;
    }

    const auto trackId = trackAt(event->position());
    if (!trackId.isNull())
    {
        m_draggedTrackId = trackId;
        emit trackSelected(trackId);
        return;
    }

    emit seedPointRequested(widgetToImagePoint(event->position()));
}

void VideoCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    if (m_draggedTrackId.isNull() || m_frame.isNull())
    {
        return;
    }

    emit selectedTrackMoved(widgetToImagePoint(event->position()));
}

void VideoCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        m_draggedTrackId = {};
    }
}

QUuid VideoCanvas::trackAt(const QPointF& widgetPoint) const
{
    if (m_frame.isNull())
    {
        return {};
    }

    const auto frameRect = imageRenderRect();
    const auto scaleX = frameRect.width() / static_cast<double>(m_frame.width());
    const auto scaleY = frameRect.height() / static_cast<double>(m_frame.height());
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

QRectF VideoCanvas::imageRenderRect() const
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

    const auto bounded = QSize{width(), height()};
    const auto scaled = m_frame.size().scaled(bounded, Qt::KeepAspectRatio);

    return QRectF{
        (width() - scaled.width()) / 2.0,
        (height() - scaled.height()) / 2.0,
        static_cast<double>(scaled.width()),
        static_cast<double>(scaled.height())
    };
}

QPointF VideoCanvas::widgetToImagePoint(const QPointF& widgetPoint) const
{
    const auto frameRect = imageRenderRect();
    const auto xRatio = (widgetPoint.x() - frameRect.left()) / frameRect.width();
    const auto yRatio = (widgetPoint.y() - frameRect.top()) / frameRect.height();

    return QPointF{
        std::clamp(xRatio, 0.0, 1.0) * m_frame.width(),
        std::clamp(yRatio, 0.0, 1.0) * m_frame.height()
    };
}
