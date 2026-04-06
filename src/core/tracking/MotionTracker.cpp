#include "core/tracking/MotionTracker.h"

#include <array>
#include <algorithm>

#include <QFileInfo>

#include <opencv2/video/tracking.hpp>

TrackPoint& MotionTracker::seedTrack(const int frameIndex, const QPointF& imagePoint, const bool motionTracked)
{
    TrackPoint track;
    track.label = QStringLiteral("Node %1").arg(m_tracks.size() + 1);
    track.color = nextTrackColor();
    track.seedFrameIndex = frameIndex;
    track.startFrame = frameIndex;
    track.motionTracked = motionTracked;
    track.samples.emplace(frameIndex, imagePoint);

    m_tracks.push_back(track);
    return m_tracks.back();
}

void MotionTracker::trackForward(const cv::Mat& previousGrayFrame, const cv::Mat& currentGrayFrame, const int currentFrameIndex)
{
    if (previousGrayFrame.empty() || currentGrayFrame.empty())
    {
        return;
    }

    std::vector<cv::Point2f> previousPoints;
    std::vector<std::size_t> trackIndices;

    for (std::size_t index = 0; index < m_tracks.size(); ++index)
    {
        auto& track = m_tracks[index];
        const auto previousFrameIndex = currentFrameIndex - 1;

        if (!track.motionTracked || !track.hasSample(previousFrameIndex) || track.hasSample(currentFrameIndex))
        {
            continue;
        }

        const auto previousSample = track.sampleAt(previousFrameIndex);
        previousPoints.emplace_back(static_cast<float>(previousSample.x()), static_cast<float>(previousSample.y()));
        trackIndices.push_back(index);
    }

    if (previousPoints.empty())
    {
        return;
    }

    std::vector<cv::Point2f> currentPoints;
    std::vector<unsigned char> status;
    std::vector<float> error;

    cv::calcOpticalFlowPyrLK(
        previousGrayFrame,
        currentGrayFrame,
        previousPoints,
        currentPoints,
        status,
        error,
        cv::Size{21, 21},
        3,
        cv::TermCriteria{cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01});

    const auto frameSize = currentGrayFrame.size();

    for (std::size_t pointIndex = 0; pointIndex < currentPoints.size(); ++pointIndex)
    {
        if (!status[pointIndex] || !isInsideFrame(currentPoints[pointIndex], frameSize))
        {
            continue;
        }

        auto& track = m_tracks[trackIndices[pointIndex]];
        track.samples.emplace(
            currentFrameIndex,
            QPointF{currentPoints[pointIndex].x, currentPoints[pointIndex].y});
    }
}

void MotionTracker::reset()
{
    m_tracks.clear();
    m_nextColorIndex = 0;
}

bool MotionTracker::hasTrack(const QUuid& trackId) const
{
    return std::any_of(
        m_tracks.begin(),
        m_tracks.end(),
        [&trackId](const auto& track)
        {
            return track.id == trackId;
        });
}

bool MotionTracker::updateTrackSample(const QUuid& trackId, const int frameIndex, const QPointF& imagePoint)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        const auto existingIt = track.samples.find(frameIndex);
        if (existingIt != track.samples.end() && existingIt->second == imagePoint)
        {
            return false;
        }

        track.samples.insert_or_assign(frameIndex, imagePoint);
        return true;
    }

    return false;
}

bool MotionTracker::setTrackLabel(const QUuid& trackId, const QString& label)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.label = label;
        return true;
    }

    return false;
}

bool MotionTracker::setTrackNodeDocument(
    const QUuid& trackId,
    const QString& nodeDocumentPath,
    const int timelineFrameCount,
    const double timelineFps)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.nodeDocumentPath = nodeDocumentPath;
        track.nodeTimelineFrameCount = std::max(0, timelineFrameCount);
        track.nodeTimelineFps = timelineFps > 0.0 ? timelineFps : 0.0;
        return true;
    }

    return false;
}

bool MotionTracker::isTrackAutoPanEnabled(const QUuid& trackId) const
{
    for (const auto& track : m_tracks)
    {
        if (track.id == trackId)
        {
            return track.autoPanEnabled;
        }
    }

    return false;
}

bool MotionTracker::setTrackAutoPanEnabled(const QUuid& trackId, const bool enabled)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.autoPanEnabled = enabled;
        return true;
    }

    return false;
}

bool MotionTracker::isTrackLabelVisible(const QUuid& trackId) const
{
    for (const auto& track : m_tracks)
    {
        if (track.id == trackId)
        {
            return track.showLabel;
        }
    }

    return false;
}

int MotionTracker::setTrackLabelsVisible(const std::vector<QUuid>& trackIds, const bool visible)
{
    if (trackIds.empty())
    {
        return 0;
    }

    int updatedCount = 0;

    for (auto& track : m_tracks)
    {
        if (std::find(trackIds.begin(), trackIds.end(), track.id) == trackIds.end())
        {
            continue;
        }

        if (track.showLabel == visible)
        {
            continue;
        }

        track.showLabel = visible;
        ++updatedCount;
    }

    return updatedCount;
}

bool MotionTracker::setTrackStartFrame(const QUuid& trackId, const int startFrame)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.startFrame = startFrame;
        if (track.endFrame.has_value() && *track.endFrame < track.startFrame)
        {
            track.endFrame = track.startFrame;
        }
        return true;
    }

    return false;
}

bool MotionTracker::setTrackEndFrame(const QUuid& trackId, const int endFrame)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.endFrame = std::max(endFrame, track.startFrame);
        return true;
    }

    return false;
}

bool MotionTracker::moveTrackFrameSpan(const QUuid& trackId, const int deltaFrames, const int maxFrameIndex)
{
    if (deltaFrames == 0)
    {
        return false;
    }

    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        const auto currentStart = std::max(0, track.startFrame);
        const auto currentEnd = track.endFrame.has_value()
            ? std::max(currentStart, *track.endFrame)
            : currentStart;
        const auto spanLength = currentEnd - currentStart;
        const auto allowedMaxStart = std::max(0, maxFrameIndex - spanLength);
        const auto newStart = std::clamp(currentStart + deltaFrames, 0, allowedMaxStart);
        if (newStart == currentStart)
        {
            return false;
        }

        track.startFrame = newStart;
        track.endFrame = std::clamp(newStart + spanLength, newStart, std::max(newStart, maxFrameIndex));
        return true;
    }

    return false;
}

bool MotionTracker::setTrackAudioAttachment(const QUuid& trackId, const QString& assetPath)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId)
        {
            continue;
        }

        track.attachedAudio = AudioAttachment{.assetPath = assetPath};
        const QFileInfo fileInfo{assetPath};
        if (!fileInfo.fileName().isEmpty())
        {
            track.label = fileInfo.fileName();
        }
        return true;
    }

    return false;
}

bool MotionTracker::setTrackAudioClipRange(const QUuid& trackId, const int clipStartMs, std::optional<int> clipEndMs)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId || !track.attachedAudio.has_value())
        {
            continue;
        }

        track.attachedAudio->clipStartMs = std::max(0, clipStartMs);
        if (clipEndMs.has_value())
        {
            track.attachedAudio->clipEndMs = std::max(track.attachedAudio->clipStartMs, *clipEndMs);
        }
        else
        {
            track.attachedAudio->clipEndMs.reset();
        }
        return true;
    }

    return false;
}

bool MotionTracker::setTrackAudioGainDb(const QUuid& trackId, const float gainDb)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId || !track.attachedAudio.has_value())
        {
            continue;
        }

        track.attachedAudio->gainDb = gainDb;
        return true;
    }

    return false;
}

bool MotionTracker::setTrackAudioLoopEnabled(const QUuid& trackId, const bool enabled)
{
    for (auto& track : m_tracks)
    {
        if (track.id != trackId || !track.attachedAudio.has_value())
        {
            continue;
        }

        track.attachedAudio->loopEnabled = enabled;
        return true;
    }

    return false;
}

int MotionTracker::detachTrackAudioByPath(const QString& assetPath)
{
    int detachedCount = 0;

    for (auto& track : m_tracks)
    {
        if (!track.attachedAudio.has_value() || track.attachedAudio->assetPath != assetPath)
        {
            continue;
        }

        track.attachedAudio.reset();
        ++detachedCount;
    }

    return detachedCount;
}

void MotionTracker::addTrack(const TrackPoint& track)
{
    m_tracks.push_back(track);
}

void MotionTracker::restoreState(const MotionTrackerState& state)
{
    m_tracks = state.tracks;
    m_nextColorIndex = state.nextColorIndex;
}

int MotionTracker::setTrackStartFrames(const std::vector<QUuid>& trackIds, const int startFrame)
{
    if (trackIds.empty())
    {
        return 0;
    }

    int updatedCount = 0;

    for (auto& track : m_tracks)
    {
        if (std::find(trackIds.begin(), trackIds.end(), track.id) == trackIds.end())
        {
            continue;
        }

        const auto extendsToFrame = !track.endFrame.has_value() || *track.endFrame >= startFrame;
        if (track.startFrame < startFrame && extendsToFrame)
        {
            track.startFrame = startFrame;
            ++updatedCount;
        }
    }

    return updatedCount;
}

int MotionTracker::setTrackEndFrames(const std::vector<QUuid>& trackIds, const int endFrame)
{
    if (trackIds.empty())
    {
        return 0;
    }

    int updatedCount = 0;

    for (auto& track : m_tracks)
    {
        if (std::find(trackIds.begin(), trackIds.end(), track.id) == trackIds.end())
        {
            continue;
        }

        const auto extendsPastFrame = !track.endFrame.has_value() || *track.endFrame > endFrame;
        if (track.startFrame <= endFrame && extendsPastFrame)
        {
            track.endFrame = endFrame;
            ++updatedCount;
        }
    }

    return updatedCount;
}

int MotionTracker::setAllTrackStartFrames(const int startFrame)
{
    std::vector<QUuid> trackIds;
    trackIds.reserve(m_tracks.size());
    for (const auto& track : m_tracks)
    {
        trackIds.push_back(track.id);
    }

    return setTrackStartFrames(trackIds, startFrame);
}

int MotionTracker::setAllTrackEndFrames(const int endFrame)
{
    std::vector<QUuid> trackIds;
    trackIds.reserve(m_tracks.size());
    for (const auto& track : m_tracks)
    {
        trackIds.push_back(track.id);
    }

    return setTrackEndFrames(trackIds, endFrame);
}

bool MotionTracker::removeTrack(const QUuid& trackId)
{
    const auto newEnd = std::remove_if(
        m_tracks.begin(),
        m_tracks.end(),
        [&trackId](const auto& track)
        {
            return track.id == trackId;
        });

    if (newEnd == m_tracks.end())
    {
        return false;
    }

    m_tracks.erase(newEnd, m_tracks.end());
    return true;
}

int MotionTracker::removeTracks(const std::vector<QUuid>& trackIds)
{
    if (trackIds.empty())
    {
        return 0;
    }

    const auto previousSize = static_cast<int>(m_tracks.size());
    const auto newEnd = std::remove_if(
        m_tracks.begin(),
        m_tracks.end(),
        [&trackIds](const auto& track)
        {
            return std::find(trackIds.begin(), trackIds.end(), track.id) != trackIds.end();
        });

    if (newEnd == m_tracks.end())
    {
        return 0;
    }

    m_tracks.erase(newEnd, m_tracks.end());
    return previousSize - static_cast<int>(m_tracks.size());
}

const std::vector<TrackPoint>& MotionTracker::tracks() const
{
    return m_tracks;
}

QString MotionTracker::trackNodeDocumentPath(const QUuid& trackId) const
{
    for (const auto& track : m_tracks)
    {
        if (track.id == trackId)
        {
            return track.nodeDocumentPath;
        }
    }

    return {};
}

MotionTrackerState MotionTracker::snapshotState() const
{
    return MotionTrackerState{
        .tracks = m_tracks,
        .nextColorIndex = m_nextColorIndex
    };
}

bool MotionTracker::hasMotionTrackedTracks() const
{
    return std::any_of(
        m_tracks.begin(),
        m_tracks.end(),
        [](const TrackPoint& track)
        {
            return track.motionTracked;
        });
}

std::vector<TrackOverlay> MotionTracker::overlaysForFrame(
    const int frameIndex,
    const std::vector<QUuid>& selectedTrackIds,
    const QUuid& fadingTrackId,
    const float fadingTrackOpacity) const
{
    std::vector<TrackOverlay> overlays;
    overlays.reserve(m_tracks.size());

    for (const auto& track : m_tracks)
    {
        if (!track.isVisibleAt(frameIndex))
        {
            continue;
        }

        const auto imagePoint = track.motionTracked
            ? (track.hasSample(frameIndex) ? std::optional<QPointF>{track.sampleAt(frameIndex)} : std::nullopt)
            : track.interpolatedSampleAt(frameIndex);

        if (!imagePoint.has_value())
        {
            continue;
        }

        overlays.push_back(TrackOverlay{
            .id = track.id,
            .label = track.label,
            .color = trackDisplayColor(track),
            .imagePoint = *imagePoint,
            .isSelected = std::find(selectedTrackIds.begin(), selectedTrackIds.end(), track.id) != selectedTrackIds.end(),
            .highlightOpacity = (std::find(selectedTrackIds.begin(), selectedTrackIds.end(), track.id) != selectedTrackIds.end()) ? 1.0F
                : (track.id == fadingTrackId ? fadingTrackOpacity : 0.0F),
            .showLabel = track.showLabel,
            .hasAttachedAudio = track.attachedAudio.has_value(),
            .autoPanEnabled = track.autoPanEnabled
        });
    }

    return overlays;
}

QColor MotionTracker::nextTrackColor()
{
    static constexpr std::array<const char*, 6> palette{
        "#ff6b35",
        "#00b894",
        "#0984e3",
        "#fdcb6e",
        "#e84393",
        "#6c5ce7"
    };

    const auto color = QColor{palette[static_cast<std::size_t>(m_nextColorIndex % static_cast<int>(palette.size()))]};
    ++m_nextColorIndex;
    return color;
}

bool MotionTracker::isInsideFrame(const cv::Point2f& point, const cv::Size& frameSize)
{
    return point.x >= 0.0F
        && point.y >= 0.0F
        && point.x < static_cast<float>(frameSize.width)
        && point.y < static_cast<float>(frameSize.height);
}
