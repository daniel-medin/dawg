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

class QQuickView;
class ClipEditorQuickController;
class ClipWaveformQuickItem;

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
    void handleStatusChanged();
    void syncWaveformItem();

    QQuickView* m_quickView = nullptr;
    QWidget* m_quickContainer = nullptr;
    ClipEditorQuickController* m_controller = nullptr;
    ClipWaveformQuickItem* m_waveformItem = nullptr;
    std::optional<ClipEditorState> m_state;
};
