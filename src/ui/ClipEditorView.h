#pragma once

#include <optional>

#include <QColor>
#include <QString>
#include <QUuid>
#include <QWidget>

struct ClipEditorState
{
    QUuid trackId;
    QString label;
    QColor color;
    QString assetPath;
    int nodeStartFrame = 0;
    int nodeEndFrame = 0;
    int clipStartMs = 0;
    int clipEndMs = 0;
    int sourceDurationMs = 0;
    std::optional<int> playheadMs;
    float gainDb = 0.0F;
    float level = 0.0F;
    bool hasAttachedAudio = false;
    bool loopEnabled = false;
};

class ClipEditorView final : public QWidget
{
    Q_OBJECT

public:
    explicit ClipEditorView(QWidget* parent = nullptr);

    void setState(const std::optional<ClipEditorState>& state);

signals:
    void clipRangeChanged(int clipStartMs, int clipEndMs);
    void playheadChanged(int playheadMs);
    void gainChanged(float gainDb);
    void loopSoundChanged(bool enabled);
    void attachAudioRequested();

private:
    class WaveformView;

    WaveformView* m_waveformView = nullptr;
    QWidget* m_editorContent = nullptr;
    QWidget* m_infoBar = nullptr;
    QWidget* m_emptyState = nullptr;
    QWidget* m_emptyStateCard = nullptr;
    class QLabel* m_titleLabel = nullptr;
    class QToolButton* m_loopButton = nullptr;
    class QProgressBar* m_levelMeter = nullptr;
    class QSlider* m_gainSlider = nullptr;
    class QScrollBar* m_waveformScrollBar = nullptr;
    class QLabel* m_sourceLabel = nullptr;
    class QLabel* m_rangeLabel = nullptr;
    class QLabel* m_durationLabel = nullptr;
    class QLabel* m_positionLabel = nullptr;
    class QLabel* m_gainLabel = nullptr;
    class QLabel* m_emptyTitleLabel = nullptr;
    class QLabel* m_emptyBodyLabel = nullptr;
    class QLabel* m_emptyActionLabel = nullptr;
};
