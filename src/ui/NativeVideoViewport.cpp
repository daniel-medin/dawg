#include "ui/NativeVideoViewport.h"

#include <algorithm>

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace
{
QRectF fallbackFrameRect(const QSize& widgetSize)
{
    const auto width = std::max(320, widgetSize.width() - 64);
    const auto height = std::max(180, widgetSize.height() - 64);
    return QRectF{
        (widgetSize.width() - width) / 2.0,
        (widgetSize.height() - height) / 2.0,
        static_cast<double>(width),
        static_cast<double>(height)
    };
}
}

NativeVideoViewport::NativeVideoViewport(QWidget* parent)
    : QWidget(parent)
    , m_ownedRenderService(std::make_unique<RenderService>())
    , m_nativeTarget(new QWidget(this))
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    m_nativeTarget->setAttribute(Qt::WA_NativeWindow);
    m_nativeTarget->setAttribute(Qt::WA_OpaquePaintEvent);
    m_nativeTarget->setAttribute(Qt::WA_NoSystemBackground);
    m_nativeTarget->setAutoFillBackground(false);
    m_nativeTarget->hide();
}

void NativeVideoViewport::setPresentedFrame(const QImage& frame, const VideoFrame& videoFrame, const QSize& sourceSize)
{
    m_frame = frame;
    m_videoFrame = videoFrame;
    m_sourceFrameSize = sourceSize;
    update();
}

void NativeVideoViewport::setRenderService(RenderService* renderService)
{
    m_renderService = renderService ? renderService : m_ownedRenderService.get();
    update();
}

void NativeVideoViewport::setOverlays(const std::vector<TrackOverlay>& overlays)
{
    m_overlays = overlays;
    update();
}

void NativeVideoViewport::setShowAllLabels(const bool enabled)
{
    if (m_showAllLabels == enabled)
    {
        return;
    }

    m_showAllLabels = enabled;
    update();
}

void NativeVideoViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto frameRect = imageRenderRect();
    updateNativeTargetGeometry(frameRect);
    const auto overlayImage = buildOverlayImage(frameRect);

    QPainter painter(this);
    painter.fillRect(rect(), QColor{12, 14, 18});

    if (tryNativePresent(frameRect, overlayImage))
    {
        painter.setPen(QPen(QColor{255, 255, 255, 30}, 1.0));
        painter.drawRect(frameRect);
        return;
    }

    painter.fillRect(frameRect, QColor{24, 27, 32});
    if (!m_frame.isNull())
    {
        painter.drawImage(frameRect, m_frame);
    }

    if (!overlayImage.isNull())
    {
        painter.drawImage(QPoint{0, 0}, overlayImage);
    }
}

void NativeVideoViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateNativeTargetGeometry(imageRenderRect());
}

QRectF NativeVideoViewport::imageRenderRect() const
{
    if (m_frame.isNull())
    {
        return fallbackFrameRect(size());
    }

    const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_frame.size();
    const auto scaled = sourceSize.scaled(size(), Qt::KeepAspectRatio);
    return QRectF{
        (width() - scaled.width()) / 2.0,
        (height() - scaled.height()) / 2.0,
        static_cast<double>(scaled.width()),
        static_cast<double>(scaled.height())
    };
}

QImage NativeVideoViewport::buildOverlayImage(const QRectF& frameRect) const
{
    if (size().isEmpty())
    {
        return {};
    }

    QImage overlayImage(size(), QImage::Format_ARGB32_Premultiplied);
    overlayImage.fill(Qt::transparent);

    QPainter painter(&overlayImage);
    painter.setRenderHint(QPainter::Antialiasing);
    paintOverlayContent(painter, frameRect);
    return overlayImage;
}

void NativeVideoViewport::updateNativeTargetGeometry(const QRectF& frameRect)
{
    const auto alignedRect = frameRect.toAlignedRect();
    if (m_nativeTargetRect == alignedRect)
    {
        return;
    }

    m_nativeTargetRect = alignedRect;
    m_nativeTarget->setGeometry(m_nativeTargetRect);
    m_nativeTarget->raise();
}

bool NativeVideoViewport::tryNativePresent(const QRectF& frameRect, const QImage& overlayImage)
{
    if (!m_renderService
        || !m_renderService->canPresentToNativeWindow()
        || !m_videoFrame.isValid()
        || !m_nativeTargetRect.isValid())
    {
        m_nativeTarget->hide();
        return false;
    }

    if (!m_nativeTarget->isVisible())
    {
        m_nativeTarget->show();
    }

    const auto presented = m_renderService->presentToNativeWindow(
        m_nativeTarget,
        m_nativeTargetRect.size(),
        m_videoFrame,
        QRectF{
            0.0,
            0.0,
            static_cast<double>(m_nativeTargetRect.width()),
            static_cast<double>(m_nativeTargetRect.height())
        },
        overlayImage);
    if (presented)
    {
        m_nativeTarget->raise();
        return true;
    }

    m_nativeTarget->hide();
    Q_UNUSED(frameRect);
    return false;
}

void NativeVideoViewport::paintOverlayContent(QPainter& painter, const QRectF& frameRect) const
{
    auto badgeFont = painter.font();
    badgeFont.setPointSizeF(9.0);
    badgeFont.setBold(true);
    painter.setFont(badgeFont);

    const QRectF badgeRect{12.0, 12.0, 128.0, 24.0};
    QPainterPath badgePath;
    badgePath.addRoundedRect(badgeRect, 6.0, 6.0);
    painter.fillPath(badgePath, QColor{10, 16, 22, 220});
    painter.setPen(QPen(QColor{90, 255, 180}, 1.0));
    painter.drawPath(badgePath);
    painter.drawText(badgeRect, Qt::AlignCenter, QStringLiteral("NATIVE TEST"));

    painter.setPen(QPen(QColor{90, 255, 180, 220}, 2.0));
    painter.drawRect(frameRect.adjusted(1.0, 1.0, -1.0, -1.0));
    painter.drawLine(frameRect.topLeft(), frameRect.topLeft() + QPointF{28.0, 28.0});
    painter.drawLine(frameRect.topRight(), frameRect.topRight() + QPointF{-28.0, 28.0});
    painter.drawLine(frameRect.bottomLeft(), frameRect.bottomLeft() + QPointF{28.0, -28.0});
    painter.drawLine(frameRect.bottomRight(), frameRect.bottomRight() + QPointF{-28.0, -28.0});

    if (m_frame.isNull())
    {
        return;
    }

    const auto sourceSize = !m_sourceFrameSize.isEmpty() ? m_sourceFrameSize : m_frame.size();
    const auto scaleX = frameRect.width() / static_cast<double>(std::max(1, sourceSize.width()));
    const auto scaleY = frameRect.height() / static_cast<double>(std::max(1, sourceSize.height()));

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
}
