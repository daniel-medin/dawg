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
            QStringLiteral("Open a clip, click a target to seed tracking, then press Play."));
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

        if (overlay.isSeedFrame)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(overlay.color.lighter(145), 2.0));
            painter.drawEllipse(canvasPoint, 12.0, 12.0);
        }

        const auto labelRect = QRectF{canvasPoint.x() + 10.0, canvasPoint.y() - 18.0, 120.0, 22.0};
        QPainterPath path;
        path.addRoundedRect(labelRect, 6.0, 6.0);
        painter.fillPath(path, QColor{16, 18, 24, 220});
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

    emit seedPointRequested(widgetToImagePoint(event->position()));
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

