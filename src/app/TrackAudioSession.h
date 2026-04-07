#pragma once

#include <functional>
#include <optional>

#include <QHash>
#include <QTimer>
#include <QUuid>

#include "ui/AudioClipPreviewTypes.h"

class AudioEngine;
class AudioPlaybackCoordinator;
class MotionTracker;

class TrackAudioSession
{
public:
    TrackAudioSession(
        MotionTracker& tracker,
        AudioEngine& audioEngine,
        AudioPlaybackCoordinator& audioPlaybackCoordinator,
        QTimer& previewStopTimer);

    void reset();

    [[nodiscard]] const QHash<QUuid, int>& playheads() const;
    void setPlayheads(const QHash<QUuid, int>& playheads);

    [[nodiscard]] bool setSelectedTrackClipPlayheadMs(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        int playheadMs,
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath);
    [[nodiscard]] bool setSelectedTrackAudioGainDb(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        float gainDb,
        bool timelinePlaying,
        const std::function<void()>& applyLiveMixStateToCurrentPlayback);
    [[nodiscard]] bool setSelectedTrackLoopEnabled(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        bool enabled,
        bool timelinePlaying,
        const std::function<void()>& syncAttachedAudioForCurrentFrame);
    [[nodiscard]] bool startSelectedTrackClipPreview(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        bool timelinePlaying,
        const std::function<void(bool)>& pausePlayback,
        const std::function<void()>& stopAudioPoolPreview,
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath);
    void handlePreviewTimeout(
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath);
    void stopSelectedTrackClipPreview();
    [[nodiscard]] bool setSelectedTrackClipRangeMs(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        int clipStartMs,
        int clipEndMs,
        bool timelinePlaying,
        const std::function<void()>& syncAttachedAudioForCurrentFrame,
        const std::function<void()>& refreshOverlays,
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath);

    [[nodiscard]] bool selectedTrackLoopEnabled(bool hasVideoLoaded, const QUuid& selectedTrackId) const;
    [[nodiscard]] std::optional<AudioClipPreviewState> selectedAudioClipPreviewState(
        bool hasVideoLoaded,
        const QUuid& selectedTrackId,
        int currentFrameIndex,
        int totalFrames,
        bool timelinePlaying,
        const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath,
        const std::function<double(int)>& frameTimestampSecondsForFrame) const;
    [[nodiscard]] std::optional<int> currentSelectedTrackClipPreviewPlayheadMs() const;

private:
    [[nodiscard]] static float clampGainDb(float gainDb);
    [[nodiscard]] const struct TrackPoint* findTrack(const QUuid& trackId) const;
    [[nodiscard]] const struct TrackPoint* findTrackWithAudio(const QUuid& trackId) const;
    [[nodiscard]] std::optional<int> restartPreview(
        const QUuid& trackId,
        const QString& assetPath,
        int previewStartMs,
        int clipStartMs,
        int clipEndMs,
        float gainDb);

    MotionTracker& m_tracker;
    AudioEngine& m_audioEngine;
    AudioPlaybackCoordinator& m_audioPlaybackCoordinator;
    QTimer& m_previewStopTimer;
    QHash<QUuid, int> m_playheadMsByTrack;
};
