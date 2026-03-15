#include "app/TrackEditService.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "app/AudioPoolService.h"
#include "core/tracking/MotionTracker.h"

namespace
{
int trackAnchorFrame(const TrackPoint& track)
{
    if (track.seedFrameIndex >= 0)
    {
        return track.seedFrameIndex;
    }

    if (!track.samples.empty())
    {
        return track.samples.begin()->first;
    }

    return std::max(0, track.startFrame);
}

QPointF trackAnchorPoint(const TrackPoint& track)
{
    const auto anchorFrame = trackAnchorFrame(track);
    if (track.hasSample(anchorFrame))
    {
        return track.sampleAt(anchorFrame);
    }

    if (!track.samples.empty())
    {
        return track.samples.begin()->second;
    }

    return QPointF{};
}

int clampedFrameIndex(const int frameIndex, const int totalFrames)
{
    const auto maxFrameIndex = totalFrames > 0 ? (totalFrames - 1) : 0;
    return std::clamp(frameIndex, 0, maxFrameIndex);
}
}

TrackEditService::TrackEditService(MotionTracker& tracker, AudioPoolService& audioPool)
    : m_tracker(tracker)
    , m_audioPool(audioPool)
{
}

TrackEditService::AudioImportResult TrackEditService::importAudioToPool(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return {};
    }

    return {
        .accepted = true,
        .poolChanged = m_audioPool.import(filePath)
    };
}

TrackEditService::SeedTrackResult TrackEditService::seedTrack(
    const int frameIndex,
    const QPointF& imagePoint,
    const bool motionTrackingEnabled,
    const int totalFrames,
    const double fps)
{
    if (totalFrames <= 0)
    {
        return {};
    }

    auto& track = m_tracker.seedTrack(frameIndex, imagePoint, motionTrackingEnabled);
    const auto safeFps = fps > 0.0 ? fps : 30.0;
    const auto placeholderFrameCount = std::max(1, static_cast<int>(std::lround(safeFps)));
    const auto maxFrameIndex = std::max(0, totalFrames - 1);
    const auto defaultEndFrame = std::clamp(
        track.startFrame + placeholderFrameCount - 1,
        track.startFrame,
        maxFrameIndex);
    m_tracker.setTrackEndFrame(track.id, defaultEndFrame);
    return {
        .created = true,
        .trackId = track.id,
        .label = track.label,
        .motionTracked = track.motionTracked
    };
}

TrackEditService::AudioTrackMutationResult TrackEditService::createTrackWithAudio(
    const int frameIndex,
    const QPointF& imagePoint,
    const bool motionTrackingEnabled,
    const int totalFrames,
    const double fps,
    const QString& filePath,
    const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack)
{
    const auto importResult = importAudioToPool(filePath);
    if (!importResult.accepted)
    {
        return {};
    }

    const auto seedResult = seedTrack(frameIndex, imagePoint, motionTrackingEnabled, totalFrames, fps);
    if (!seedResult.created)
    {
        return {
            .success = false,
            .poolChanged = importResult.poolChanged
        };
    }

    if (!m_tracker.setTrackAudioAttachment(seedResult.trackId, filePath))
    {
        m_tracker.removeTrack(seedResult.trackId);
        return {
            .success = false,
            .poolChanged = importResult.poolChanged
        };
    }

    if (const auto* track = findTrack(seedResult.trackId))
    {
        if (const auto endFrame = trimmedEndFrameForTrack(*track); endFrame.has_value())
        {
            m_tracker.setTrackEndFrame(seedResult.trackId, *endFrame);
        }
    }

    return {
        .success = true,
        .poolChanged = importResult.poolChanged,
        .trackId = seedResult.trackId
    };
}

TrackEditService::AudioTrackMutationResult TrackEditService::attachAudioToTrack(
    const QUuid& trackId,
    const QString& filePath,
    const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack)
{
    const auto importResult = importAudioToPool(filePath);
    if (!importResult.accepted || trackId.isNull())
    {
        return {};
    }

    if (!m_tracker.setTrackAudioAttachment(trackId, filePath))
    {
        return {
            .success = false,
            .poolChanged = importResult.poolChanged
        };
    }

    if (const auto* track = findTrack(trackId))
    {
        if (const auto endFrame = trimmedEndFrameForTrack(*track); endFrame.has_value())
        {
            m_tracker.setTrackEndFrame(trackId, *endFrame);
        }
    }

    return {
        .success = true,
        .poolChanged = importResult.poolChanged,
        .trackId = trackId
    };
}

bool TrackEditService::copyTracks(const std::vector<QUuid>& trackIds)
{
    if (trackIds.empty())
    {
        return false;
    }

    m_copiedTracks.clear();
    m_copiedTracks.reserve(trackIds.size());
    for (const auto& trackId : trackIds)
    {
        if (const auto* track = findTrack(trackId))
        {
            m_copiedTracks.push_back(*track);
        }
    }

    return !m_copiedTracks.empty();
}

bool TrackEditService::hasCopiedTracks() const
{
    return !m_copiedTracks.empty();
}

TrackEditService::PasteTracksResult TrackEditService::pasteCopiedTracks(
    const int currentFrameIndex,
    const QSize& frameSize,
    const int totalFrames)
{
    PasteTracksResult result;
    if (m_copiedTracks.empty() || frameSize.width() <= 0 || frameSize.height() <= 0)
    {
        return result;
    }

    const auto imageCenter = QPointF{
        static_cast<double>(frameSize.width()) * 0.5,
        static_cast<double>(frameSize.height()) * 0.5
    };
    const auto maxFrameIndex = std::max(0, totalFrames - 1);
    result.pastedTrackIds.reserve(m_copiedTracks.size());

    for (const auto& copiedTrack : m_copiedTracks)
    {
        auto pastedTrack = copiedTrack;
        pastedTrack.id = QUuid::createUuid();

        const auto anchorFrame = trackAnchorFrame(copiedTrack);
        const auto anchorPoint = trackAnchorPoint(copiedTrack);
        const auto deltaFrames = currentFrameIndex - anchorFrame;
        const auto deltaPoint = imageCenter - anchorPoint;

        std::map<int, QPointF> shiftedSamples;
        for (const auto& [frameIndex, point] : copiedTrack.samples)
        {
            const auto shiftedFrameIndex = frameIndex + deltaFrames;
            if (shiftedFrameIndex < 0 || shiftedFrameIndex > maxFrameIndex)
            {
                continue;
            }

            shiftedSamples.insert_or_assign(shiftedFrameIndex, point + deltaPoint);
        }

        if (shiftedSamples.empty())
        {
            shiftedSamples.emplace(currentFrameIndex, imageCenter);
        }

        pastedTrack.samples = std::move(shiftedSamples);
        pastedTrack.seedFrameIndex = std::clamp(copiedTrack.seedFrameIndex + deltaFrames, 0, maxFrameIndex);
        pastedTrack.startFrame = std::clamp(copiedTrack.startFrame + deltaFrames, 0, maxFrameIndex);
        if (pastedTrack.endFrame.has_value())
        {
            pastedTrack.endFrame = std::clamp(*copiedTrack.endFrame + deltaFrames, pastedTrack.startFrame, maxFrameIndex);
        }

        m_tracker.addTrack(pastedTrack);
        result.pastedTrackIds.push_back(pastedTrack.id);
    }

    return result;
}

int TrackEditService::removeTracks(const std::vector<QUuid>& trackIds)
{
    if (trackIds.empty())
    {
        return 0;
    }

    return trackIds.size() == 1
        ? (m_tracker.removeTrack(trackIds.front()) ? 1 : 0)
        : m_tracker.removeTracks(trackIds);
}

bool TrackEditService::setTrackStartFrame(const QUuid& trackId, const int frameIndex, const int totalFrames)
{
    return !trackId.isNull()
        && m_tracker.setTrackStartFrame(trackId, clampedFrameIndex(frameIndex, totalFrames));
}

bool TrackEditService::setTrackEndFrame(const QUuid& trackId, const int frameIndex, const int totalFrames)
{
    return !trackId.isNull()
        && m_tracker.setTrackEndFrame(trackId, clampedFrameIndex(frameIndex, totalFrames));
}

int TrackEditService::setTrackStartFrames(const std::vector<QUuid>& trackIds, const int frameIndex)
{
    if (trackIds.empty())
    {
        return 0;
    }

    return trackIds.size() == 1
        ? (m_tracker.setTrackStartFrame(trackIds.front(), frameIndex) ? 1 : 0)
        : m_tracker.setTrackStartFrames(trackIds, frameIndex);
}

int TrackEditService::setTrackEndFrames(const std::vector<QUuid>& trackIds, const int frameIndex)
{
    if (trackIds.empty())
    {
        return 0;
    }

    return trackIds.size() == 1
        ? (m_tracker.setTrackEndFrame(trackIds.front(), frameIndex) ? 1 : 0)
        : m_tracker.setTrackEndFrames(trackIds, frameIndex);
}

int TrackEditService::setAllTrackStartFrames(const int frameIndex)
{
    return m_tracker.setAllTrackStartFrames(frameIndex);
}

int TrackEditService::setAllTrackEndFrames(const int frameIndex)
{
    return m_tracker.setAllTrackEndFrames(frameIndex);
}

TrackEditService::TrimTracksResult TrackEditService::trimTracksToAttachedSound(
    const std::vector<QUuid>& trackIds,
    const std::function<std::optional<int>(const TrackPoint&)>& trimmedEndFrameForTrack)
{
    TrimTracksResult result;
    for (const auto& trackId : trackIds)
    {
        const auto* track = findTrack(trackId);
        if (track == nullptr)
        {
            continue;
        }

        if (!track->attachedAudio.has_value())
        {
            ++result.missingAudioCount;
            continue;
        }

        const auto endFrame = trimmedEndFrameForTrack(*track);
        if (!endFrame.has_value())
        {
            ++result.failedDurationCount;
            continue;
        }

        if (m_tracker.setTrackEndFrame(trackId, *endFrame))
        {
            ++result.trimmedCount;
        }
    }

    return result;
}

void TrackEditService::clearAllTracks()
{
    m_tracker.reset();
}

void TrackEditService::reset()
{
    m_copiedTracks.clear();
}

const TrackPoint* TrackEditService::findTrack(const QUuid& trackId) const
{
    const auto trackIt = std::find_if(
        m_tracker.tracks().cbegin(),
        m_tracker.tracks().cend(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });
    return trackIt != m_tracker.tracks().cend() ? &(*trackIt) : nullptr;
}
