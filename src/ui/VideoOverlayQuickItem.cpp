#include "ui/VideoOverlayQuickItem.h"

#include <algorithm>

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QVariantMap>

#include "ui/VideoViewportQuickController.h"

namespace
{
QRectF rectFromVariantMap(const QVariantMap& map)
{
    return QRectF{
        map.value(QStringLiteral("x")).toDouble(),
        map.value(QStringLiteral("y")).toDouble(),
        map.value(QStringLiteral("width")).toDouble(),
        map.value(QStringLiteral("height")).toDouble()};
}
}

VideoOverlayQuickItem::VideoOverlayQuickItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &QQuickItem::widthChanged, this, &QQuickItem::update);
    connect(this, &QQuickItem::heightChanged, this, &QQuickItem::update);
}

QObject* VideoOverlayQuickItem::controller() const
{
    return m_controller;
}

void VideoOverlayQuickItem::setController(QObject* controllerObject)
{
    auto* controller = qobject_cast<VideoViewportQuickController*>(controllerObject);
    if (m_controller == controller)
    {
        return;
    }

    disconnectController();
    m_controller = controller;
    if (m_controller)
    {
        m_controllerConnections.append(
            connect(m_controller, &VideoViewportQuickController::overlaysChanged, this, &QQuickItem::update));
        m_controllerConnections.append(
            connect(m_controller, &VideoViewportQuickController::showAllLabelsChanged, this, &QQuickItem::update));
        m_controllerConnections.append(
            connect(m_controller, &VideoViewportQuickController::displayScaleFactorChanged, this, &QQuickItem::update));
        m_controllerConnections.append(
            connect(m_controller, &VideoViewportQuickController::sourceGeometryChanged, this, &QQuickItem::update));
        m_controllerConnections.append(
            connect(m_controller, &VideoViewportQuickController::hasFrameChanged, this, &QQuickItem::update));
    }

    emit controllerChanged();
    update();
}

void VideoOverlayQuickItem::paint(QPainter* painter)
{
    if (!painter || !m_controller || !m_controller->hasFrame())
    {
        return;
    }

    const auto frameRect = rectFromVariantMap(m_controller->frameRect(width(), height()));
    const auto sourceWidth = std::max(1, m_controller->sourceWidth());
    const auto sourceHeight = std::max(1, m_controller->sourceHeight());
    if (!frameRect.isValid() || frameRect.width() <= 0.0 || frameRect.height() <= 0.0)
    {
        return;
    }

    const auto scaleX = frameRect.width() / static_cast<double>(sourceWidth);
    const auto scaleY = frameRect.height() / static_cast<double>(sourceHeight);

    painter->setRenderHint(QPainter::Antialiasing, true);

    for (const auto& overlay : m_controller->overlayData())
    {
        const QPointF canvasPoint{
            frameRect.left() + overlay.imagePoint.x() * scaleX,
            frameRect.top() + overlay.imagePoint.y() * scaleY};

        painter->setBrush(overlay.color);
        painter->setPen(QPen(Qt::black, 2.0));
        painter->drawEllipse(canvasPoint, 7.0, 7.0);

        if (overlay.autoPanEnabled)
        {
            painter->setBrush(QColor{8, 9, 12});
            painter->setPen(QPen(QColor{255, 255, 255, 180}, 1.0));
            painter->drawEllipse(canvasPoint, 2.5, 2.5);
        }

        if (overlay.highlightOpacity > 0.0F)
        {
            painter->setBrush(Qt::NoBrush);
            auto ringColor = overlay.color.lighter(145);
            ringColor.setAlphaF(std::clamp(overlay.highlightOpacity, 0.0F, 1.0F));
            painter->setPen(QPen(ringColor, 2.0));
            painter->drawEllipse(canvasPoint, 12.0, 12.0);
        }

        if (m_controller->showAllLabels() || overlay.showLabel)
        {
            auto label = overlay.label;
            if (overlay.hasAttachedAudio)
            {
                label += QStringLiteral(" [snd]");
            }

            auto labelFont = painter->font();
            labelFont.setPointSizeF(9.0);
            painter->setFont(labelFont);
            const QFontMetricsF metrics{labelFont};
            const auto labelWidth = std::max(34.0, metrics.horizontalAdvance(label) + 16.0);
            const auto labelHeight = std::max(22.0, metrics.height() + 6.0);
            const auto labelRect = QRectF{
                canvasPoint.x() + 10.0,
                canvasPoint.y() - (labelHeight * 0.5 + 7.0),
                labelWidth,
                labelHeight};

            QPainterPath path;
            path.addRoundedRect(labelRect, 6.0, 6.0);
            painter->fillPath(
                path,
                overlay.isSelected ? QColor{34, 42, 57, 235} : QColor{16, 18, 24, 220});
            painter->setPen(Qt::white);
            painter->drawText(labelRect.adjusted(8.0, 0.0, -8.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, label);
        }
    }
}

void VideoOverlayQuickItem::disconnectController()
{
    for (const auto& connection : m_controllerConnections)
    {
        disconnect(connection);
    }
    m_controllerConnections.clear();
}
