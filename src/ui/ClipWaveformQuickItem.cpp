#include "ui/ClipWaveformQuickItem.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace
{
juce::File toJuceFile(const QString& filePath)
{
    const auto widePath = filePath.toStdWString();
    return juce::File(juce::String(widePath.c_str()));
}

juce::AudioFormatManager& waveformFormatManager()
{
    static juce::AudioFormatManager manager;
    static const bool registered = []()
    {
        manager.registerBasicFormats();
        return true;
    }();
    Q_UNUSED(registered);
    return manager;
}

bool sameWaveformState(const std::optional<ClipEditorState>& lhs, const std::optional<ClipEditorState>& rhs)
{
    if (lhs.has_value() != rhs.has_value())
    {
        return false;
    }

    if (!lhs.has_value())
    {
        return true;
    }

    return lhs->hasAttachedAudio == rhs->hasAttachedAudio
        && lhs->assetPath == rhs->assetPath
        && lhs->sourceDurationMs == rhs->sourceDurationMs
        && lhs->clipStartMs == rhs->clipStartMs
        && lhs->clipEndMs == rhs->clipEndMs
        && lhs->playheadMs == rhs->playheadMs;
}
}

ClipWaveformQuickItem::ClipWaveformQuickItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemHasContents, true);
}

void ClipWaveformQuickItem::setState(const std::optional<ClipEditorState>& state)
{
    if (!state.has_value() || !state->hasAttachedAudio)
    {
        if (!m_state.has_value() && m_loadedAssetPath.isEmpty() && m_peaks.empty())
        {
            return;
        }

        m_state.reset();
        m_loadedAssetPath.clear();
        m_peaks.clear();
        m_dragMode = DragMode::None;
        m_horizontalZoom = 1.0;
        m_verticalZoom = 1.0;
        m_viewStartMs = 0.0;
        updateScrollMetrics();
        update();
        return;
    }

    if (sameWaveformState(m_state, state))
    {
        return;
    }

    const auto assetChanged = m_loadedAssetPath != state->assetPath;
    m_state = state;
    if (assetChanged)
    {
        m_loadedAssetPath = state->assetPath;
        loadWaveform(state->assetPath);
        m_horizontalZoom = 1.0;
        m_verticalZoom = 1.0;
        m_viewStartMs = 0.0;
    }
    clampViewWindow();
    updateScrollMetrics();
    update();
}

bool ClipWaveformQuickItem::scrollVisible() const
{
    return m_scrollVisible;
}

int ClipWaveformQuickItem::scrollValue() const
{
    return m_scrollValue;
}

int ClipWaveformQuickItem::scrollMaximum() const
{
    return m_scrollMaximum;
}

int ClipWaveformQuickItem::scrollPageStep() const
{
    return m_scrollPageStep;
}

void ClipWaveformQuickItem::setViewStartMs(const int viewStartMs)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return;
    }

    const auto previousStartMs = m_viewStartMs;
    m_viewStartMs = static_cast<double>(viewStartMs);
    clampViewWindow();
    if (std::abs(previousStartMs - m_viewStartMs) < 0.5)
    {
        return;
    }

    updateScrollMetrics();
    update();
}

void ClipWaveformQuickItem::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(QRectF{0.0, 0.0, width(), height()}, QColor{10, 13, 18});

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    if (!bounds.isValid())
    {
        return;
    }

    painter->setPen(QPen(QColor{34, 40, 48}, 1.0));
    painter->setBrush(QColor{14, 18, 24});
    painter->drawRect(bounds);

    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        painter->setPen(QColor{126, 136, 148});
        painter->drawText(bounds, Qt::AlignCenter, QStringLiteral("Select a node with audio."));
        return;
    }

    const auto clipRect = selectionRect(bounds);
    painter->fillRect(bounds, QColor{12, 16, 22});
    painter->fillRect(clipRect, QColor{24, 34, 46});

    const auto centerY = bounds.center().y();
    painter->setPen(QPen(QColor{28, 35, 44}, 1.0));
    painter->drawLine(QPointF{bounds.left(), centerY}, QPointF{bounds.right(), centerY});

    if (!m_peaks.empty())
    {
        const auto verticalRadius = std::max(10.0, bounds.height() * 0.45);
        const auto leftX = static_cast<int>(std::floor(bounds.left()));
        const auto rightX = static_cast<int>(std::ceil(bounds.right()));

        painter->setPen(Qt::NoPen);
        for (int pixelX = leftX; pixelX <= rightX; ++pixelX)
        {
            const auto timeMs = m_viewStartMs
                + (((static_cast<double>(pixelX) + 0.5) - bounds.left()) / std::max(1.0, bounds.width()))
                    * visibleDurationMs();
            const auto amplitude = interpolatedPeakAtMs(timeMs);
            const auto halfHeight = std::max(
                1.0,
                std::min(bounds.height() * 0.48, amplitude * verticalRadius * m_verticalZoom));
            const auto active = clipRect.contains(QPointF{static_cast<double>(pixelX), centerY});
            painter->setBrush(active ? QColor{202, 216, 234} : QColor{92, 102, 114});
            painter->drawRect(
                QRectF{
                    QPointF{static_cast<double>(pixelX), centerY - halfHeight},
                    QSizeF{1.0, (halfHeight * 2.0) + 1.0}});
        }
    }
    else
    {
        painter->setPen(QColor{126, 136, 148});
        painter->drawText(bounds, Qt::AlignCenter, QStringLiteral("Waveform unavailable."));
    }

    paintHandle(*painter, bounds, clipStartX(bounds), QColor{224, 230, 236});
    paintHandle(*painter, bounds, clipEndX(bounds), QColor{224, 230, 236});
    paintPlayhead(*painter, bounds, QColor{241, 196, 86});
}

void ClipWaveformQuickItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        event->ignore();
        return;
    }

    forceActiveFocus(Qt::MouseFocusReason);

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    const auto x = event->position().x();
    constexpr double handleHitRadius = 8.0;

    if (std::abs(x - clipStartX(bounds)) <= handleHitRadius)
    {
        m_dragMode = DragMode::Start;
        applyDragAt(event->position());
        event->accept();
        return;
    }

    if (std::abs(x - clipEndX(bounds)) <= handleHitRadius)
    {
        m_dragMode = DragMode::End;
        applyDragAt(event->position());
        event->accept();
        return;
    }

    m_dragMode = DragMode::Playhead;
    applyPlayheadAt(event->position());
    event->accept();
}

void ClipWaveformQuickItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        unsetCursor();
        event->ignore();
        return;
    }

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    constexpr double handleHitRadius = 8.0;
    const auto x = event->position().x();
    if (m_dragMode != DragMode::None)
    {
        setCursor(Qt::SizeHorCursor);
        if (m_dragMode == DragMode::Playhead)
        {
            applyPlayheadAt(event->position());
        }
        else
        {
            applyDragAt(event->position());
        }
        event->accept();
        return;
    }

    if (std::abs(x - clipStartX(bounds)) <= handleHitRadius
        || std::abs(x - clipEndX(bounds)) <= handleHitRadius
        || std::abs(x - playheadX(bounds)) <= handleHitRadius)
    {
        setCursor(Qt::SizeHorCursor);
    }
    else
    {
        unsetCursor();
    }

    event->accept();
}

void ClipWaveformQuickItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragMode = DragMode::None;
    }
    event->accept();
}

void ClipWaveformQuickItem::wheelEvent(QWheelEvent* event)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        event->ignore();
        return;
    }

    const auto deltaY = event->angleDelta().y();
    if (deltaY == 0)
    {
        event->ignore();
        return;
    }

    constexpr double zoomStepFactor = 1.18;
    const auto steps = static_cast<double>(deltaY) / 120.0;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
    {
        m_verticalZoom = std::clamp(m_verticalZoom * std::pow(zoomStepFactor, steps), 0.5, 8.0);
        update();
        event->accept();
        return;
    }

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    if (!bounds.isValid())
    {
        event->ignore();
        return;
    }

    const auto anchorRatio = std::clamp(
        (event->position().x() - bounds.left()) / std::max(1.0, bounds.width()),
        0.0,
        1.0);
    const auto anchorMs = m_viewStartMs + visibleDurationMs() * anchorRatio;
    const auto nextZoom = std::clamp(
        m_horizontalZoom * std::pow(zoomStepFactor, steps),
        1.0,
        maxHorizontalZoom());
    if (std::abs(nextZoom - m_horizontalZoom) < 0.001)
    {
        event->accept();
        return;
    }

    m_horizontalZoom = nextZoom;
    m_viewStartMs = anchorMs - visibleDurationMs() * anchorRatio;
    clampViewWindow();
    updateScrollMetrics();
    update();
    event->accept();
}

void ClipWaveformQuickItem::loadWaveform(const QString& filePath)
{
    m_peaks.clear();

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        waveformFormatManager().createReaderFor(toJuceFile(filePath)));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
    {
        return;
    }

    constexpr int kPeakBucketCount = 1400;
    const auto totalSamples = reader->lengthInSamples;
    const auto bucketCount = std::max(64, std::min<int>(kPeakBucketCount, static_cast<int>(totalSamples)));
    const auto bucketSamples = std::max<juce::int64>(
        1,
        static_cast<juce::int64>(std::ceil(static_cast<double>(totalSamples) / bucketCount)));

    m_peaks.reserve(static_cast<std::size_t>(bucketCount));

    for (int bucketIndex = 0; bucketIndex < bucketCount; ++bucketIndex)
    {
        const auto startSample = static_cast<juce::int64>(bucketIndex) * bucketSamples;
        if (startSample >= totalSamples)
        {
            break;
        }

        const auto samplesToRead =
            static_cast<int>(std::min<juce::int64>(bucketSamples, totalSamples - startSample));
        juce::AudioBuffer<float> buffer(reader->numChannels, samplesToRead);
        reader->read(&buffer, 0, samplesToRead, startSample, true, true);

        float peak = 0.0F;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* channelData = buffer.getReadPointer(channel);
            for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
            {
                peak = std::max(peak, std::abs(channelData[sampleIndex]));
            }
        }

        m_peaks.push_back(peak);
    }
}

QRectF ClipWaveformQuickItem::selectionRect(const QRectF& bounds) const
{
    const auto left = std::min(clipStartX(bounds), clipEndX(bounds));
    const auto right = std::max(clipStartX(bounds), clipEndX(bounds));
    return QRectF{QPointF{left, bounds.top()}, QPointF{right, bounds.bottom()}}.intersected(bounds);
}

double ClipWaveformQuickItem::clipStartX(const QRectF& bounds) const
{
    return xForMs(bounds, m_state.has_value() ? m_state->clipStartMs : 0);
}

double ClipWaveformQuickItem::clipEndX(const QRectF& bounds) const
{
    return xForMs(bounds, m_state.has_value() ? m_state->clipEndMs : 0);
}

double ClipWaveformQuickItem::playheadX(const QRectF& bounds) const
{
    if (!m_state.has_value() || !m_state->playheadMs.has_value())
    {
        return clipStartX(bounds);
    }

    return xForMs(bounds, *m_state->playheadMs);
}

double ClipWaveformQuickItem::xForMs(const QRectF& bounds, const int timeMs) const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return bounds.left();
    }

    const auto ratio =
        (static_cast<double>(std::clamp(timeMs, 0, m_state->sourceDurationMs)) - m_viewStartMs)
        / std::max(1.0, visibleDurationMs());
    return bounds.left() + ratio * bounds.width();
}

int ClipWaveformQuickItem::msForX(const QRectF& bounds, const double x) const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 0;
    }

    const auto ratio = std::clamp((x - bounds.left()) / std::max(1.0, bounds.width()), 0.0, 1.0);
    return static_cast<int>(std::lround(m_viewStartMs + ratio * visibleDurationMs()));
}

double ClipWaveformQuickItem::interpolatedPeakAtMs(const double timeMs) const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0 || m_peaks.empty())
    {
        return 0.0;
    }

    if (m_peaks.size() == 1)
    {
        return std::clamp(static_cast<double>(m_peaks.front()), 0.0, 1.0);
    }

    const auto normalizedTime =
        std::clamp(timeMs, 0.0, static_cast<double>(m_state->sourceDurationMs))
        / std::max(1.0, static_cast<double>(m_state->sourceDurationMs));
    const auto scaledIndex = normalizedTime * static_cast<double>(m_peaks.size() - 1);
    const auto leftIndex = static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::floor(scaledIndex)),
        0,
        static_cast<int>(m_peaks.size() - 1)));
    const auto rightIndex = static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::ceil(scaledIndex)),
        0,
        static_cast<int>(m_peaks.size() - 1)));
    const auto blend = std::clamp(scaledIndex - std::floor(scaledIndex), 0.0, 1.0);
    const auto leftPeak = static_cast<double>(m_peaks[leftIndex]);
    const auto rightPeak = static_cast<double>(m_peaks[rightIndex]);
    return std::clamp(leftPeak + ((rightPeak - leftPeak) * blend), 0.0, 1.0);
}

void ClipWaveformQuickItem::paintHandle(QPainter& painter, const QRectF& bounds, const double x, const QColor& color) const
{
    if (x < bounds.left() - 6.0 || x > bounds.right() + 6.0)
    {
        return;
    }

    painter.setPen(QPen(color, 2.0));
    painter.drawLine(QPointF{x, bounds.top()}, QPointF{x, bounds.bottom()});
    painter.drawLine(QPointF{x - 4.0, bounds.top() + 7.0}, QPointF{x + 4.0, bounds.top() + 7.0});
    painter.drawLine(QPointF{x - 4.0, bounds.bottom() - 7.0}, QPointF{x + 4.0, bounds.bottom() - 7.0});
}

void ClipWaveformQuickItem::paintPlayhead(QPainter& painter, const QRectF& bounds, const QColor& color) const
{
    if (!m_state.has_value() || !m_state->playheadMs.has_value())
    {
        return;
    }

    const auto x = playheadX(bounds);
    if (x < bounds.left() - 6.0 || x > bounds.right() + 6.0)
    {
        return;
    }

    painter.setPen(QPen(color, 1.6));
    painter.drawLine(QPointF{x, bounds.top()}, QPointF{x, bounds.bottom()});
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    const QPolygonF marker{
        QPointF{x - 5.0, bounds.top()},
        QPointF{x + 5.0, bounds.top()},
        QPointF{x, bounds.top() + 8.0}
    };
    painter.drawPolygon(marker);
}

void ClipWaveformQuickItem::applyDragAt(const QPointF& position)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return;
    }

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    auto clipStartMs = m_state->clipStartMs;
    auto clipEndMs = m_state->clipEndMs;
    const auto draggedMs = msForX(bounds, position.x());

    if (m_dragMode == DragMode::Start)
    {
        clipStartMs = std::clamp(draggedMs, 0, std::max(0, clipEndMs - 1));
    }
    else if (m_dragMode == DragMode::End)
    {
        clipEndMs = std::clamp(draggedMs, clipStartMs + 1, m_state->sourceDurationMs);
    }

    if (clipStartMs == m_state->clipStartMs && clipEndMs == m_state->clipEndMs)
    {
        return;
    }

    m_state->clipStartMs = clipStartMs;
    m_state->clipEndMs = clipEndMs;
    emit clipRangeChanged(clipStartMs, clipEndMs);
    update();
}

void ClipWaveformQuickItem::applyPlayheadAt(const QPointF& position)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return;
    }

    const QRectF bounds = QRectF{0.0, 0.0, width(), height()}.adjusted(10.0, 10.0, -10.0, -10.0);
    const auto nextPlayheadMs = std::clamp(
        msForX(bounds, position.x()),
        0,
        std::max(0, m_state->sourceDurationMs - 1));
    if (m_state->playheadMs.has_value() && *m_state->playheadMs == nextPlayheadMs)
    {
        return;
    }

    m_state->playheadMs = nextPlayheadMs;
    emit playheadChanged(nextPlayheadMs);
    update();
}

double ClipWaveformQuickItem::visibleDurationMs() const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 1.0;
    }

    return std::max(12.0, static_cast<double>(m_state->sourceDurationMs) / std::max(1.0, m_horizontalZoom));
}

double ClipWaveformQuickItem::maxHorizontalZoom() const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 1.0;
    }

    return std::max(1.0, static_cast<double>(m_state->sourceDurationMs) / 12.0);
}

void ClipWaveformQuickItem::clampViewWindow()
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        m_viewStartMs = 0.0;
        return;
    }

    const auto maxStartMs = std::max(0.0, static_cast<double>(m_state->sourceDurationMs) - visibleDurationMs());
    m_viewStartMs = std::clamp(m_viewStartMs, 0.0, maxStartMs);
}

void ClipWaveformQuickItem::updateScrollMetrics()
{
    const auto totalDurationMs = (m_state.has_value() && m_state->sourceDurationMs > 0) ? m_state->sourceDurationMs : 0;
    const auto visibleMs = totalDurationMs > 0 ? static_cast<int>(std::lround(visibleDurationMs())) : 0;
    const auto viewStartMs = totalDurationMs > 0 ? static_cast<int>(std::lround(m_viewStartMs)) : 0;
    const auto maxStartMs = std::max(0, totalDurationMs - visibleMs);
    const auto showScrollBar = totalDurationMs > 0 && maxStartMs > 0;

    if (m_scrollVisible == showScrollBar
        && m_scrollValue == std::clamp(viewStartMs, 0, maxStartMs)
        && m_scrollMaximum == maxStartMs
        && m_scrollPageStep == std::max(1, visibleMs))
    {
        return;
    }

    m_scrollVisible = showScrollBar;
    m_scrollMaximum = maxStartMs;
    m_scrollPageStep = std::max(1, visibleMs);
    m_scrollValue = std::clamp(viewStartMs, 0, maxStartMs);
    emit scrollMetricsChanged();
}
