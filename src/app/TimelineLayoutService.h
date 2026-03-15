#pragma once

#include <unordered_map>
#include <vector>

#include <QUuid>

#include "core/tracking/TrackTypes.h"
#include "ui/MixView.h"
#include "ui/TimelineView.h"

class AudioEngine;

class TimelineLayoutService
{
public:
    [[nodiscard]] static std::vector<TimelineTrackSpan> timelineTrackSpans(
        const std::vector<TrackPoint>& tracks,
        int totalFrames,
        const std::vector<QUuid>& selectedTrackIds);

    [[nodiscard]] static std::vector<MixLaneStrip> mixLaneStrips(
        const std::vector<TimelineTrackSpan>& spans,
        const std::vector<TrackPoint>& tracks,
        const AudioEngine& audioEngine,
        const std::unordered_map<int, float>& gainByLane,
        const std::unordered_map<int, bool>& mutedByLane,
        const std::unordered_map<int, bool>& soloByLane);
};
