#include "app/ProjectSessionAdapter.h"

#include <algorithm>
#include <cmath>

#include <QFileInfo>

#include "app/AudioPoolService.h"
#include "core/tracking/MotionTracker.h"

namespace
{
template <typename Value>
Value lookupOrDefault(
    const std::unordered_map<int, Value>& valuesByLane,
    const int laneIndex,
    const Value defaultValue)
{
    const auto valueIt = valuesByLane.find(laneIndex);
    return valueIt != valuesByLane.end() ? valueIt->second : defaultValue;
}
}

dawg::project::ControllerState ProjectSessionAdapter::snapshot(const SnapshotInput& input)
{
    dawg::project::ControllerState state;
    state.videoPath = input.videoPath;
    state.audioPoolAssetPaths = input.audioPool.assetPaths();
    state.trackerState = input.tracker.snapshotState();
    state.selectedTrackIds = input.selectedTrackIds;
    state.currentFrameIndex = input.currentFrameIndex;
    state.motionTrackingEnabled = input.motionTrackingEnabled;
    state.insertionFollowsPlayback = input.insertionFollowsPlayback;
    state.fastPlaybackEnabled = input.fastPlaybackEnabled;
    state.embeddedVideoAudioMuted = input.embeddedVideoAudioMuted;
    state.loopStartFrame = input.loopStartFrame;
    state.loopEndFrame = input.loopEndFrame;
    state.masterMixGainDb = input.masterMixGainDb;
    state.masterMixMuted = input.masterMixMuted;

    std::vector<int> laneIndices;
    laneIndices.reserve(
        input.mixLaneGainDbByLane.size()
        + input.mixLaneMutedByLane.size()
        + input.mixLaneSoloByLane.size());
    for (const auto& [laneIndex, _] : input.mixLaneGainDbByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    for (const auto& [laneIndex, _] : input.mixLaneMutedByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    for (const auto& [laneIndex, _] : input.mixLaneSoloByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    std::sort(laneIndices.begin(), laneIndices.end());
    laneIndices.erase(std::unique(laneIndices.begin(), laneIndices.end()), laneIndices.end());
    for (const auto laneIndex : laneIndices)
    {
        state.mixLanes.push_back(dawg::project::MixLaneState{
            .laneIndex = laneIndex,
            .gainDb = lookupOrDefault(input.mixLaneGainDbByLane, laneIndex, 0.0F),
            .muted = lookupOrDefault(input.mixLaneMutedByLane, laneIndex, false),
            .soloed = lookupOrDefault(input.mixLaneSoloByLane, laneIndex, false)
        });
    }

    state.clipEditorPlayheads.reserve(input.clipEditorPlayheads.size());
    for (auto it = input.clipEditorPlayheads.cbegin(); it != input.clipEditorPlayheads.cend(); ++it)
    {
        state.clipEditorPlayheads.emplace_back(it.key(), it.value());
    }
    std::sort(
        state.clipEditorPlayheads.begin(),
        state.clipEditorPlayheads.end(),
        [](const auto& left, const auto& right)
        {
            return left.first.toString(QUuid::WithoutBraces) < right.first.toString(QUuid::WithoutBraces);
        });

    return state;
}

bool ProjectSessionAdapter::validate(const dawg::project::ControllerState& state, QString* errorMessage)
{
    for (const auto& assetPath : state.audioPoolAssetPaths)
    {
        if (!assetPath.isEmpty() && !QFileInfo::exists(assetPath))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Project audio file is missing: %1").arg(assetPath);
            }
            return false;
        }
    }

    for (const auto& track : state.trackerState.tracks)
    {
        if (track.attachedAudio.has_value()
            && !track.attachedAudio->assetPath.isEmpty()
            && !QFileInfo::exists(track.attachedAudio->assetPath))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Attached audio file is missing: %1")
                    .arg(track.attachedAudio->assetPath);
            }
            return false;
        }
    }

    if (!state.videoPath.isEmpty() && !QFileInfo::exists(state.videoPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project video file is missing: %1").arg(state.videoPath);
        }
        return false;
    }

    if (state.videoPath.isEmpty() && !state.trackerState.tracks.empty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project contains nodes but no video file.");
        }
        return false;
    }

    return true;
}

ProjectSessionAdapter::RestorePayload ProjectSessionAdapter::buildRestorePayload(
    const dawg::project::ControllerState& state,
    const std::function<float(float)>& clampMixGainDb)
{
    RestorePayload payload;
    payload.audioPoolAssetPaths = state.audioPoolAssetPaths;
    payload.trackerState = state.trackerState;
    payload.selectedTrackIds = state.selectedTrackIds;
    payload.currentFrameIndex = state.currentFrameIndex;
    payload.motionTrackingEnabled = state.motionTrackingEnabled;
    payload.insertionFollowsPlayback = state.insertionFollowsPlayback;
    payload.fastPlaybackEnabled = state.fastPlaybackEnabled;
    payload.embeddedVideoAudioMuted = state.embeddedVideoAudioMuted;
    payload.loopStartFrame = state.loopStartFrame;
    payload.loopEndFrame = state.loopEndFrame;
    payload.masterMixGainDb = clampMixGainDb(state.masterMixGainDb);
    payload.masterMixMuted = state.masterMixMuted;

    for (const auto& lane : state.mixLanes)
    {
        if (lane.laneIndex < 0)
        {
            continue;
        }

        if (std::abs(lane.gainDb) >= 0.001F)
        {
            payload.mixLaneGainDbByLane[lane.laneIndex] = clampMixGainDb(lane.gainDb);
        }
        if (lane.muted)
        {
            payload.mixLaneMutedByLane[lane.laneIndex] = true;
        }
        if (lane.soloed)
        {
            payload.mixLaneSoloByLane[lane.laneIndex] = true;
        }
    }

    for (const auto& [trackId, playheadMs] : state.clipEditorPlayheads)
    {
        if (!trackId.isNull())
        {
            payload.clipEditorPlayheads.insert(trackId, playheadMs);
        }
    }

    return payload;
}

std::vector<QUuid> ProjectSessionAdapter::filterExistingSelection(
    const std::vector<QUuid>& selectedTrackIds,
    const std::function<bool(const QUuid&)>& trackExists)
{
    std::vector<QUuid> validSelection;
    validSelection.reserve(selectedTrackIds.size());
    for (const auto& selectedTrackId : selectedTrackIds)
    {
        if (trackExists(selectedTrackId))
        {
            validSelection.push_back(selectedTrackId);
        }
    }
    return validSelection;
}
