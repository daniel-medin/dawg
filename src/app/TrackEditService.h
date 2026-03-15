#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QPointF>
#include <QSize>
#include <QString>
#include <QUuid>

#include "core/tracking/TrackTypes.h"

class AudioPoolService;
class MotionTracker;

class TrackEditService
{
public:
    struct AudioImportResult
    {
        bool accepted = false;
        bool poolChanged = false;
    };

    struct SeedTrackResult
    {
        bool created = false;
        QUuid trackId;
        QString label;
        bool motionTracked = false;
    };

    struct AudioTrackMutationResult
    {
        bool success = false;
        bool poolChanged = false;
        QUuid trackId;
    };

    struct PasteTracksResult
    {
        std::vector<QUuid> pastedTrackIds;
    };

    struct TrimTracksResult
    {
        int trimmedCount = 0;
        int missingAudioCount = 0;
        int failedDurationCount = 0;
    };

    TrackEditService(MotionTracker& tracker, AudioPoolService& audioPool);

    [[nodiscard]] AudioImportResult importAudioToPool(const QString& filePath);
    [[nodiscard]] SeedTrackResult seedTrack(
        int frameIndex,
        const QPointF& imagePoint,
        bool motionTrackingEnabled,
        int totalFrames,
        double fps);
    [[nodiscard]] AudioTrackMutationResult createTrackWithAudio(
        int frameIndex,
        const QPointF& imagePoint,
        bool motionTrackingEnabled,
        int totalFrames,
        double fps,
        const QString& filePath,
        const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack);
    [[nodiscard]] AudioTrackMutationResult attachAudioToTrack(
        const QUuid& trackId,
        const QString& filePath,
        const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack);
    [[nodiscard]] bool copyTracks(const std::vector<QUuid>& trackIds);
    [[nodiscard]] bool hasCopiedTracks() const;
    [[nodiscard]] PasteTracksResult pasteCopiedTracks(
        int currentFrameIndex,
        const QSize& frameSize,
        int totalFrames);
    [[nodiscard]] int removeTracks(const std::vector<QUuid>& trackIds);
    [[nodiscard]] bool setTrackStartFrame(const QUuid& trackId, int frameIndex, int totalFrames);
    [[nodiscard]] bool setTrackEndFrame(const QUuid& trackId, int frameIndex, int totalFrames);
    [[nodiscard]] int setTrackStartFrames(const std::vector<QUuid>& trackIds, int frameIndex);
    [[nodiscard]] int setTrackEndFrames(const std::vector<QUuid>& trackIds, int frameIndex);
    [[nodiscard]] int setAllTrackStartFrames(int frameIndex);
    [[nodiscard]] int setAllTrackEndFrames(int frameIndex);
    [[nodiscard]] TrimTracksResult trimTracksToAttachedSound(
        const std::vector<QUuid>& trackIds,
        const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack);
    void clearAllTracks();
    void reset();

private:
    [[nodiscard]] const TrackPoint* findTrack(const QUuid& trackId) const;

    MotionTracker& m_tracker;
    AudioPoolService& m_audioPool;
    std::vector<TrackPoint> m_copiedTracks;
};
