#include "ui/ClipEditorView.h"
#include "ui/QuickClipGainWidget.h"

#include <algorithm>
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
#include <QScrollBar>
#include <QSignalBlocker>
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

void setLabelText(QLabel* label, const QString& text)
{
    if (label && label->text() != text)
    {
        label->setText(text);
    }
}

bool sameWaveformState(
    const std::optional<ClipEditorState>& lhs,
    const std::optional<ClipEditorState>& rhs)
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
            notifyViewWindowChanged();
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
            0,
            std::max(0, m_state->sourceDurationMs - 1));
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

    m_infoBar = new QWidget(this);
    auto* infoLayout = new QHBoxLayout(m_infoBar);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(18);

    m_sourceLabel = new QLabel(this);
    m_rangeLabel = new QLabel(this);
    m_durationLabel = new QLabel(this);
    m_positionLabel = new QLabel(this);
    for (auto* label : {m_sourceLabel, m_rangeLabel, m_durationLabel, m_positionLabel})
    {
        label->setStyleSheet(QStringLiteral("color: #aeb8c4; font-size: 8.5pt;"));
        infoLayout->addWidget(label);
    }
    infoLayout->addStretch(1);
    rootLayout->addWidget(m_infoBar);

    m_editorContent = new QWidget(this);
    auto* editorLayout = new QHBoxLayout(m_editorContent);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(12);

    m_gainStrip = new QuickClipGainWidget(m_editorContent);
    m_gainStrip->setGainChangedHandler([this](const float gainDb)
    {
        emit gainChanged(gainDb);
    });
    editorLayout->addWidget(m_gainStrip, 0);

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
    m_emptyActionLabel = new QLabel(m_emptyStateCard);
    m_emptyActionLabel->setTextFormat(Qt::RichText);
    m_emptyActionLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_emptyActionLabel->setOpenExternalLinks(false);
    m_emptyActionLabel->setVisible(false);
    m_emptyActionLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #dfe7ef; font-size: 8.75pt; font-weight: 600; }"
        "QLabel a { color: #eef3f8; text-decoration: underline; }"
        "QLabel a:hover { color: #ffffff; }"));

    cardLayout->addWidget(m_emptyTitleLabel);
    cardLayout->addWidget(m_emptyBodyLabel);
    cardLayout->addWidget(m_emptyActionLabel);
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
    connect(m_loopButton, &QToolButton::toggled, this, &ClipEditorView::loopSoundChanged);
    connect(m_emptyActionLabel, &QLabel::linkActivated, this, [this](const QString&)
    {
        emit attachAudioRequested();
    });

    setState(std::nullopt);
}

void ClipEditorView::setState(const std::optional<ClipEditorState>& state)
{
    const auto hasSelection = state.has_value();
    const auto hasAttachedAudio = hasSelection && state->hasAttachedAudio;

    m_editorContent->setVisible(hasAttachedAudio);
    m_emptyState->setVisible(!hasAttachedAudio);
    if (m_infoBar)
    {
        m_infoBar->setVisible(hasAttachedAudio);
    }

    if (!hasSelection)
    {
        setLabelText(m_titleLabel, QStringLiteral("Clip Editor"));
        if (m_loopButton)
        {
            const QSignalBlocker blocker{m_loopButton};
            m_loopButton->setChecked(false);
            m_loopButton->setEnabled(false);
            m_loopButton->setVisible(false);
        }
        setLabelText(m_sourceLabel, QString{});
        setLabelText(m_rangeLabel, QString{});
        setLabelText(m_durationLabel, QString{});
        setLabelText(m_positionLabel, QString{});
        if (m_gainStrip)
        {
            m_gainStrip->setEnabled(false);
            m_gainStrip->setGainDb(0.0F);
            m_gainStrip->setMeterLevel(0.0F);
        }
        if (m_waveformScrollBar)
        {
            const QSignalBlocker blocker{m_waveformScrollBar};
            m_waveformScrollBar->setVisible(false);
            m_waveformScrollBar->setRange(0, 0);
            m_waveformScrollBar->setValue(0);
        }
        setLabelText(m_emptyTitleLabel, QStringLiteral("Select a node"));
        setLabelText(m_emptyBodyLabel, QStringLiteral("Select a node with attached audio to edit its clip."));
        if (m_emptyActionLabel)
        {
            setLabelText(m_emptyActionLabel, QString{});
            m_emptyActionLabel->setVisible(false);
        }
        m_waveformView->setState(std::nullopt);
        return;
    }

    setLabelText(m_titleLabel, state->label.isEmpty() ? QStringLiteral("Clip Editor") : state->label);
    if (m_loopButton)
    {
        const QSignalBlocker blocker{m_loopButton};
        m_loopButton->setEnabled(hasAttachedAudio);
        m_loopButton->setChecked(state->loopEnabled);
        m_loopButton->setVisible(hasAttachedAudio);
    }

    if (!hasAttachedAudio)
    {
        setLabelText(m_sourceLabel, QString{});
        setLabelText(m_rangeLabel, QString{});
        setLabelText(m_durationLabel, QString{});
        setLabelText(m_positionLabel, QString{});
        if (m_gainStrip)
        {
            m_gainStrip->setEnabled(false);
            m_gainStrip->setGainDb(0.0F);
            m_gainStrip->setMeterLevel(0.0F);
        }
        if (m_waveformScrollBar)
        {
            const QSignalBlocker blocker{m_waveformScrollBar};
            m_waveformScrollBar->setVisible(false);
            m_waveformScrollBar->setRange(0, 0);
            m_waveformScrollBar->setValue(0);
        }
        setLabelText(m_emptyTitleLabel, state->label.isEmpty() ? QStringLiteral("Node") : state->label);
        setLabelText(m_emptyBodyLabel, QStringLiteral("No audio attached"));
        if (m_emptyActionLabel)
        {
            setLabelText(m_emptyActionLabel, QStringLiteral("<a href=\"attach\">Attach Audio...</a>"));
            m_emptyActionLabel->setVisible(true);
        }
        m_waveformView->setState(std::nullopt);
        return;
    }

    if (m_emptyActionLabel)
    {
        setLabelText(m_emptyActionLabel, QString{});
        m_emptyActionLabel->setVisible(false);
    }

    setLabelText(m_sourceLabel, QStringLiteral("Source  %1").arg(QFileInfo(state->assetPath).fileName()));
    setLabelText(
        m_rangeLabel,
        QStringLiteral("In/Out  %1 - %2")
            .arg(formatTimeMs(state->clipStartMs))
            .arg(formatTimeMs(state->clipEndMs)));
    setLabelText(
        m_durationLabel,
        QStringLiteral("Clip  %1").arg(formatTimeMs(std::max(0, state->clipEndMs - state->clipStartMs))));
    setLabelText(
        m_positionLabel,
        state->playheadMs.has_value()
            ? QStringLiteral("Playhead  %1").arg(formatTimeMs(*state->playheadMs))
            : QStringLiteral("Playhead  --"));
    if (m_gainStrip)
    {
        m_gainStrip->setEnabled(true);
        m_gainStrip->setGainDb(state->gainDb);
        m_gainStrip->setMeterLevel(state->level);
    }
    m_waveformView->setState(state);
}
