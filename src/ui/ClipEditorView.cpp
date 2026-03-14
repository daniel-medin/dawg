#include "ui/ClipEditorView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QWheelEvent>
#include <QVBoxLayout>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace
{
QString formatTimeMs(const int timeMs)
{
    const auto safeMs = std::max(0, timeMs);
    const auto totalSeconds = safeMs / 1000;
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    const auto milliseconds = safeMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar{'0'})
        .arg(milliseconds / 10, 2, 10, QChar{'0'});
}

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

constexpr int kClipGainMinValue = -1000;
constexpr int kClipGainMaxValue = 120;
constexpr float kClipSilentGainDb = static_cast<float>(kClipGainMinValue) / 10.0F;
constexpr float kClipMeterFloorDb = kClipSilentGainDb;
constexpr float kClipMeterCeilingDb = static_cast<float>(kClipGainMaxValue) / 10.0F;
constexpr float kClipMeterYellowThresholdDb = -18.0F;
constexpr float kClipMeterOrangeThresholdDb = -6.0F;

float sliderValueToClipGainDb(const int sliderValue)
{
    return sliderValue <= kClipGainMinValue ? kClipSilentGainDb : static_cast<float>(sliderValue) / 10.0F;
}

int clipGainDbToSliderValue(const float gainDb)
{
    if (gainDb <= kClipSilentGainDb + 0.001F)
    {
        return kClipGainMinValue;
    }

    return static_cast<int>(std::lround(std::clamp(gainDb, kClipSilentGainDb, 12.0F) * 10.0F));
}

QString gainLabelText(const float gainDb)
{
    if (gainDb <= kClipSilentGainDb + 0.001F)
    {
        return QStringLiteral("Gain  -inf");
    }

    return QStringLiteral("Gain  %1 dB").arg(gainDb, 0, 'f', 1);
}

float meterLevelToDb(const float level)
{
    if (level <= 0.0F)
    {
        return kClipMeterFloorDb;
    }

    return std::clamp(20.0F * std::log10(level), kClipMeterFloorDb, 0.0F);
}

int meterValueForLevel(const float level)
{
    const auto meterDb = meterLevelToDb(level);
    const auto normalized = (meterDb - kClipMeterFloorDb) / (kClipMeterCeilingDb - kClipMeterFloorDb);
    return static_cast<int>(std::lround(std::clamp(normalized, 0.0F, 1.0F) * 1000.0F));
}

QString meterStyleSheet()
{
    const auto yellowStop =
        (kClipMeterYellowThresholdDb - kClipMeterFloorDb) / (kClipMeterCeilingDb - kClipMeterFloorDb);
    const auto orangeStop =
        (kClipMeterOrangeThresholdDb - kClipMeterFloorDb) / (kClipMeterCeilingDb - kClipMeterFloorDb);

    return QStringLiteral(
        "QProgressBar {"
        "  background: #0b1016;"
        "  border: 1px solid #1d2733;"
        "  border-radius: 4px;"
        "}"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:1, x2:0, y2:0,"
        "    stop:0 #2fe06d,"
        "    stop:%1 #2fe06d,"
        "    stop:%2 #ffb33b,"
        "    stop:1 #ff5b4d);"
        "  border-radius: 3px;"
        "}")
        .arg(yellowStop, 0, 'f', 3)
        .arg(orangeStop, 0, 'f', 3);
}

QString gainSliderStyleSheet()
{
    return QStringLiteral(
        "QSlider::groove:vertical { background: #1a212b; width: 6px; border-radius: 3px; }"
        "QSlider::sub-page:vertical { background: #11161d; border-radius: 3px; }"
        "QSlider::add-page:vertical { background: #2d3948; border-radius: 3px; }"
        "QSlider::handle:vertical { background: #f2f5f8; border: 1px solid #3e4d61; height: 16px; margin: -3px -7px; border-radius: 7px; }");
}

class ClipGainSlider final : public QSlider
{
public:
    explicit ClipGainSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Vertical, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))
        {
            setValue(0);
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }
};

class ClipGainScaleWidget final : public QWidget
{
public:
    explicit ClipGainScaleWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(28);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        static const std::array<std::pair<float, QString>, 7> markers{{
            {12.0F, QStringLiteral("+12")},
            {0.0F, QStringLiteral("0")},
            {-12.0F, QStringLiteral("-12")},
            {-24.0F, QStringLiteral("-24")},
            {-48.0F, QStringLiteral("-48")},
            {-72.0F, QStringLiteral("-72")},
            {kClipSilentGainDb, QStringLiteral("-inf")}
        }};

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(QColor{124, 137, 152});
        auto font = painter.font();
        font.setPointSizeF(7.0);
        painter.setFont(font);

        const auto bounds = rect().adjusted(0, 8, 0, -8);
        for (const auto& [gainDb, label] : markers)
        {
            const auto normalized = gainDb <= kClipSilentGainDb + 0.001F
                ? 1.0
                : 1.0 - ((std::clamp(gainDb, kClipSilentGainDb, 12.0F) - kClipSilentGainDb)
                    / (12.0F - kClipSilentGainDb));
            const auto y = bounds.top() + (normalized * bounds.height());
            painter.drawText(
                QRectF{0.0, y - 8.0, static_cast<double>(width()), 16.0},
                Qt::AlignLeft | Qt::AlignVCenter,
                label);
        }
    }
};
}

class ClipEditorView::WaveformView final : public QWidget
{
public:
    explicit WaveformView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(128);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setRangeChangedHandler(std::function<void(int, int)> handler)
    {
        m_rangeChangedHandler = std::move(handler);
    }

    void setPlayheadChangedHandler(std::function<void(int)> handler)
    {
        m_playheadChangedHandler = std::move(handler);
    }

    void setViewWindowChangedHandler(std::function<void(int, int, int)> handler)
    {
        m_viewWindowChangedHandler = std::move(handler);
    }

    void setViewStartMs(const int viewStartMs)
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

        notifyViewWindowChanged();
        update();
    }

    void setState(const std::optional<ClipEditorState>& state)
    {
        if (!state.has_value() || !state->hasAttachedAudio)
        {
            m_state.reset();
            m_loadedAssetPath.clear();
            m_peaks.clear();
            m_dragMode = DragMode::None;
            m_horizontalZoom = 1.0;
            m_verticalZoom = 1.0;
            m_viewStartMs = 0.0;
            notifyViewWindowChanged();
            update();
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
        notifyViewWindowChanged();
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor{10, 13, 18});

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
        if (!bounds.isValid())
        {
            return;
        }

        painter.setPen(QPen(QColor{34, 40, 48}, 1.0));
        painter.setBrush(QColor{14, 18, 24});
        painter.drawRect(bounds);

        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            painter.setPen(QColor{126, 136, 148});
            painter.drawText(bounds, Qt::AlignCenter, QStringLiteral("Select a node with audio."));
            return;
        }

        const auto clipRect = selectionRect(bounds);
        painter.fillRect(bounds, QColor{12, 16, 22});
        painter.fillRect(clipRect, QColor{24, 34, 46});

        const auto centerY = bounds.center().y();
        painter.setPen(QPen(QColor{28, 35, 44}, 1.0));
        painter.drawLine(QPointF{bounds.left(), centerY}, QPointF{bounds.right(), centerY});

        if (!m_peaks.empty())
        {
            const auto verticalRadius = std::max(10.0, bounds.height() * 0.45);
            const auto leftX = static_cast<int>(std::floor(bounds.left()));
            const auto rightX = static_cast<int>(std::ceil(bounds.right()));

            painter.setPen(Qt::NoPen);
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
                painter.setBrush(active ? QColor{202, 216, 234} : QColor{92, 102, 114});
                painter.drawRect(
                    QRectF{
                        QPointF{static_cast<double>(pixelX), centerY - halfHeight},
                        QSizeF{1.0, (halfHeight * 2.0) + 1.0}});
            }
        }
        else
        {
            painter.setPen(QColor{126, 136, 148});
            painter.drawText(bounds, Qt::AlignCenter, QStringLiteral("Waveform unavailable."));
        }

        paintHandle(painter, bounds, clipStartX(bounds), QColor{224, 230, 236});
        paintHandle(painter, bounds, clipEndX(bounds), QColor{224, 230, 236});
        paintPlayhead(painter, bounds, QColor{241, 196, 86});
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QWidget::mousePressEvent(event);
        setFocus(Qt::MouseFocusReason);

        if (event->button() != Qt::LeftButton || !m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return;
        }

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
        const auto x = event->position().x();
        constexpr double handleHitRadius = 8.0;

        if (std::abs(x - clipStartX(bounds)) <= handleHitRadius)
        {
            m_dragMode = DragMode::Start;
            applyDragAt(event->position());
            return;
        }

        if (std::abs(x - clipEndX(bounds)) <= handleHitRadius)
        {
            m_dragMode = DragMode::End;
            applyDragAt(event->position());
            return;
        }

        m_dragMode = DragMode::Playhead;
        applyPlayheadAt(event->position());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QWidget::mouseMoveEvent(event);

        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            unsetCursor();
            return;
        }

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
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
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QWidget::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton)
        {
            m_dragMode = DragMode::None;
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            QWidget::wheelEvent(event);
            return;
        }

        const auto deltaY = event->angleDelta().y();
        if (deltaY == 0)
        {
            QWidget::wheelEvent(event);
            return;
        }

        constexpr double zoomStepFactor = 1.18;
        const auto steps = static_cast<double>(deltaY) / 120.0;
        if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            m_verticalZoom = std::clamp(
                m_verticalZoom * std::pow(zoomStepFactor, steps),
                0.5,
                8.0);
            update();
            event->accept();
            return;
        }

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
        if (!bounds.isValid())
        {
            QWidget::wheelEvent(event);
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
        notifyViewWindowChanged();
        update();
        event->accept();
    }

private:
    enum class DragMode
    {
        None,
        Start,
        End,
        Playhead
    };

    void loadWaveform(const QString& filePath)
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

    [[nodiscard]] QRectF selectionRect(const QRectF& bounds) const
    {
        const auto left = std::min(clipStartX(bounds), clipEndX(bounds));
        const auto right = std::max(clipStartX(bounds), clipEndX(bounds));
        return QRectF{
            QPointF{left, bounds.top()},
            QPointF{right, bounds.bottom()}
        }.intersected(bounds);
    }

    [[nodiscard]] double clipStartX(const QRectF& bounds) const
    {
        return xForMs(bounds, m_state.has_value() ? m_state->clipStartMs : 0);
    }

    [[nodiscard]] double clipEndX(const QRectF& bounds) const
    {
        return xForMs(bounds, m_state.has_value() ? m_state->clipEndMs : 0);
    }

    [[nodiscard]] double playheadX(const QRectF& bounds) const
    {
        if (!m_state.has_value() || !m_state->playheadMs.has_value())
        {
            return clipStartX(bounds);
        }

        return xForMs(bounds, *m_state->playheadMs);
    }

    [[nodiscard]] double xForMs(const QRectF& bounds, const int timeMs) const
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

    [[nodiscard]] int msForX(const QRectF& bounds, const double x) const
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return 0;
        }

        const auto ratio = std::clamp((x - bounds.left()) / std::max(1.0, bounds.width()), 0.0, 1.0);
        return static_cast<int>(std::lround(m_viewStartMs + ratio * visibleDurationMs()));
    }

    [[nodiscard]] double interpolatedPeakAtMs(const double timeMs) const
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

    void paintHandle(QPainter& painter, const QRectF& bounds, const double x, const QColor& color) const
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

    void paintPlayhead(QPainter& painter, const QRectF& bounds, const QColor& color) const
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

    void applyDragAt(const QPointF& position)
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return;
        }

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
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
        if (m_rangeChangedHandler)
        {
            m_rangeChangedHandler(clipStartMs, clipEndMs);
        }
        update();
    }

    void applyPlayheadAt(const QPointF& position)
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return;
        }

        const QRectF bounds = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
        const auto nextPlayheadMs = std::clamp(
            msForX(bounds, position.x()),
            m_state->clipStartMs,
            std::max(m_state->clipStartMs, m_state->clipEndMs - 1));
        if (m_state->playheadMs.has_value() && *m_state->playheadMs == nextPlayheadMs)
        {
            return;
        }

        m_state->playheadMs = nextPlayheadMs;
        if (m_playheadChangedHandler)
        {
            m_playheadChangedHandler(nextPlayheadMs);
        }
        update();
    }

    [[nodiscard]] double visibleDurationMs() const
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return 1.0;
        }

        return std::max(12.0, static_cast<double>(m_state->sourceDurationMs) / std::max(1.0, m_horizontalZoom));
    }

    [[nodiscard]] double viewEndMs() const
    {
        return m_viewStartMs + visibleDurationMs();
    }

    [[nodiscard]] double maxHorizontalZoom() const
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return 1.0;
        }

        return std::max(1.0, static_cast<double>(m_state->sourceDurationMs) / 12.0);
    }

    void clampViewWindow()
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            m_viewStartMs = 0.0;
            return;
        }

        const auto maxStartMs = std::max(0.0, static_cast<double>(m_state->sourceDurationMs) - visibleDurationMs());
        m_viewStartMs = std::clamp(m_viewStartMs, 0.0, maxStartMs);
    }

    void notifyViewWindowChanged() const
    {
        if (!m_viewWindowChangedHandler)
        {
            return;
        }

        const auto totalDurationMs = (m_state.has_value() && m_state->sourceDurationMs > 0)
            ? m_state->sourceDurationMs
            : 0;
        const auto visibleMs = totalDurationMs > 0
            ? static_cast<int>(std::lround(visibleDurationMs()))
            : 0;
        const auto viewStartMs = totalDurationMs > 0
            ? static_cast<int>(std::lround(m_viewStartMs))
            : 0;
        m_viewWindowChangedHandler(viewStartMs, visibleMs, totalDurationMs);
    }

    std::optional<ClipEditorState> m_state;
    QString m_loadedAssetPath;
    std::vector<float> m_peaks;
    DragMode m_dragMode = DragMode::None;
    std::function<void(int, int)> m_rangeChangedHandler;
    std::function<void(int)> m_playheadChangedHandler;
    std::function<void(int, int, int)> m_viewWindowChangedHandler;
    double m_horizontalZoom = 1.0;
    double m_verticalZoom = 1.0;
    double m_viewStartMs = 0.0;
};

ClipEditorView::ClipEditorView(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 12);
    rootLayout->setSpacing(10);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("Clip Editor"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #f1f4f7; font-size: 11pt; font-weight: 600;"));

    m_loopButton = new QToolButton(this);
    m_loopButton->setText(QStringLiteral("Loop Sound"));
    m_loopButton->setCheckable(true);
    m_loopButton->setCursor(Qt::PointingHandCursor);
    m_loopButton->setFixedHeight(26);
    m_loopButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: #141a21;"
        "  color: #d5dce4;"
        "  border: 1px solid #2a3541;"
        "  border-radius: 6px;"
        "  padding: 0 10px;"
        "}"
        "QToolButton:hover { background: #1a232d; }"
        "QToolButton:checked {"
        "  background: #2b4d31;"
        "  color: #eef7f0;"
        "  border: 1px solid #4f8a58;"
        "}"));

    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(m_loopButton, 0, Qt::AlignVCenter);
    rootLayout->addLayout(headerLayout);

    auto* infoLayout = new QHBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(18);

    m_sourceLabel = new QLabel(this);
    m_rangeLabel = new QLabel(this);
    m_durationLabel = new QLabel(this);
    m_positionLabel = new QLabel(this);
    m_gainLabel = new QLabel(this);
    for (auto* label : {m_sourceLabel, m_rangeLabel, m_durationLabel, m_positionLabel, m_gainLabel})
    {
        label->setStyleSheet(QStringLiteral("color: #aeb8c4; font-size: 8.5pt;"));
        infoLayout->addWidget(label);
    }
    infoLayout->addStretch(1);
    rootLayout->addLayout(infoLayout);

    m_editorContent = new QWidget(this);
    auto* editorLayout = new QHBoxLayout(m_editorContent);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(12);

    auto* stripWidget = new QFrame(m_editorContent);
    stripWidget->setObjectName(QStringLiteral("clipEditorStrip"));
    stripWidget->setFixedWidth(92);
    stripWidget->setStyleSheet(QStringLiteral(
        "QFrame#clipEditorStrip { background: #0f141b; border: 1px solid #1b2430; border-radius: 8px; }"));
    auto* stripLayout = new QHBoxLayout(stripWidget);
    stripLayout->setContentsMargins(8, 12, 8, 12);
    stripLayout->setSpacing(6);

    m_levelMeter = new QProgressBar(stripWidget);
    m_levelMeter->setRange(0, 1000);
    m_levelMeter->setValue(0);
    m_levelMeter->setTextVisible(false);
    m_levelMeter->setOrientation(Qt::Vertical);
    m_levelMeter->setFixedWidth(12);
    m_levelMeter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_levelMeter->setStyleSheet(meterStyleSheet());
    stripLayout->addWidget(m_levelMeter, 0);

    m_gainSlider = new ClipGainSlider(stripWidget);
    m_gainSlider->setRange(kClipGainMinValue, kClipGainMaxValue);
    m_gainSlider->setValue(0);
    m_gainSlider->setTickInterval(60);
    m_gainSlider->setTickPosition(QSlider::TicksLeft);
    m_gainSlider->setMinimumHeight(128);
    m_gainSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_gainSlider->setStyleSheet(gainSliderStyleSheet());
    stripLayout->addWidget(m_gainSlider, 0);

    auto* gainScale = new ClipGainScaleWidget(stripWidget);
    gainScale->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    stripLayout->addWidget(gainScale, 0);

    editorLayout->addWidget(stripWidget, 0);

    auto* waveformColumn = new QWidget(m_editorContent);
    auto* waveformColumnLayout = new QVBoxLayout(waveformColumn);
    waveformColumnLayout->setContentsMargins(0, 0, 0, 0);
    waveformColumnLayout->setSpacing(6);

    m_waveformView = new WaveformView(waveformColumn);
    waveformColumnLayout->addWidget(m_waveformView, 1);

    m_waveformScrollBar = new QScrollBar(Qt::Horizontal, waveformColumn);
    m_waveformScrollBar->setFixedHeight(12);
    m_waveformScrollBar->setVisible(false);
    m_waveformScrollBar->setCursor(Qt::PointingHandCursor);
    m_waveformScrollBar->setStyleSheet(QStringLiteral(
        "QScrollBar:horizontal {"
        "  background: #0d1218;"
        "  height: 12px;"
        "  border: 1px solid #1c2631;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #334252;"
        "  min-width: 24px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #44576b; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; height: 0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"));
    waveformColumnLayout->addWidget(m_waveformScrollBar, 0);

    editorLayout->addWidget(waveformColumn, 1);
    rootLayout->addWidget(m_editorContent, 1);

    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 8, 0, 0);
    emptyLayout->setSpacing(6);

    m_emptyStateCard = new QWidget(m_emptyState);
    m_emptyStateCard->setStyleSheet(QStringLiteral(
        "QWidget { background: #12171d; border: 1px solid #232c35; border-radius: 8px; }"));
    auto* cardLayout = new QVBoxLayout(m_emptyStateCard);
    cardLayout->setContentsMargins(14, 14, 14, 14);
    cardLayout->setSpacing(4);

    m_emptyTitleLabel = new QLabel(QStringLiteral("Select a node"), m_emptyStateCard);
    m_emptyTitleLabel->setStyleSheet(QStringLiteral("color: #eef2f6; font-size: 10pt; font-weight: 600;"));
    m_emptyBodyLabel = new QLabel(QStringLiteral("Select a node with attached audio to edit its clip."), m_emptyStateCard);
    m_emptyBodyLabel->setWordWrap(true);
    m_emptyBodyLabel->setStyleSheet(QStringLiteral("color: #9ca8b6; font-size: 8.75pt;"));

    cardLayout->addWidget(m_emptyTitleLabel);
    cardLayout->addWidget(m_emptyBodyLabel);
    emptyLayout->addWidget(m_emptyStateCard);
    emptyLayout->addStretch(1);
    rootLayout->addWidget(m_emptyState, 1);

    m_waveformView->setRangeChangedHandler([this](const int clipStartMs, const int clipEndMs)
    {
        emit clipRangeChanged(clipStartMs, clipEndMs);
    });
    m_waveformView->setPlayheadChangedHandler([this](const int playheadMs)
    {
        emit playheadChanged(playheadMs);
    });
    m_waveformView->setViewWindowChangedHandler([this](const int viewStartMs, const int visibleDurationMs, const int totalDurationMs)
    {
        if (!m_waveformScrollBar)
        {
            return;
        }

        const auto maxStartMs = std::max(0, totalDurationMs - visibleDurationMs);
        const auto showScrollBar = totalDurationMs > 0 && maxStartMs > 0;
        m_waveformScrollBar->setVisible(showScrollBar);
        const QSignalBlocker blocker{m_waveformScrollBar};
        m_waveformScrollBar->setEnabled(showScrollBar);
        m_waveformScrollBar->setRange(0, maxStartMs);
        m_waveformScrollBar->setPageStep(std::max(1, visibleDurationMs));
        m_waveformScrollBar->setSingleStep(std::max(1, visibleDurationMs / 20));
        m_waveformScrollBar->setValue(std::clamp(viewStartMs, 0, maxStartMs));
    });
    connect(m_waveformScrollBar, &QScrollBar::valueChanged, this, [this](const int value)
    {
        if (m_waveformView)
        {
            m_waveformView->setViewStartMs(value);
        }
    });
    connect(m_gainSlider, &QSlider::valueChanged, this, [this](const int value)
    {
        if (m_gainLabel)
        {
            m_gainLabel->setText(gainLabelText(sliderValueToClipGainDb(value)));
        }
        emit gainChanged(sliderValueToClipGainDb(value));
    });
    connect(m_loopButton, &QToolButton::toggled, this, &ClipEditorView::loopSoundChanged);

    setState(std::nullopt);
}

void ClipEditorView::setState(const std::optional<ClipEditorState>& state)
{
    const auto hasSelection = state.has_value();
    const auto hasAttachedAudio = hasSelection && state->hasAttachedAudio;

    m_editorContent->setVisible(hasAttachedAudio);
    m_emptyState->setVisible(!hasAttachedAudio);

    if (!hasSelection)
    {
        m_titleLabel->setText(QStringLiteral("Clip Editor"));
        if (m_loopButton)
        {
            const QSignalBlocker blocker{m_loopButton};
            m_loopButton->setChecked(false);
            m_loopButton->setEnabled(false);
        }
        m_sourceLabel->clear();
        m_rangeLabel->clear();
        m_durationLabel->clear();
        m_positionLabel->clear();
        m_gainLabel->clear();
        if (m_gainSlider)
        {
            const QSignalBlocker blocker{m_gainSlider};
            m_gainSlider->setEnabled(false);
            m_gainSlider->setValue(clipGainDbToSliderValue(0.0F));
        }
        if (m_levelMeter)
        {
            m_levelMeter->setValue(0);
        }
        if (m_waveformScrollBar)
        {
            const QSignalBlocker blocker{m_waveformScrollBar};
            m_waveformScrollBar->setVisible(false);
            m_waveformScrollBar->setRange(0, 0);
            m_waveformScrollBar->setValue(0);
        }
        m_emptyTitleLabel->setText(QStringLiteral("Select a node"));
        m_emptyBodyLabel->setText(QStringLiteral("Select a node with attached audio to edit its clip."));
        m_waveformView->setState(std::nullopt);
        return;
    }

    m_titleLabel->setText(state->label.isEmpty() ? QStringLiteral("Clip Editor") : state->label);
    if (m_loopButton)
    {
        const QSignalBlocker blocker{m_loopButton};
        m_loopButton->setEnabled(hasAttachedAudio);
        m_loopButton->setChecked(state->loopEnabled);
    }

    if (!hasAttachedAudio)
    {
        m_sourceLabel->setText(QStringLiteral("No audio attached"));
        m_rangeLabel->clear();
        m_durationLabel->clear();
        m_positionLabel->clear();
        m_gainLabel->clear();
        if (m_gainSlider)
        {
            const QSignalBlocker blocker{m_gainSlider};
            m_gainSlider->setEnabled(false);
            m_gainSlider->setValue(clipGainDbToSliderValue(0.0F));
        }
        if (m_levelMeter)
        {
            m_levelMeter->setValue(0);
        }
        if (m_waveformScrollBar)
        {
            const QSignalBlocker blocker{m_waveformScrollBar};
            m_waveformScrollBar->setVisible(false);
            m_waveformScrollBar->setRange(0, 0);
            m_waveformScrollBar->setValue(0);
        }
        m_emptyTitleLabel->setText(state->label.isEmpty() ? QStringLiteral("No audio attached") : state->label);
        m_emptyBodyLabel->setText(QStringLiteral("Import or drag audio onto this node to edit its waveform."));
        m_waveformView->setState(std::nullopt);
        return;
    }

    m_sourceLabel->setText(QStringLiteral("Source  %1").arg(QFileInfo(state->assetPath).fileName()));
    m_rangeLabel->setText(
        QStringLiteral("In/Out  %1 - %2")
            .arg(formatTimeMs(state->clipStartMs))
            .arg(formatTimeMs(state->clipEndMs)));
    m_durationLabel->setText(
        QStringLiteral("Clip  %1").arg(formatTimeMs(std::max(0, state->clipEndMs - state->clipStartMs))));
    m_positionLabel->setText(
        state->playheadMs.has_value()
            ? QStringLiteral("Playhead  %1").arg(formatTimeMs(*state->playheadMs))
            : QStringLiteral("Playhead  --"));
    m_gainLabel->setText(gainLabelText(state->gainDb));
    if (m_gainSlider)
    {
        const QSignalBlocker blocker{m_gainSlider};
        m_gainSlider->setEnabled(true);
        m_gainSlider->setValue(clipGainDbToSliderValue(state->gainDb));
    }
    if (m_levelMeter)
    {
        m_levelMeter->setValue(meterValueForLevel(state->level));
    }
    m_waveformView->setState(state);
}
