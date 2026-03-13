#include "ui/VideoCanvas.h"

#include <algorithm>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

VideoCanvas::VideoCanvas(QWidget* parent)
    : QWidget(parent)
    , m_emptyStateLogo(QStringLiteral(":/branding/logo.png"))
{
    setMinimumSize(960, 540);
    setMouseTracking(true);
    setAcceptDrops(true);
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

void VideoCanvas::setShowAllLabels(const bool enabled)
{
    if (m_showAllLabels == enabled)
    {
        return;
    }

    m_showAllLabels = enabled;
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
        paintWelcomeState(painter);
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

        if (m_showAllLabels || overlay.showLabel)
        {
            auto label = overlay.label;
            if (overlay.hasAttachedAudio)
            {
                label += QStringLiteral(" [snd]");
            }

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

void VideoCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_frame.isNull()
        && !audioPathFromMimeData(event->mimeData()).isEmpty())
    {
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void VideoCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (!m_frame.isNull()
        && !audioPathFromMimeData(event->mimeData()).isEmpty())
    {
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void VideoCanvas::dropEvent(QDropEvent* event)
{
    if (m_frame.isNull())
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

    emit audioDropped(assetPath, widgetToImagePoint(event->position()));
    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
}

void VideoCanvas::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    if (m_frame.isNull())
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
            emit trackContextMenuRequested(trackId, event->globalPosition().toPoint());
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
        emit trackSelected(trackId);
        return;
    }

    m_pressPosition = event->position();
    m_selectionRect = QRectF{m_pressPosition, m_pressPosition};
    m_pendingSeed = true;
    m_isMarqueeSelecting = false;
}

void VideoCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    if (m_draggedTrackId.isNull() || m_frame.isNull())
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

    emit selectedTrackMoved(widgetToImagePoint(event->position()));
}

void VideoCanvas::mouseReleaseEvent(QMouseEvent* event)
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
            emit tracksSelected(tracksInRect(m_selectionRect.normalized()));
            m_isMarqueeSelecting = false;
            m_pendingSeed = false;
            m_selectionRect = {};
            update();
            return;
        }

        if (m_pendingSeed)
        {
            m_pendingSeed = false;
            emit seedPointRequested(widgetToImagePoint(event->position()));
        }

        m_draggedTrackId = {};
    }
}

QString VideoCanvas::audioPathFromMimeData(const QMimeData* mimeData)
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

QList<QUuid> VideoCanvas::tracksInRect(const QRectF& widgetRect) const
{
    QList<QUuid> trackIds;

    if (m_frame.isNull() || widgetRect.isEmpty())
    {
        return trackIds;
    }

    const auto frameRect = imageRenderRect();
    const auto scaleX = frameRect.width() / static_cast<double>(m_frame.width());
    const auto scaleY = frameRect.height() / static_cast<double>(m_frame.height());
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

void VideoCanvas::paintWelcomeState(QPainter& painter)
{
    const auto frameRect = imageRenderRect();

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
        painter.drawImage(logoRect, m_emptyStateLogo);
    }

    auto headlineFont = painter.font();
    headlineFont.setPointSizeF(headlineFont.pointSizeF() + 6.0);
    headlineFont.setBold(true);
    painter.setFont(headlineFont);
    painter.setPen(QColor{240, 244, 248});
    painter.drawText(
        QRectF{contentRect.left(), contentRect.top() + logoSize.height() + 16.0, contentRect.width(), 34.0},
        Qt::AlignHCenter | Qt::AlignVCenter,
        QStringLiteral("Video-first node audio editing"));

    auto bodyFont = painter.font();
    bodyFont.setPointSizeF(bodyFont.pointSizeF() - 4.0);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor{166, 178, 194});
    painter.drawText(
        QRectF{contentRect.left() - 40.0, contentRect.top() + logoSize.height() + 56.0, contentRect.width() + 80.0, 54.0},
        Qt::AlignHCenter | Qt::TextWordWrap,
        QStringLiteral("Open a clip, click the frame to place a node, then attach sound and shape it across motion."));

    const QRectF hintRect{
        contentRect.center().x() - 126.0,
        contentRect.top() + logoSize.height() + 124.0,
        252.0,
        32.0
    };
    QPainterPath hintPath;
    hintPath.addRoundedRect(hintRect, 16.0, 16.0);
    painter.fillPath(hintPath, QColor{32, 49, 72, 220});
    painter.setPen(QColor{214, 225, 238});
    painter.drawText(
        hintRect,
        Qt::AlignCenter,
        QStringLiteral("Start with File > Open Video"));
}
