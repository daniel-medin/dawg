#include "ui/VideoOverlayQuickItem.h"

#include <algorithm>
#include <atomic>

#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QPainter>
#include <QTransform>

#include "ui/VideoViewportQuickController.h"

namespace
{
std::atomic<std::int64_t> g_overlayPaintNs{0};
std::atomic<int> g_overlayPaintCount{0};
std::atomic<int> g_overlayLabelCount{0};
}

VideoOverlayQuickItem::VideoOverlayQuickItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
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

VideoOverlayQuickItem::DebugStats VideoOverlayQuickItem::debugStats()
{
    return DebugStats{
        .paintMs = static_cast<double>(g_overlayPaintNs.load(std::memory_order_relaxed)) / 1'000'000.0,
        .overlayCount = g_overlayPaintCount.load(std::memory_order_relaxed),
        .labelCount = g_overlayLabelCount.load(std::memory_order_relaxed)};
}

void VideoOverlayQuickItem::paint(QPainter* painter)
{
    QElapsedTimer timer;
    timer.start();

    if (!painter || !m_controller || !m_controller->hasFrame())
    {
        g_overlayPaintNs.store(0, std::memory_order_relaxed);
        g_overlayPaintCount.store(0, std::memory_order_relaxed);
        g_overlayLabelCount.store(0, std::memory_order_relaxed);
        return;
    }

    const QRectF frameRect{0.0, 0.0, width(), height()};
    const auto sourceWidth = std::max(1, m_controller->sourceWidth());
    const auto sourceHeight = std::max(1, m_controller->sourceHeight());
    if (!frameRect.isValid() || frameRect.width() <= 0.0 || frameRect.height() <= 0.0)
    {
        g_overlayPaintNs.store(0, std::memory_order_relaxed);
        g_overlayPaintCount.store(0, std::memory_order_relaxed);
        g_overlayLabelCount.store(0, std::memory_order_relaxed);
        return;
    }

    const auto scaleX = frameRect.width() / static_cast<double>(sourceWidth);
    const auto scaleY = frameRect.height() / static_cast<double>(sourceHeight);

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    QFont labelFont = painter->font();
    labelFont.setPointSizeF(9.0);
    painter->setFont(labelFont);
    const QFontMetricsF labelMetrics{labelFont};

    const auto showAllLabels = m_controller->showAllLabels();
    const QPen nodePen{Qt::black, 2.0};
    const QPen autoPanPen{QColor{255, 255, 255, 180}, 1.0};
    const QColor autoPanFill{8, 9, 12};
    const QColor selectedLabelFill{34, 42, 57, 235};
    const QColor defaultLabelFill{16, 18, 24, 220};

    int labelCount = 0;
    const auto& overlays = m_controller->overlayData();
    for (const auto& overlay : overlays)
    {
        const QPointF canvasPoint{
            frameRect.left() + overlay.imagePoint.x() * scaleX,
            frameRect.top() + overlay.imagePoint.y() * scaleY};

        painter->setBrush(overlay.color);
        painter->setPen(nodePen);
        painter->drawEllipse(canvasPoint, 7.0, 7.0);

        if (overlay.autoPanEnabled)
        {
            painter->setBrush(autoPanFill);
            painter->setPen(autoPanPen);
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

        if (showAllLabels || overlay.showLabel)
        {
            ++labelCount;
            auto cacheIt = m_labelCache.find(overlay.label);
            if (cacheIt == m_labelCache.end())
            {
                LabelCacheEntry entry;
                entry.text.setText(overlay.label);
                entry.text.setTextFormat(Qt::PlainText);
                entry.text.setPerformanceHint(QStaticText::AggressiveCaching);
                entry.text.prepare(QTransform{}, labelFont);
                entry.width = labelMetrics.horizontalAdvance(overlay.label);
                entry.height = labelMetrics.height();
                cacheIt = m_labelCache.insert(overlay.label, entry);
            }

            const auto labelWidth = std::max(34.0, cacheIt->width + 16.0);
            const auto labelHeight = std::max(22.0, cacheIt->height + 6.0);
            const auto labelRect = QRectF{
                canvasPoint.x() + 10.0,
                canvasPoint.y() - (labelHeight * 0.5 + 7.0),
                labelWidth,
                labelHeight};

            painter->setPen(Qt::NoPen);
            painter->setBrush(overlay.isSelected ? selectedLabelFill : defaultLabelFill);
            painter->drawRoundedRect(labelRect, 6.0, 6.0);
            painter->setPen(Qt::white);
            painter->drawStaticText(
                QPointF{
                    labelRect.left() + 8.0,
                    labelRect.top() + std::max(0.0, (labelRect.height() - cacheIt->height) * 0.5)},
                cacheIt->text);
        }
    }

    g_overlayPaintNs.store(timer.nsecsElapsed(), std::memory_order_relaxed);
    g_overlayPaintCount.store(static_cast<int>(overlays.size()), std::memory_order_relaxed);
    g_overlayLabelCount.store(labelCount, std::memory_order_relaxed);
}

void VideoOverlayQuickItem::disconnectController()
{
    for (const auto& connection : m_controllerConnections)
    {
        disconnect(connection);
    }
    m_controllerConnections.clear();
}
