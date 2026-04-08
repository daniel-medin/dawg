#include "ui/ClipWaveformQuickItem.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtGlobal>

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

bool sameWaveformState(const std::optional<AudioClipPreviewState>& lhs, const std::optional<AudioClipPreviewState>& rhs)
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

AudioClipPreviewState audioClipPreviewStateFromMap(const QVariantMap& map)
{
    AudioClipPreviewState state;
    state.label = map.value(QStringLiteral("label")).toString();
    state.assetPath = map.value(QStringLiteral("assetPath")).toString();
    state.clipStartMs = std::max(0, map.value(QStringLiteral("clipStartMs"), 0).toInt());
    state.clipEndMs = std::max(
        state.clipStartMs + 1,
        map.value(QStringLiteral("clipEndMs"), state.clipStartMs + 1).toInt());
    state.sourceDurationMs = std::max(
        state.clipEndMs,
        map.value(QStringLiteral("sourceDurationMs"), state.clipEndMs).toInt());
    state.playheadMs = std::clamp(
        map.value(QStringLiteral("playheadMs"), state.clipStartMs).toInt(),
        0,
        std::max(0, state.sourceDurationMs - 1));
    state.gainDb = static_cast<float>(map.value(QStringLiteral("gainDb"), 0.0).toDouble());
    state.hasAttachedAudio = map.value(QStringLiteral("hasAttachedAudio"), !state.assetPath.isEmpty()).toBool();
    state.loopEnabled = map.value(QStringLiteral("loopEnabled"), false).toBool();
    return state;
}
}

ClipWaveformQuickItem::ClipWaveformQuickItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemHasContents, true);
}

void ClipWaveformQuickItem::setState(const std::optional<AudioClipPreviewState>& state)
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
        m_waveformChannelCount = 0;
        m_dragMode = DragMode::None;
        m_horizontalZoom = 1.0;
        m_viewStartMs = 0.0;
        m_previewWaveformState.clear();
        m_previewClipRangeActive = false;
        m_previewClipStartMs = 0;
        m_previewClipEndMs = 0;
        invalidateWaveformCache();
        updateScrollMetrics();
        update();
        return;
    }

    if (sameWaveformState(m_state, state))
    {
        return;
    }

    const auto assetChanged = m_loadedAssetPath != state->assetPath;
    const auto hadPreviousState = m_state.has_value();
    const auto clipChanged = !hadPreviousState
        || m_state->clipStartMs != state->clipStartMs
        || m_state->clipEndMs != state->clipEndMs;
    m_state = state;
    if (assetChanged)
    {
        m_loadedAssetPath = state->assetPath;
        loadWaveform(state->assetPath);
        m_horizontalZoom = 1.0;
        m_viewStartMs = 0.0;
        invalidateWaveformCache();
    }
    else if (clipChanged)
    {
        invalidateWaveformCache();
    }
    clampViewWindow();
    updateScrollMetrics();
    update();
}

QVariantMap ClipWaveformQuickItem::waveformState() const
{
    return m_waveformState;
}

void ClipWaveformQuickItem::setWaveformState(const QVariantMap& state)
{
    if (m_waveformState == state)
    {
        return;
    }

    m_waveformState = state;
    const auto waveformState = audioClipPreviewStateFromMap(state);
    if (waveformState.hasAttachedAudio && !waveformState.assetPath.isEmpty() && waveformState.sourceDurationMs > 0)
    {
        setState(waveformState);
    }
    else
    {
        setState(std::nullopt);
    }
    emit waveformStateChanged();
}

QVariantMap ClipWaveformQuickItem::previewWaveformState() const
{
    return m_previewWaveformState;
}

void ClipWaveformQuickItem::setPreviewWaveformState(const QVariantMap& state)
{
    const auto active = state.value(QStringLiteral("active"), false).toBool();
    auto startMs = state.value(QStringLiteral("clipStartMs"), 0).toInt();
    auto endMs = state.value(QStringLiteral("clipEndMs"), startMs + 1).toInt();
    if (m_state.has_value() && m_state->sourceDurationMs > 0)
    {
        startMs = std::clamp(startMs, 0, std::max(0, m_state->sourceDurationMs - 1));
        endMs = std::clamp(endMs, startMs + 1, m_state->sourceDurationMs);
    }
    else
    {
        startMs = std::max(0, startMs);
        endMs = std::max(startMs + 1, endMs);
    }

    if (m_previewClipRangeActive == active
        && m_previewClipStartMs == startMs
        && m_previewClipEndMs == endMs)
    {
        return;
    }

    m_previewClipRangeActive = active;
    m_previewClipStartMs = startMs;
    m_previewClipEndMs = endMs;
    m_previewWaveformState = QVariantMap{
        {QStringLiteral("active"), m_previewClipRangeActive},
        {QStringLiteral("clipStartMs"), m_previewClipStartMs},
        {QStringLiteral("clipEndMs"), m_previewClipEndMs}};
    if (m_clipRangeOnly)
    {
        invalidateWaveformCache();
        update();
    }
    emit previewWaveformStateChanged();
}

bool ClipWaveformQuickItem::clipRangeHandlesVisible() const
{
    return m_clipRangeHandlesVisible;
}

void ClipWaveformQuickItem::setClipRangeHandlesVisible(const bool visible)
{
    if (m_clipRangeHandlesVisible == visible)
    {
        return;
    }

    m_clipRangeHandlesVisible = visible;
    invalidateWaveformCache();
    update();
    emit clipRangeHandlesVisibleChanged();
}

bool ClipWaveformQuickItem::clipRangeOnly() const
{
    return m_clipRangeOnly;
}

void ClipWaveformQuickItem::setClipRangeOnly(const bool clipRangeOnly)
{
    if (m_clipRangeOnly == clipRangeOnly)
    {
        return;
    }

    m_clipRangeOnly = clipRangeOnly;
    m_viewStartMs = 0.0;
    clampViewWindow();
    invalidateWaveformCache();
    updateScrollMetrics();
    update();
    emit clipRangeOnlyChanged();
}

bool ClipWaveformQuickItem::playheadVisible() const
{
    return m_playheadVisible;
}

void ClipWaveformQuickItem::setPlayheadVisible(const bool visible)
{
    if (m_playheadVisible == visible)
    {
        return;
    }

    m_playheadVisible = visible;
    update();
    emit playheadVisibleChanged();
}

qreal ClipWaveformQuickItem::contentMargin() const
{
    return m_contentMargin;
}

void ClipWaveformQuickItem::setContentMargin(const qreal margin)
{
    const auto nextMargin = std::clamp(margin, 0.0, 64.0);
    if (qFuzzyCompare(m_contentMargin + 1.0, nextMargin + 1.0))
    {
        return;
    }

    m_contentMargin = nextMargin;
    invalidateWaveformCache();
    update();
    emit contentMarginChanged();
}

qreal ClipWaveformQuickItem::verticalZoom() const
{
    return m_verticalZoom;
}

void ClipWaveformQuickItem::setVerticalZoom(const qreal zoom)
{
    const auto nextZoom = std::clamp(zoom, 0.5, 8.0);
    if (qFuzzyCompare(m_verticalZoom + 1.0, nextZoom + 1.0))
    {
        return;
    }

    m_verticalZoom = nextZoom;
    invalidateWaveformCache();
    update();
    emit verticalZoomChanged();
}

bool ClipWaveformQuickItem::invertedColors() const
{
    return m_invertedColors;
}

void ClipWaveformQuickItem::setInvertedColors(const bool inverted)
{
    if (m_invertedColors == inverted)
    {
        return;
    }

    m_invertedColors = inverted;
    invalidateWaveformCache();
    update();
    emit invertedColorsChanged();
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

    invalidateWaveformCache();
    updateScrollMetrics();
    update();
}

void ClipWaveformQuickItem::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    const QRectF bounds = waveformBounds();
    if (!bounds.isValid())
    {
        return;
    }

    ensureWaveformCache();
    painter->drawImage(QPointF{0.0, 0.0}, m_waveformCache);
    if (m_playheadVisible)
    {
        paintPlayhead(*painter, bounds, QColor{241, 196, 86});
    }
}

void ClipWaveformQuickItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        event->ignore();
        return;
    }

    forceActiveFocus(Qt::MouseFocusReason);

    const QRectF bounds = waveformBounds();
    const auto x = event->position().x();
    constexpr double handleHitRadius = 8.0;

    if (m_clipRangeHandlesVisible && std::abs(x - clipStartX(bounds)) <= handleHitRadius)
    {
        m_dragMode = DragMode::Start;
        applyDragAt(event->position());
        event->accept();
        return;
    }

    if (m_clipRangeHandlesVisible && std::abs(x - clipEndX(bounds)) <= handleHitRadius)
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

    const QRectF bounds = waveformBounds();
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

    if ((m_clipRangeHandlesVisible && std::abs(x - clipStartX(bounds)) <= handleHitRadius)
        || (m_clipRangeHandlesVisible && std::abs(x - clipEndX(bounds)) <= handleHitRadius)
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
        setVerticalZoom(m_verticalZoom * std::pow(zoomStepFactor, steps));
        event->accept();
        return;
    }

    if (m_clipRangeOnly)
    {
        event->ignore();
        return;
    }

    const QRectF bounds = waveformBounds();
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
    invalidateWaveformCache();
    updateScrollMetrics();
    update();
    event->accept();
}

void ClipWaveformQuickItem::loadWaveform(const QString& filePath)
{
    m_peaks.clear();
    m_waveformChannelCount = 0;

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        waveformFormatManager().createReaderFor(toJuceFile(filePath)));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
    {
        return;
    }

    constexpr int kPeakBucketCount = 32768;
    constexpr int kReadChunkSamples = 65536;
    const auto totalSamples = reader->lengthInSamples;
    const auto bucketCount = static_cast<int>(std::clamp<juce::int64>(
        totalSamples,
        1,
        kPeakBucketCount));
    const auto channelCount = std::max(1, static_cast<int>(reader->numChannels));
    m_waveformChannelCount = std::clamp(channelCount, 1, 2);
    m_peaks.assign(static_cast<std::size_t>(bucketCount), WaveformPeak{});

    juce::AudioBuffer<float> buffer(channelCount, kReadChunkSamples);
    for (juce::int64 readStartSample = 0; readStartSample < totalSamples; readStartSample += kReadChunkSamples)
    {
        const auto samplesToRead =
            static_cast<int>(std::min<juce::int64>(kReadChunkSamples, totalSamples - readStartSample));
        buffer.clear();
        if (!reader->read(&buffer, 0, samplesToRead, readStartSample, true, true))
        {
            continue;
        }

        std::array<const float*, 2> channelData{};
        for (int channel = 0; channel < m_waveformChannelCount; ++channel)
        {
            channelData[static_cast<std::size_t>(channel)] = buffer.getReadPointer(channel);
        }

        for (int sampleIndex = 0; sampleIndex < samplesToRead; ++sampleIndex)
        {
            const auto absoluteSample = readStartSample + sampleIndex;
            const auto bucketIndex = static_cast<int>(std::clamp<juce::int64>(
                (absoluteSample * bucketCount) / std::max<juce::int64>(1, totalSamples),
                0,
                bucketCount - 1));
            auto& peak = m_peaks[static_cast<std::size_t>(bucketIndex)];
            for (int channel = 0; channel < m_waveformChannelCount; ++channel)
            {
                const auto* channelSamples = channelData[static_cast<std::size_t>(channel)];
                const auto sample = channelSamples[sampleIndex];
                auto& channelPeak = peak.channels[static_cast<std::size_t>(channel)];
                channelPeak.positive = std::max(channelPeak.positive, sample);
                channelPeak.negative = std::min(channelPeak.negative, sample);
            }
        }
    }
}

QRectF ClipWaveformQuickItem::waveformBounds() const
{
    const auto margin = std::min<double>(
        static_cast<double>(m_contentMargin),
        std::max(0.0, std::min(width(), height()) * 0.45));
    return QRectF{0.0, 0.0, width(), height()}.adjusted(margin, margin, -margin, -margin);
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
        (static_cast<double>(std::clamp(timeMs, 0, m_state->sourceDurationMs))
            - static_cast<double>(displayRangeStartMs())
            - m_viewStartMs)
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
    return std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(displayRangeStartMs()) + m_viewStartMs + ratio * visibleDurationMs())),
        0,
        m_state->sourceDurationMs);
}

ClipWaveformQuickItem::WaveformPeak ClipWaveformQuickItem::peakRangeBetweenMs(
    const double startMs,
    const double endMs) const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0 || m_peaks.empty())
    {
        return {};
    }

    if (m_peaks.size() == 1)
    {
        return m_peaks.front();
    }

    const auto durationMs = std::max(1.0, static_cast<double>(m_state->sourceDurationMs));
    const auto clampedStartMs = std::clamp(std::min(startMs, endMs), 0.0, durationMs);
    const auto clampedEndMs = std::clamp(std::max(startMs, endMs), clampedStartMs, durationMs);
    const auto startScaledIndex = (clampedStartMs / durationMs) * static_cast<double>(m_peaks.size() - 1);
    const auto endScaledIndex = (clampedEndMs / durationMs) * static_cast<double>(m_peaks.size() - 1);
    const auto startIndex = static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::floor(startScaledIndex)),
        0,
        static_cast<int>(m_peaks.size() - 1)));
    const auto endIndex = static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::ceil(endScaledIndex)),
        0,
        static_cast<int>(m_peaks.size() - 1)));

    WaveformPeak result;
    for (auto peakIndex = startIndex; peakIndex <= endIndex; ++peakIndex)
    {
        const auto& peak = m_peaks[peakIndex];
        for (int channel = 0; channel < m_waveformChannelCount; ++channel)
        {
            auto& resultChannel = result.channels[static_cast<std::size_t>(channel)];
            const auto& sourceChannel = peak.channels[static_cast<std::size_t>(channel)];
            resultChannel.positive = std::max(resultChannel.positive, sourceChannel.positive);
            resultChannel.negative = std::min(resultChannel.negative, sourceChannel.negative);
        }
        if (peakIndex == endIndex)
        {
            break;
        }
    }
    for (int channel = 0; channel < m_waveformChannelCount; ++channel)
    {
        auto& resultChannel = result.channels[static_cast<std::size_t>(channel)];
        resultChannel.positive = std::clamp(resultChannel.positive, 0.0F, 1.0F);
        resultChannel.negative = std::clamp(resultChannel.negative, -1.0F, 0.0F);
    }
    return result;
}

int ClipWaveformQuickItem::displayRangeStartMs() const
{
    if (!m_clipRangeOnly || !m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 0;
    }

    if (m_previewClipRangeActive)
    {
        return std::clamp(m_previewClipStartMs, 0, std::max(0, m_state->sourceDurationMs - 1));
    }

    return std::clamp(m_state->clipStartMs, 0, std::max(0, m_state->sourceDurationMs - 1));
}

int ClipWaveformQuickItem::displayRangeEndMs() const
{
    if (!m_clipRangeOnly || !m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return m_state.has_value() ? std::max(1, m_state->sourceDurationMs) : 1;
    }

    const auto startMs = displayRangeStartMs();
    if (m_previewClipRangeActive)
    {
        return std::clamp(m_previewClipEndMs, startMs + 1, m_state->sourceDurationMs);
    }

    return std::clamp(m_state->clipEndMs, startMs + 1, m_state->sourceDurationMs);
}

int ClipWaveformQuickItem::displayRangeDurationMs() const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 1;
    }
    if (!m_clipRangeOnly)
    {
        return m_state->sourceDurationMs;
    }

    const auto startMs = displayRangeStartMs();
    const auto endMs = displayRangeEndMs();
    return std::max(1, endMs - startMs);
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

    const QRectF bounds = waveformBounds();
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
    invalidateWaveformCache();
    emit clipRangeChanged(clipStartMs, clipEndMs);
    update();
}

void ClipWaveformQuickItem::applyPlayheadAt(const QPointF& position)
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return;
    }

    const QRectF bounds = waveformBounds();
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
    if (m_clipRangeOnly)
    {
        return std::max(1.0, static_cast<double>(displayRangeDurationMs()));
    }

    return std::max(12.0, static_cast<double>(displayRangeDurationMs()) / std::max(1.0, m_horizontalZoom));
}

double ClipWaveformQuickItem::maxHorizontalZoom() const
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        return 1.0;
    }
    if (m_clipRangeOnly)
    {
        return 1.0;
    }

    return std::max(1.0, static_cast<double>(displayRangeDurationMs()) / 12.0);
}

void ClipWaveformQuickItem::clampViewWindow()
{
    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        m_viewStartMs = 0.0;
        return;
    }
    if (m_clipRangeOnly)
    {
        m_horizontalZoom = 1.0;
        m_viewStartMs = 0.0;
        return;
    }

    const auto maxStartMs = std::max(0.0, static_cast<double>(displayRangeDurationMs()) - visibleDurationMs());
    m_viewStartMs = std::clamp(m_viewStartMs, 0.0, maxStartMs);
}

void ClipWaveformQuickItem::updateScrollMetrics()
{
    const auto totalDurationMs = (m_state.has_value() && m_state->sourceDurationMs > 0) ? displayRangeDurationMs() : 0;
    const auto visibleMs = totalDurationMs > 0 ? static_cast<int>(std::lround(visibleDurationMs())) : 0;
    const auto viewStartMs = totalDurationMs > 0 ? static_cast<int>(std::lround(m_viewStartMs)) : 0;
    const auto maxStartMs = std::max(0, totalDurationMs - visibleMs);
    const auto showScrollBar = !m_clipRangeOnly && totalDurationMs > 0 && maxStartMs > 0;

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

void ClipWaveformQuickItem::invalidateWaveformCache()
{
    m_waveformCacheDirty = true;
}

void ClipWaveformQuickItem::ensureWaveformCache()
{
    const auto pixelWidth = std::max(1, static_cast<int>(std::ceil(width())));
    const auto pixelHeight = std::max(1, static_cast<int>(std::ceil(height())));
    if (m_waveformCache.size() != QSize(pixelWidth, pixelHeight))
    {
        m_waveformCache = QImage(QSize(pixelWidth, pixelHeight), QImage::Format_ARGB32_Premultiplied);
        m_waveformCacheDirty = true;
    }

    if (m_waveformCacheDirty)
    {
        rebuildWaveformCache();
    }
}

void ClipWaveformQuickItem::rebuildWaveformCache()
{
    if (m_waveformCache.isNull())
    {
        return;
    }

    const QColor cacheBackground = m_invertedColors ? QColor{214, 224, 235} : QColor{10, 13, 18};
    const QColor framePen = m_invertedColors ? QColor{172, 184, 197} : QColor{34, 40, 48};
    const QColor frameBrush = m_invertedColors ? QColor{228, 235, 243} : QColor{14, 18, 24};
    const QColor boundsFill = m_invertedColors ? QColor{220, 229, 238} : QColor{12, 16, 22};
    const QColor clipFill = m_invertedColors ? QColor{236, 242, 248} : QColor{24, 34, 46};
    const QColor channelGuide = m_invertedColors ? QColor{183, 194, 206} : QColor{28, 35, 44};
    const QColor activeWaveColor = m_invertedColors ? QColor{22, 31, 41} : QColor{202, 216, 234};
    const QColor inactiveWaveColor = m_invertedColors ? QColor{98, 109, 121} : QColor{92, 102, 114};
    const QColor emptyTextColor = m_invertedColors ? QColor{68, 78, 89} : QColor{126, 136, 148};

    m_waveformCache.fill(cacheBackground.rgba());

    QPainter painter(&m_waveformCache);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRectF bounds = waveformBounds();
    if (!bounds.isValid())
    {
        m_waveformCacheDirty = false;
        return;
    }

    painter.setPen(QPen(framePen, 1.0));
    painter.setBrush(frameBrush);
    painter.drawRect(bounds);

    if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
    {
        painter.setPen(emptyTextColor);
        painter.drawText(bounds, Qt::AlignCenter, QStringLiteral("Select a node with audio."));
        m_waveformCacheDirty = false;
        return;
    }

    const auto clipRect = m_clipRangeOnly ? bounds : selectionRect(bounds);
    painter.fillRect(bounds, boundsFill);
    painter.fillRect(clipRect, clipFill);

    const auto displayedChannelCount = std::clamp(m_waveformChannelCount, 1, 2);
    const auto channelRowHeight = bounds.height() / static_cast<double>(displayedChannelCount);
    painter.setPen(QPen(channelGuide, 1.0));
    for (int channel = 0; channel < displayedChannelCount; ++channel)
    {
        const auto rowTop = bounds.top() + static_cast<double>(channel) * channelRowHeight;
        const auto rowBottom = (channel + 1 == displayedChannelCount) ? bounds.bottom() : rowTop + channelRowHeight;
        const QRectF channelBounds{
            QPointF{bounds.left(), rowTop},
            QPointF{bounds.right(), rowBottom}};
        const auto rowCenterY = channelBounds.center().y();
        painter.drawLine(QPointF{bounds.left(), rowCenterY}, QPointF{bounds.right(), rowCenterY});
        if (displayedChannelCount > 1 && channel + 1 < displayedChannelCount)
        {
            painter.drawLine(QPointF{bounds.left(), rowBottom}, QPointF{bounds.right(), rowBottom});
        }
    }

    if (!m_peaks.empty())
    {
        const auto leftX = static_cast<int>(std::floor(bounds.left()));
        const auto rightX = static_cast<int>(std::ceil(bounds.right()));

        painter.setPen(Qt::NoPen);
        for (int pixelX = leftX; pixelX <= rightX; ++pixelX)
        {
            const auto pixelStartMs = static_cast<double>(displayRangeStartMs()) + m_viewStartMs
                + ((static_cast<double>(pixelX) - bounds.left()) / std::max(1.0, bounds.width()))
                    * visibleDurationMs();
            const auto pixelEndMs = static_cast<double>(displayRangeStartMs()) + m_viewStartMs
                + (((static_cast<double>(pixelX) + 1.0) - bounds.left()) / std::max(1.0, bounds.width()))
                    * visibleDurationMs();
            const auto peak = peakRangeBetweenMs(pixelStartMs, pixelEndMs);
            for (int channel = 0; channel < displayedChannelCount; ++channel)
            {
                const auto rowTop = bounds.top() + static_cast<double>(channel) * channelRowHeight;
                const auto rowBottom = (channel + 1 == displayedChannelCount) ? bounds.bottom() : rowTop + channelRowHeight;
                const QRectF channelBounds{
                    QPointF{bounds.left(), rowTop},
                    QPointF{bounds.right(), rowBottom}};
                const auto rowCenterY = channelBounds.center().y();
                const auto verticalRadius = std::max(2.0, channelBounds.height() * 0.45);
                const auto& channelPeak = peak.channels[static_cast<std::size_t>(channel)];
                const auto positiveHeight = std::min(
                    channelBounds.height() * 0.48,
                    static_cast<double>(channelPeak.positive) * verticalRadius * m_verticalZoom);
                const auto negativeHeight = std::min(
                    channelBounds.height() * 0.48,
                    static_cast<double>(std::abs(channelPeak.negative)) * verticalRadius * m_verticalZoom);
                const auto totalHeight = positiveHeight + negativeHeight;
                const auto topY = rowCenterY - (totalHeight > 0.001 ? positiveHeight : 1.0);
                const auto drawHeight = totalHeight > 0.001 ? totalHeight + 1.0 : 2.0;
                const auto active = clipRect.contains(QPointF{static_cast<double>(pixelX), rowCenterY});
                painter.setBrush(active ? activeWaveColor : inactiveWaveColor);
                painter.drawRect(
                    QRectF{
                        QPointF{static_cast<double>(pixelX), topY},
                        QSizeF{1.0, drawHeight}});
            }
        }
    }
    else
    {
        painter.setPen(emptyTextColor);
        painter.drawText(bounds, Qt::AlignCenter, QStringLiteral("Waveform unavailable."));
    }

    if (m_clipRangeHandlesVisible)
    {
        paintHandle(painter, bounds, clipStartX(bounds), QColor{224, 230, 236});
        paintHandle(painter, bounds, clipEndX(bounds), QColor{224, 230, 236});
    }
    m_waveformCacheDirty = false;
}
