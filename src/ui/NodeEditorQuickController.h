#pragma once

#include <optional>

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

#include "ui/AudioClipPreviewTypes.h"

class NodeEditorQuickController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canOpenNode READ canOpenNode NOTIFY stateChanged)
    Q_PROPERTY(bool canSaveNode READ canSaveNode NOTIFY stateChanged)
    Q_PROPERTY(bool canSaveNodeAs READ canSaveNodeAs NOTIFY stateChanged)
    Q_PROPERTY(bool canExportNode READ canExportNode NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(QString selectedNodeName READ selectedNodeName NOTIFY stateChanged)
    Q_PROPERTY(QString nodeContainerText READ nodeContainerText NOTIFY stateChanged)
    Q_PROPERTY(bool hasAttachedAudio READ hasAttachedAudio NOTIFY stateChanged)
    Q_PROPERTY(int nodeTrackCount READ nodeTrackCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList nodeTracks READ nodeTracks NOTIFY stateChanged)
    Q_PROPERTY(QString selectedLaneId READ selectedLaneId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedLaneHeaderId READ selectedLaneHeaderId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedClipId READ selectedClipId NOTIFY stateChanged)
    Q_PROPERTY(QString audioSummaryText READ audioSummaryText NOTIFY stateChanged)
    Q_PROPERTY(QString emptyBodyText READ emptyBodyText NOTIFY stateChanged)
    Q_PROPERTY(bool showTimeline READ showTimeline NOTIFY stateChanged)
    Q_PROPERTY(QString timelineStartText READ timelineStartText NOTIFY stateChanged)
    Q_PROPERTY(QString timelineEndText READ timelineEndText NOTIFY stateChanged)
    Q_PROPERTY(QString timelineDurationText READ timelineDurationText NOTIFY stateChanged)
    Q_PROPERTY(QString playheadText READ playheadText NOTIFY playheadPositionChanged)
    Q_PROPERTY(int nodeDurationMs READ nodeDurationMs NOTIFY stateChanged)
    Q_PROPERTY(int playheadMs READ playheadMs NOTIFY playheadPositionChanged)
    Q_PROPERTY(qreal playheadRatio READ playheadRatio NOTIFY playheadPositionChanged)
    Q_PROPERTY(int insertionMarkerMs READ insertionMarkerMs NOTIFY playheadPositionChanged)
    Q_PROPERTY(qreal insertionMarkerRatio READ insertionMarkerRatio NOTIFY playheadPositionChanged)
    Q_PROPERTY(bool insertionMarkerStationary READ insertionMarkerStationary NOTIFY playbackStateChanged)
    Q_PROPERTY(bool insertionFollowsPlayback READ insertionFollowsPlayback NOTIFY stateChanged)
    Q_PROPERTY(bool playbackActive READ playbackActive NOTIFY playbackStateChanged)
    Q_PROPERTY(int laneMeterToken READ laneMeterToken NOTIFY laneMeterLevelsChanged)
    Q_PROPERTY(int meterResetToken READ meterResetToken NOTIFY meterResetTokenChanged)
    Q_PROPERTY(qreal waveformWidthRatio READ waveformWidthRatio NOTIFY stateChanged)
    Q_PROPERTY(qreal waveformOffsetRatio READ waveformOffsetRatio NOTIFY stateChanged)

public:
    explicit NodeEditorQuickController(QObject* parent = nullptr);

    void setState(
        bool canOpenNode,
        const QString& label,
        const QString& nodeContainerPath,
        bool hasUnsavedChanges,
        double timelineFps,
        const std::optional<AudioClipPreviewState>& clipState,
        const QVariantList& nodeTracks);

    [[nodiscard]] bool canOpenNode() const;
    [[nodiscard]] bool canSaveNode() const;
    [[nodiscard]] bool canSaveNodeAs() const;
    [[nodiscard]] bool canExportNode() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectedNodeName() const;
    [[nodiscard]] QString nodeContainerText() const;
    [[nodiscard]] bool hasAttachedAudio() const;
    [[nodiscard]] int nodeTrackCount() const;
    [[nodiscard]] QVariantList nodeTracks() const;
    [[nodiscard]] QString selectedLaneId() const;
    [[nodiscard]] QString selectedLaneHeaderId() const;
    [[nodiscard]] QString selectedClipId() const;
    [[nodiscard]] QString audioSummaryText() const;
    [[nodiscard]] QString emptyBodyText() const;
    [[nodiscard]] bool showTimeline() const;
    [[nodiscard]] QString timelineStartText() const;
    [[nodiscard]] QString timelineEndText() const;
    [[nodiscard]] QString timelineDurationText() const;
    [[nodiscard]] QString playheadText() const;
    [[nodiscard]] int nodeDurationMs() const;
    [[nodiscard]] int playheadMs() const;
    [[nodiscard]] qreal playheadRatio() const;
    [[nodiscard]] int insertionMarkerMs() const;
    [[nodiscard]] qreal insertionMarkerRatio() const;
    [[nodiscard]] bool insertionMarkerStationary() const;
    [[nodiscard]] bool insertionFollowsPlayback() const;
    [[nodiscard]] bool playbackActive() const;
    [[nodiscard]] int laneMeterToken() const;
    [[nodiscard]] int meterResetToken() const;
    [[nodiscard]] qreal waveformWidthRatio() const;
    [[nodiscard]] qreal waveformOffsetRatio() const;

    Q_INVOKABLE void triggerFileAction(const QString& actionKey);
    Q_INVOKABLE void triggerAudioAction(const QString& actionKey);
    Q_INVOKABLE void selectLane(const QString& laneId);
    Q_INVOKABLE void selectLaneHeader(const QString& laneId);
    Q_INVOKABLE void selectClip(const QString& laneId, const QString& clipId);
    Q_INVOKABLE void setLaneMuted(const QString& laneId, bool muted);
    Q_INVOKABLE void setLaneSoloed(const QString& laneId, bool soloed);
    Q_INVOKABLE qreal laneMeterLevel(const QString& laneId) const;
    Q_INVOKABLE qreal laneMeterLeftLevel(const QString& laneId) const;
    Q_INVOKABLE qreal laneMeterRightLevel(const QString& laneId) const;
    Q_INVOKABLE bool laneUsesStereoMeter(const QString& laneId) const;
    Q_INVOKABLE void setPlayheadFromRatio(qreal ratio);
    void setPlayheadMs(int playheadMs);
    void setInsertionMarkerMs(int markerMs);
    void setInsertionFollowsPlayback(bool enabled);
    void setPlaybackActive(bool active);
    void setLaneMeterStates(const QVariantList& meterStates);

signals:
    void stateChanged();
    void playheadPositionChanged();
    void playbackStateChanged();
    void laneMeterLevelsChanged();
    void meterResetTokenChanged();
    void playheadChanged(int playheadMs);
    void laneMuteRequested(const QString& laneId, bool muted);
    void laneSoloRequested(const QString& laneId, bool soloed);
    void fileActionRequested(const QString& actionKey);
    void audioActionRequested(const QString& actionKey);

private:
    struct LaneMeterState
    {
        float meterLevel = 0.0F;
        float meterLeftLevel = 0.0F;
        float meterRightLevel = 0.0F;
        bool useStereoMeter = false;
    };

    bool syncLaneMeterTopology(const QVariantList& nodeTracks);

    bool m_canOpenNode = false;
    QString m_selectedNodeLabel;
    QString m_nodeContainerPath;
    bool m_hasUnsavedChanges = false;
    double m_timelineFps = 0.0;
    std::optional<AudioClipPreviewState> m_clipState;
    QVariantList m_nodeTracks;
    QString m_selectedLaneId;
    QString m_selectedLaneHeaderId;
    QString m_selectedClipId;
    int m_playheadMs = 0;
    int m_insertionMarkerMs = 0;
    bool m_insertionFollowsPlayback = false;
    bool m_playbackActive = false;
    QHash<QString, LaneMeterState> m_laneMeterStates;
    int m_laneMeterToken = 0;
    int m_meterResetToken = 0;
};
