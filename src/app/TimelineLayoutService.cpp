#include "app/TimelineLayoutService.h"

#include <algorithm>
#include "core/audio/AudioEngine.h"

namespace
{
struct TimelineTrackCandidate
{
    const TrackPoint* track = nullptr;
    int startFrame = 0;
    int endFrame = 0;
};

float laneGainDb(
    const std::unordered_map<int, float>& gainByLane,
    const int laneIndex)
{
    const auto gainIt = gainByLane.find(laneIndex);
    return gainIt != gainByLane.end() ? gainIt->second : 0.0F;
}

bool laneMuted(
    const std::unordered_map<int, bool>& mutedByLane,
    const int laneIndex)
{
    const auto mutedIt = mutedByLane.find(laneIndex);
    return mutedIt != mutedByLane.end() && mutedIt->second;
}

bool laneSoloed(
    const std::unordered_map<int, bool>& soloByLane,
    const int laneIndex)
{
    const auto soloIt = soloByLane.find(laneIndex);
    return soloIt != soloByLane.end() && soloIt->second;
}
}

std::vector<TimelineTrackSpan> TimelineLayoutService::timelineTrackSpans(
    const std::vector<TrackPoint>& tracks,
    const int totalFrames,
    const std::vector<QUuid>& selectedTrackIds)
{
    std::vector<TimelineTrackCandidate> candidates;
    candidates.reserve(tracks.size());

    for (const auto& track : tracks)
    {
        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = track.endFrame.has_value()
            ? std::max(startFrame, *track.endFrame)
            : std::max(startFrame, totalFrames > 0 ? (totalFrames - 1) : track.startFrame);

        candidates.push_back(TimelineTrackCandidate{
            .track = &track,
            .startFrame = startFrame,
            .endFrame = endFrame
        });
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const TimelineTrackCandidate& left, const TimelineTrackCandidate& right)
        {
            if (left.startFrame != right.startFrame)
            {
                return left.startFrame < right.startFrame;
            }

            if (left.endFrame != right.endFrame)
            {
                return left.endFrame < right.endFrame;
            }

            return left.track && right.track && left.track->label < right.track->label;
        });

    std::vector<TimelineTrackSpan> trackSpans;
    trackSpans.reserve(candidates.size());
    std::vector<int> laneEndFrames;

    for (const auto& candidate : candidates)
    {
        int laneIndex = 0;
        for (; laneIndex < static_cast<int>(laneEndFrames.size()); ++laneIndex)
        {
            if (candidate.startFrame > laneEndFrames[static_cast<std::size_t>(laneIndex)])
            {
                break;
            }
        }

        if (laneIndex == static_cast<int>(laneEndFrames.size()))
        {
            laneEndFrames.push_back(candidate.endFrame);
        }
        else
        {
            laneEndFrames[static_cast<std::size_t>(laneIndex)] = candidate.endFrame;
        }

        trackSpans.push_back(TimelineTrackSpan{
            .id = candidate.track->id,
            .label = candidate.track->label,
            .color = trackDisplayColor(*candidate.track),
            .startFrame = candidate.startFrame,
            .endFrame = candidate.endFrame,
            .laneIndex = laneIndex,
            .hasAttachedAudio = candidate.track->attachedAudio.has_value(),
            .isSelected = std::find(
                selectedTrackIds.cbegin(),
                selectedTrackIds.cend(),
                candidate.track->id) != selectedTrackIds.cend()
        });
    }

    std::stable_sort(
        trackSpans.begin(),
        trackSpans.end(),
        [](const TimelineTrackSpan& left, const TimelineTrackSpan& right)
        {
            if (left.laneIndex != right.laneIndex)
            {
                return left.laneIndex < right.laneIndex;
            }

            if (left.startFrame != right.startFrame)
            {
                return left.startFrame < right.startFrame;
            }

            if (left.endFrame != right.endFrame)
            {
                return left.endFrame < right.endFrame;
            }

            return left.label < right.label;
        });

    return trackSpans;
}

std::vector<MixLaneStrip> TimelineLayoutService::mixLaneStrips(
    const std::vector<TimelineTrackSpan>& spans,
    const std::vector<TrackPoint>& tracks,
    const AudioEngine& audioEngine,
    const std::unordered_map<int, float>& gainByLane,
    const std::unordered_map<int, bool>& mutedByLane,
    const std::unordered_map<int, bool>& soloByLane)
{
    std::vector<MixLaneStrip> strips;
    strips.reserve(spans.size());

    for (const auto& span : spans)
    {
        auto stripIt = std::find_if(
            strips.begin(),
            strips.end(),
            [&span](const MixLaneStrip& strip)
            {
                return strip.laneIndex == span.laneIndex;
            });
        if (stripIt == strips.end())
        {
            strips.push_back(MixLaneStrip{
                .laneIndex = span.laneIndex,
                .label = QStringLiteral("Track %1").arg(span.laneIndex + 1),
                .color = span.color,
                .gainDb = laneGainDb(gainByLane, span.laneIndex),
                .meterLevel = 0.0F,
                .clipCount = 1,
                .muted = laneMuted(mutedByLane, span.laneIndex),
                .soloed = laneSoloed(soloByLane, span.laneIndex)
            });
            continue;
        }

        ++stripIt->clipCount;
    }

    for (auto& strip : strips)
    {
        float laneLevel = 0.0F;
        for (const auto& track : tracks)
        {
            const auto spanIt = std::find_if(
                spans.begin(),
                spans.end(),
                [&track](const TimelineTrackSpan& span)
                {
                    return span.id == track.id;
                });
            if (spanIt == spans.end() || spanIt->laneIndex != strip.laneIndex)
            {
                continue;
            }

            if (!track.attachedAudio.has_value())
            {
                continue;
            }

            strip.color = track.color;
            laneLevel = std::max(laneLevel, audioEngine.trackLevel(track.id));
        }

        strip.meterLevel = laneLevel;
    }

    return strips;
}
