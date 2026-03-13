#include "core/tracking/MotionTracker.h"

#include <array>
#include <algorithm>

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

        track.samples.insert_or_assign(frameIndex, imagePoint);
        return true;
    }

    return false;
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

int MotionTracker::setAllTrackStartFrames(const int startFrame)
{
    int updatedCount = 0;

    for (auto& track : m_tracks)
    {
        track.startFrame = startFrame;
        if (track.endFrame.has_value() && *track.endFrame < track.startFrame)
        {
            track.endFrame = track.startFrame;
        }
        ++updatedCount;
    }

    return updatedCount;
}

int MotionTracker::setAllTrackEndFrames(const int endFrame)
{
    int updatedCount = 0;

    for (auto& track : m_tracks)
    {
        track.endFrame = std::max(endFrame, track.startFrame);
        ++updatedCount;
    }

    return updatedCount;
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

const std::vector<TrackPoint>& MotionTracker::tracks() const
{
    return m_tracks;
}

std::vector<TrackOverlay> MotionTracker::overlaysForFrame(
    const int frameIndex,
    const QUuid& selectedTrackId,
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
            .color = track.color,
            .imagePoint = *imagePoint,
            .isSelected = track.id == selectedTrackId,
            .highlightOpacity = track.id == selectedTrackId ? 1.0F
                : (track.id == fadingTrackId ? fadingTrackOpacity : 0.0F),
            .hasAttachedAudio = track.attachedAudio.has_value()
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
