#include "ui/ClipEditorView.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
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
}

class ClipEditorView::WaveformView final : public QWidget
{
public:
    explicit WaveformView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(128);
        setMouseTracking(true);
    }

    void setRangeChangedHandler(std::function<void(int, int)> handler)
    {
        m_rangeChangedHandler = std::move(handler);
    }

    void setState(const std::optional<ClipEditorState>& state)
    {
        if (!state.has_value() || !state->hasAttachedAudio)
        {
            m_state.reset();
            m_loadedAssetPath.clear();
            m_peaks.clear();
            m_dragMode = DragMode::None;
            update();
            return;
        }

        const auto assetChanged = m_loadedAssetPath != state->assetPath;
        m_state = state;
        if (assetChanged)
        {
            m_loadedAssetPath = state->assetPath;
            loadWaveform(state->assetPath);
        }
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
            const auto sampleCount = static_cast<int>(m_peaks.size());
            const auto verticalRadius = std::max(10.0, bounds.height() * 0.45);

            for (int index = 0; index < sampleCount; ++index)
            {
                const auto x = bounds.left() + (static_cast<double>(index) / std::max(1, sampleCount - 1)) * bounds.width();
                const auto amplitude = std::clamp(static_cast<double>(m_peaks[static_cast<std::size_t>(index)]), 0.0, 1.0);
                const auto halfHeight = std::max(1.0, amplitude * verticalRadius);
                const auto active = clipRect.contains(QPointF{x, centerY});
                painter.setPen(QPen(active ? QColor{202, 216, 234} : QColor{92, 102, 114}, 1.0));
                painter.drawLine(
                    QPointF{x, centerY - halfHeight},
                    QPointF{x, centerY + halfHeight});
            }
        }
        else
        {
            painter.setPen(QColor{126, 136, 148});
            painter.drawText(bounds, Qt::AlignCenter, QStringLiteral("Waveform unavailable."));
        }

        paintHandle(painter, bounds, clipStartX(bounds), QColor{224, 230, 236});
        paintHandle(painter, bounds, clipEndX(bounds), QColor{224, 230, 236});

        if (m_state->playheadMs.has_value())
        {
            const auto playheadRatio =
                static_cast<double>(std::clamp(*m_state->playheadMs, 0, m_state->sourceDurationMs))
                / std::max(1, m_state->sourceDurationMs);
            const auto x = bounds.left() + playheadRatio * bounds.width();
            painter.setPen(QPen(QColor{214, 220, 228, 156}, 1.0));
            painter.drawLine(QPointF{x, bounds.top()}, QPointF{x, bounds.bottom()});
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QWidget::mousePressEvent(event);

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
        }
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
            applyDragAt(event->position());
            return;
        }

        if (std::abs(x - clipStartX(bounds)) <= handleHitRadius
            || std::abs(x - clipEndX(bounds)) <= handleHitRadius)
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

private:
    enum class DragMode
    {
        None,
        Start,
        End
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
        return QRectF{
            clipStartX(bounds),
            bounds.top(),
            std::max(2.0, clipEndX(bounds) - clipStartX(bounds)),
            bounds.height()
        };
    }

    [[nodiscard]] double clipStartX(const QRectF& bounds) const
    {
        return xForMs(bounds, m_state.has_value() ? m_state->clipStartMs : 0);
    }

    [[nodiscard]] double clipEndX(const QRectF& bounds) const
    {
        return xForMs(bounds, m_state.has_value() ? m_state->clipEndMs : 0);
    }

    [[nodiscard]] double xForMs(const QRectF& bounds, const int timeMs) const
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return bounds.left();
        }

        const auto ratio =
            static_cast<double>(std::clamp(timeMs, 0, m_state->sourceDurationMs))
            / std::max(1, m_state->sourceDurationMs);
        return bounds.left() + ratio * bounds.width();
    }

    [[nodiscard]] int msForX(const QRectF& bounds, const double x) const
    {
        if (!m_state.has_value() || m_state->sourceDurationMs <= 0)
        {
            return 0;
        }

        const auto ratio = std::clamp((x - bounds.left()) / std::max(1.0, bounds.width()), 0.0, 1.0);
        return static_cast<int>(std::lround(ratio * m_state->sourceDurationMs));
    }

    void paintHandle(QPainter& painter, const QRectF& bounds, const double x, const QColor& color) const
    {
        painter.setPen(QPen(color, 2.0));
        painter.drawLine(QPointF{x, bounds.top()}, QPointF{x, bounds.bottom()});
        painter.drawLine(QPointF{x - 4.0, bounds.top() + 7.0}, QPointF{x + 4.0, bounds.top() + 7.0});
        painter.drawLine(QPointF{x - 4.0, bounds.bottom() - 7.0}, QPointF{x + 4.0, bounds.bottom() - 7.0});
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

    std::optional<ClipEditorState> m_state;
    QString m_loadedAssetPath;
    std::vector<float> m_peaks;
    DragMode m_dragMode = DragMode::None;
    std::function<void(int, int)> m_rangeChangedHandler;
};

ClipEditorView::ClipEditorView(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 12);
    rootLayout->setSpacing(10);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("Clip Editor"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #f1f4f7; font-size: 11pt; font-weight: 600;"));

    auto* playButton = new QPushButton(QStringLiteral("Play"), this);
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setFixedHeight(28);
    playButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1f2c37; color: #f3f5f7; border: 1px solid #314251; border-radius: 6px; padding: 0 12px; }"
        "QPushButton:hover { background: #253441; }"
        "QPushButton:pressed { background: #18232c; }"));

    auto* stopButton = new QPushButton(QStringLiteral("Stop"), this);
    stopButton->setCursor(Qt::PointingHandCursor);
    stopButton->setFixedHeight(28);
    stopButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #131920; color: #d9e0e8; border: 1px solid #2b3642; border-radius: 6px; padding: 0 12px; }"
        "QPushButton:hover { background: #182029; }"
        "QPushButton:pressed { background: #0e141a; }"));

    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(playButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(stopButton, 0, Qt::AlignVCenter);
    rootLayout->addLayout(headerLayout);

    auto* infoLayout = new QHBoxLayout();
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
    rootLayout->addLayout(infoLayout);

    m_editorContent = new QWidget(this);
    auto* editorLayout = new QVBoxLayout(m_editorContent);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_waveformView = new WaveformView(m_editorContent);
    editorLayout->addWidget(m_waveformView, 1);
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

    connect(playButton, &QPushButton::clicked, this, &ClipEditorView::playRequested);
    connect(stopButton, &QPushButton::clicked, this, &ClipEditorView::stopRequested);
    m_waveformView->setRangeChangedHandler([this](const int clipStartMs, const int clipEndMs)
    {
        emit clipRangeChanged(clipStartMs, clipEndMs);
    });

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
        m_sourceLabel->clear();
        m_rangeLabel->clear();
        m_durationLabel->clear();
        m_positionLabel->clear();
        m_emptyTitleLabel->setText(QStringLiteral("Select a node"));
        m_emptyBodyLabel->setText(QStringLiteral("Select a node with attached audio to edit its clip."));
        m_waveformView->setState(std::nullopt);
        return;
    }

    m_titleLabel->setText(state->label.isEmpty() ? QStringLiteral("Clip Editor") : state->label);

    if (!hasAttachedAudio)
    {
        m_sourceLabel->setText(QStringLiteral("No audio attached"));
        m_rangeLabel->clear();
        m_durationLabel->clear();
        m_positionLabel->clear();
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
    m_waveformView->setState(state);
}
