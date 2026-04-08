#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QElapsedTimer>
#include <QPointF>
#include <QString>
#include <QUuid>

#include "core/audio/AudioEngine.h"

struct TrackPoint;
struct TimelineTrackSpan;

class AudioPlaybackCoordinator
{
public:
    struct PlaybackSyncRequest
    {
        bool hasVideoLoaded = false;
        int currentFrameIndex = -1;
        int totalFrames = 0;
        int currentFrameWidth = 0;
        float masterGainDb = 0.0F;
        bool masterMuted = false;
        QUuid embeddedVideoAudioTrackId;
        QString embeddedVideoAudioPath;
        bool embeddedVideoAudioMuted = true;
    };

    struct ClipPreviewRequest
    {
        QUuid sourceTrackId;
        QString assetPath;
        int previewStartMs = 0;
        int clipStartMs = 0;
        int clipEndMs = 0;
        float gainDb = 0.0F;
    };

    struct NodePreviewClip
    {
        QUuid previewTrackId;
        QString laneId;
        QString assetPath;
        int laneOffsetMs = 0;
        int clipStartMs = 0;
        int clipEndMs = 0;
        float gainDb = 0.0F;
        bool loopEnabled = false;
        bool useStereoMeter = false;
    };

    struct NodePreviewLaneMeterState
    {
        QString laneId;
        float meterLevel = 0.0F;
        float meterLeftLevel = 0.0F;
        float meterRightLevel = 0.0F;
        bool useStereoMeter = false;
    };

    struct ProjectNodeClip
    {
        QUuid previewTrackId;
        QUuid ownerTrackId;
        QString assetPath;
        int projectStartMs = 0;
        int clipStartMs = 0;
        int clipEndMs = 0;
        float gainDb = 0.0F;
        float pan = 0.0F;
        bool useStereoMeter = false;
    };

    struct ProjectNodeTrackMeterState
    {
        QUuid ownerTrackId;
        float meterLevel = 0.0F;
        float meterLeftLevel = 0.0F;
        float meterRightLevel = 0.0F;
        bool useStereoMeter = false;
    };

    explicit AudioPlaybackCoordinator(AudioEngine& audioEngine);

    [[nodiscard]] const QUuid& audioPoolPreviewTrackId() const;
    [[nodiscard]] const QUuid& clipPreviewTrackId() const;

    bool startAudioPoolPreview(const QString& filePath, bool* stateChanged = nullptr);
    void stopAudioPoolPreview(bool* stateChanged = nullptr);
    [[nodiscard]] bool isAudioPoolPreviewPlaying() const;
    [[nodiscard]] QString audioPoolPreviewAssetPath() const;

    [[nodiscard]] bool isClipPreviewPlaying() const;
    [[nodiscard]] QUuid clipPreviewSourceTrackId() const;
    [[nodiscard]] std::optional<int> currentClipPreviewPlayheadMs() const;
    [[nodiscard]] std::optional<int> playClipPreview(const ClipPreviewRequest& request);
    void setClipPreviewGain(float gainDb);
    void stopClipPreview();
    bool syncNodePreview(
        const std::vector<NodePreviewClip>& clips,
        int nodeDurationMs,
        int playheadMs,
        bool forceRepositionActiveTracks = false);
    void stopNodePreview();
    [[nodiscard]] bool isNodePreviewPlaying() const;
    [[nodiscard]] AudioEngine::StereoLevels nodePreviewStereoLevels() const;
    [[nodiscard]] std::vector<NodePreviewLaneMeterState> nodePreviewLaneMeterStates() const;
    void syncProjectNodePlayback(
        const std::vector<ProjectNodeClip>& clips,
        int currentProjectMs);
    void stopProjectNodePlayback();
    [[nodiscard]] std::vector<ProjectNodeTrackMeterState> projectNodeTrackMeterStates() const;
    void applyLiveMixStateToCurrentPlayback(
        const PlaybackSyncRequest& request,
        const std::vector<TimelineTrackSpan>& spans,
        const std::vector<TrackPoint>& tracks,
        const std::unordered_map<int, float>& gainByLane,
        const std::unordered_map<int, bool>& mutedByLane,
        const std::unordered_map<int, bool>& soloByLane);
    void syncAttachedAudioForCurrentFrame(
        const PlaybackSyncRequest& request,
        const std::vector<TimelineTrackSpan>& spans,
        const std::vector<TrackPoint>& tracks,
        const std::unordered_map<int, float>& gainByLane,
        const std::unordered_map<int, bool>& mutedByLane,
        const std::unordered_map<int, bool>& soloByLane,
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath,
        const std::function<double(int)>& frameTimestampSecondsForFrame);
    void reset();

private:
    AudioEngine& m_audioEngine;
    const QUuid m_audioPoolPreviewTrackId = QUuid(QStringLiteral("{8d6166c4-b107-4c55-8f11-f9cbf67d0e0a}"));
    const QUuid m_clipPreviewTrackId = QUuid(QStringLiteral("{3427e43b-a4c0-4d8e-86ea-52e9d85f2747}"));
    QString m_audioPoolPreviewAssetPath;
    QUuid m_clipPreviewSourceTrackId;
    QElapsedTimer m_clipPreviewElapsedTimer;
    int m_clipPreviewStartMs = 0;
    int m_clipPreviewClipStartMs = 0;
    int m_clipPreviewClipEndMs = 0;
    bool m_nodePreviewActive = false;
    std::vector<NodePreviewClip> m_nodePreviewClips;
    std::vector<QUuid> m_nodePreviewTrackIds;
    std::vector<QUuid> m_nodePreviewActiveTrackIds;
    std::vector<ProjectNodeClip> m_projectNodeClips;
    std::vector<QUuid> m_projectNodeTrackIds;
    std::vector<QUuid> m_projectNodeActiveTrackIds;
};
