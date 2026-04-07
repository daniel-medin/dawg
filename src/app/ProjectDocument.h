#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QUuid>

#include "core/tracking/TrackTypes.h"
#include "ui/TimelineTypes.h"

namespace dawg::project
{
constexpr int kSchemaVersion = 6;
constexpr auto kProjectFileSuffix = ".dawg";

struct MixLaneState
{
    int laneIndex = 0;
    float gainDb = 0.0F;
    bool muted = false;
    bool soloed = false;
};

struct ControllerState
{
    QString videoPath;
    QString proxyVideoPath;
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
    std::vector<MixLaneState> mixLanes;
    std::vector<std::pair<QUuid, int>> trackAudioPlayheads;
};

struct UiState
{
    bool videoDetached = false;
    QByteArray detachedVideoWindowGeometry;
    bool timelineDetached = false;
    QByteArray detachedTimelineWindowGeometry;
    bool mixDetached = false;
    QByteArray detachedMixWindowGeometry;
    bool audioPoolDetached = false;
    QByteArray detachedAudioPoolWindowGeometry;
    bool timelineVisible = true;
    bool nodeEditorVisible = false;
    bool mixVisible = false;
    bool audioPoolVisible = false;
    bool audioPoolShowLength = true;
    bool audioPoolShowSize = true;
    bool showAllNodeNames = true;
    bool timelineClickSeeks = true;
    bool timelineThumbnailsVisible = true;
    bool useProxyVideo = false;
    int audioPoolPreferredWidth = 320;
    int timelinePreferredHeight = 148;
    int nodeEditorPreferredHeight = 260;
    int mixPreferredHeight = 368;
    std::vector<int> contentSplitterSizes;
    std::vector<int> mainVerticalSplitterSizes;
    QByteArray windowGeometry;
    bool windowMaximized = false;
};

struct Document
{
    QString name;
    ControllerState controller;
    UiState ui;
};

QString sanitizeProjectName(const QString& name);
QString projectFileNameForName(const QString& projectName);
std::optional<Document> loadDocument(const QString& projectFilePath, QString* errorMessage = nullptr);
bool saveDocument(const QString& projectFilePath, const Document& document, QString* errorMessage = nullptr);
}
