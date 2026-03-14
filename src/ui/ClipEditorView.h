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
    bool hasAttachedAudio = false;
};

class ClipEditorView final : public QWidget
{
    Q_OBJECT

public:
    explicit ClipEditorView(QWidget* parent = nullptr);

    void setState(const std::optional<ClipEditorState>& state);

signals:
    void playRequested();
    void stopRequested();
    void clipRangeChanged(int clipStartMs, int clipEndMs);

private:
    class WaveformView;

    WaveformView* m_waveformView = nullptr;
    QWidget* m_editorContent = nullptr;
    QWidget* m_emptyState = nullptr;
    QWidget* m_emptyStateCard = nullptr;
    class QLabel* m_titleLabel = nullptr;
    class QLabel* m_sourceLabel = nullptr;
    class QLabel* m_rangeLabel = nullptr;
    class QLabel* m_durationLabel = nullptr;
    class QLabel* m_positionLabel = nullptr;
    class QLabel* m_emptyTitleLabel = nullptr;
    class QLabel* m_emptyBodyLabel = nullptr;
};
