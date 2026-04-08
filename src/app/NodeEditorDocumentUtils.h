#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QVariantList>

#include "app/AudioPlaybackCoordinator.h"
#include "app/NodeDocument.h"
#include "ui/AudioClipPreviewTypes.h"

namespace dawg::nodeeditor
{

using AudioDurationFn = std::function<std::optional<int>(const QString&)>;
using AudioChannelCountFn = std::function<std::optional<int>(const QString&)>;

struct ViewStateBuildResult
{
    std::optional<AudioClipPreviewState> nodeEditorState;
    QVariantList nodeTrackItems;
    bool savedLabelDiffers = false;
};

[[nodiscard]] dawg::node::AudioClipData nodeAudioClipFromClipState(
    const AudioClipPreviewState& state,
    const QString& fallbackLabel);

[[nodiscard]] std::optional<AudioClipPreviewState> clipStateFromNodeAudioClip(
    const dawg::node::AudioClipData& clip,
    const QString& fallbackLabel,
    const AudioDurationFn& durationForPath);

[[nodiscard]] QVariantList nodeTrackItemsFromDocument(
    const dawg::node::Document& document,
    int nodeDurationMs,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath);

[[nodiscard]] std::vector<AudioPlaybackCoordinator::NodePreviewClip> nodePreviewClipsFromDocument(
    const dawg::node::Document& document,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath);

[[nodiscard]] int clampedNodeAudioClipOffsetMs(
    const dawg::node::AudioClipData& clip,
    int desiredOffsetMs,
    int nodeDurationMs,
    const AudioDurationFn& durationForPath = {});

[[nodiscard]] ViewStateBuildResult buildNodeEditorViewState(
    const QString& nodeDocumentPath,
    const QString& selectedNodeLabel,
    const AudioClipPreviewState& nodeTimelineState,
    int nodeDurationMs,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath);

}
