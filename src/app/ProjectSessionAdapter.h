#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QHash>
#include <QString>
#include <QUuid>

#include "app/ProjectDocument.h"

class AudioPoolService;
class MotionTracker;

class ProjectSessionAdapter
{
public:
    struct SnapshotInput
    {
        const QString& videoPath;
        const AudioPoolService& audioPool;
        const MotionTracker& tracker;
        const std::vector<QUuid>& selectedTrackIds;
        int currentFrameIndex = 0;
        bool motionTrackingEnabled = false;
        bool insertionFollowsPlayback = false;
        bool fastPlaybackEnabled = false;
        bool embeddedVideoAudioMuted = true;
        std::vector<TimelineLoopRange> loopRanges;
        float masterMixGainDb = 0.0F;
        bool masterMixMuted = false;
        bool mixSoloXorMode = false;
        const std::unordered_map<int, float>& mixLaneGainDbByLane;
        const std::unordered_map<int, bool>& mixLaneMutedByLane;
        const std::unordered_map<int, bool>& mixLaneSoloByLane;
        const QHash<QUuid, int>& trackAudioPlayheads;
    };

    struct RestorePayload
    {
        std::vector<QString> audioPoolAssetPaths;
        MotionTrackerState trackerState;
        std::vector<QUuid> selectedTrackIds;
        int currentFrameIndex = 0;
        bool motionTrackingEnabled = false;
        bool insertionFollowsPlayback = false;
        bool fastPlaybackEnabled = false;
        bool embeddedVideoAudioMuted = true;
        std::vector<TimelineLoopRange> loopRanges;
        float masterMixGainDb = 0.0F;
        bool masterMixMuted = false;
        bool mixSoloXorMode = false;
        std::unordered_map<int, float> mixLaneGainDbByLane;
        std::unordered_map<int, bool> mixLaneMutedByLane;
        std::unordered_map<int, bool> mixLaneSoloByLane;
        QHash<QUuid, int> trackAudioPlayheads;
    };

    [[nodiscard]] static dawg::project::ControllerState snapshot(const SnapshotInput& input);
    static bool validate(const dawg::project::ControllerState& state, QString* errorMessage = nullptr);
    [[nodiscard]] static RestorePayload buildRestorePayload(
        const dawg::project::ControllerState& state,
        const std::function<float(float)>& clampMixGainDb);
    [[nodiscard]] static std::vector<QUuid> filterExistingSelection(
        const std::vector<QUuid>& selectedTrackIds,
        const std::function<bool(const QUuid&)>& trackExists);
};
