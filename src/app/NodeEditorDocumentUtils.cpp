#include "app/NodeEditorDocumentUtils.h"

#include <algorithm>

#include <QFileInfo>
#include <QVariantMap>

#include "core/audio/AudioDurationProbe.h"

namespace
{

QUuid uuidFromNodeDocumentId(const QString& id)
{
    auto cleaned = id.trimmed();
    if (cleaned.isEmpty())
    {
        return QUuid{};
    }
    if (!cleaned.startsWith(QLatin1Char('{')))
    {
        cleaned = QStringLiteral("{%1}").arg(cleaned);
    }
    return QUuid(cleaned);
}

int nodeAudioClipDurationMs(
    const dawg::node::AudioClipData& clip,
    const dawg::nodeeditor::AudioDurationFn& durationForPath)
{
    if (!clip.attachedAudio.has_value())
    {
        return 1;
    }

    const auto sourceDurationMs = !clip.attachedAudio->assetPath.isEmpty()
        ? (durationForPath
            ? durationForPath(clip.attachedAudio->assetPath)
            : dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath))
        : std::optional<int>{};
    const auto clipStartMs = std::clamp(
        clip.attachedAudio->clipStartMs,
        0,
        std::max(0, sourceDurationMs.value_or(clip.attachedAudio->clipEndMs.value_or(clip.attachedAudio->clipStartMs + 1)) - 1));
    const auto clipEndMs = std::clamp(
        clip.attachedAudio->clipEndMs.value_or(sourceDurationMs.value_or(clipStartMs + 1)),
        clipStartMs + 1,
        std::max(clipStartMs + 1, sourceDurationMs.value_or(clipStartMs + 1)));
    return std::max(1, clipEndMs - clipStartMs);
}

}

namespace dawg::nodeeditor
{

dawg::node::AudioClipData nodeAudioClipFromClipState(
    const AudioClipPreviewState& state,
    const QString& fallbackLabel)
{
    dawg::node::AudioClipData clip;
    clip.label = state.label.trimmed().isEmpty() ? fallbackLabel : state.label.trimmed();
    if (state.hasAttachedAudio)
    {
        clip.attachedAudio = AudioAttachment{
            .assetPath = state.assetPath,
            .gainDb = state.gainDb,
            .clipStartMs = state.clipStartMs,
            .clipEndMs = state.clipEndMs,
            .loopEnabled = false
        };
    }
    return clip;
}

std::optional<AudioClipPreviewState> clipStateFromNodeAudioClip(
    const dawg::node::AudioClipData& clip,
    const QString& fallbackLabel,
    const AudioDurationFn& durationForPath)
{
    if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
    {
        return std::nullopt;
    }

    const auto durationMs = durationForPath
        ? durationForPath(clip.attachedAudio->assetPath)
        : dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 0)
    {
        return std::nullopt;
    }

    const auto clipStartMs = std::clamp(clip.attachedAudio->clipStartMs, 0, *durationMs);
    const auto clipEndMs = std::clamp(
        clip.attachedAudio->clipEndMs.value_or(*durationMs),
        clipStartMs + 1,
        *durationMs);
    AudioClipPreviewState state;
    state.label = clip.label.trimmed().isEmpty() ? fallbackLabel : clip.label.trimmed();
    state.assetPath = clip.attachedAudio->assetPath;
    state.clipStartMs = clipStartMs;
    state.clipEndMs = clipEndMs;
    state.sourceDurationMs = *durationMs;
    state.playheadMs = clipStartMs;
    state.gainDb = clip.attachedAudio->gainDb;
    state.hasAttachedAudio = true;
    state.loopEnabled = false;
    return state;
}

QVariantList nodeTrackItemsFromDocument(
    const dawg::node::Document& document,
    const int nodeDurationMs,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath)
{
    QVariantList items;
    const auto safeNodeDurationMs = std::max(1, nodeDurationMs);
    for (int laneIndex = 0; laneIndex < static_cast<int>(document.node.lanes.size()); ++laneIndex)
    {
        const auto& lane = document.node.lanes[static_cast<std::size_t>(laneIndex)];
        const auto title = lane.label.trimmed().isEmpty()
            ? QStringLiteral("Lane %1").arg(laneIndex + 1)
            : lane.label.trimmed();
        const auto clipCount = static_cast<int>(lane.audioClips.size());
        const auto subtitle = clipCount == 0
            ? QStringLiteral("Empty lane")
            : QStringLiteral("%1 audio clip(s)").arg(clipCount);

        QVariantList clipItems;
        QVariantMap waveformState;
        bool laneUsesStereoMeter = false;
        for (int clipIndex = 0; clipIndex < static_cast<int>(lane.audioClips.size()); ++clipIndex)
        {
            const auto& clip = lane.audioClips[static_cast<std::size_t>(clipIndex)];
            if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
            {
                continue;
            }

            if (!laneUsesStereoMeter && channelCountForPath)
            {
                const auto channelCount = channelCountForPath(clip.attachedAudio->assetPath);
                laneUsesStereoMeter = channelCount.has_value() && *channelCount > 1;
            }
            const auto clipTitle = clip.label.trimmed().isEmpty()
                ? QStringLiteral("Audio Clip %1").arg(clipIndex + 1)
                : clip.label.trimmed();
            const auto clipFileName = QFileInfo(clip.attachedAudio->assetPath).fileName();
            QVariantMap clipWaveformState;
            int clipDurationMs = 1;
            const auto clipOffsetRatio = std::clamp(
                static_cast<double>(clip.laneOffsetMs) / static_cast<double>(safeNodeDurationMs),
                0.0,
                1.0);
            double clipWidthRatio = 0.0;
            const auto durationMs = durationForPath
                ? durationForPath(clip.attachedAudio->assetPath)
                : dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
            if (durationMs.has_value() && *durationMs > 0)
            {
                const auto clipStartMs = std::clamp(
                    clip.attachedAudio->clipStartMs,
                    0,
                    std::max(0, *durationMs - 1));
                const auto clipEndMs = std::clamp(
                    clip.attachedAudio->clipEndMs.value_or(*durationMs),
                    clipStartMs + 1,
                    *durationMs);
                clipDurationMs = std::max(1, clipEndMs - clipStartMs);
                clipWidthRatio = std::clamp(
                    static_cast<double>(clipDurationMs) / static_cast<double>(safeNodeDurationMs),
                    0.0,
                    1.0 - clipOffsetRatio);
                clipWaveformState = QVariantMap{
                    {QStringLiteral("label"), clipTitle},
                    {QStringLiteral("assetPath"), clip.attachedAudio->assetPath},
                    {QStringLiteral("clipStartMs"), clipStartMs},
                    {QStringLiteral("clipEndMs"), clipEndMs},
                    {QStringLiteral("sourceDurationMs"), *durationMs},
                    {QStringLiteral("playheadMs"), clipStartMs},
                    {QStringLiteral("gainDb"), clip.attachedAudio->gainDb},
                    {QStringLiteral("hasAttachedAudio"), true},
                    {QStringLiteral("loopEnabled"), false}
                };
                if (waveformState.isEmpty())
                {
                    waveformState = clipWaveformState;
                }
            }
            clipItems.push_back(QVariantMap{
                {QStringLiteral("clipId"), clip.id},
                {QStringLiteral("title"), clipTitle},
                {QStringLiteral("subtitle"), clipFileName.isEmpty() ? QStringLiteral("Embedded audio") : clipFileName},
                {QStringLiteral("laneOffsetMs"), clip.laneOffsetMs},
                {QStringLiteral("clipDurationMs"), clipDurationMs},
                {QStringLiteral("clipOffsetRatio"), clipOffsetRatio},
                {QStringLiteral("clipWidthRatio"), clipWidthRatio},
                {QStringLiteral("clipSourceStartMs"), clipWaveformState.value(QStringLiteral("clipStartMs"), 0)},
                {QStringLiteral("clipSourceEndMs"), clipWaveformState.value(QStringLiteral("clipEndMs"), clipDurationMs)},
                {QStringLiteral("clipSourceDurationMs"), clipWaveformState.value(QStringLiteral("sourceDurationMs"), clipDurationMs)},
                {QStringLiteral("hasWaveform"), !clipWaveformState.isEmpty()},
                {QStringLiteral("waveformState"), clipWaveformState}
            });
        }

        items.push_back(QVariantMap{
            {QStringLiteral("laneId"), lane.id},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("primary"), laneIndex == 0},
            {QStringLiteral("muted"), lane.muted},
            {QStringLiteral("soloed"), lane.soloed},
            {QStringLiteral("useStereoMeter"), laneUsesStereoMeter},
            {QStringLiteral("clipCount"), clipCount},
            {QStringLiteral("clips"), clipItems},
            {QStringLiteral("hasWaveform"), !waveformState.isEmpty()},
            {QStringLiteral("waveformState"), waveformState}
        });
    }
    return items;
}

std::vector<AudioPlaybackCoordinator::NodePreviewClip> nodePreviewClipsFromDocument(
    const dawg::node::Document& document,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath)
{
    std::vector<AudioPlaybackCoordinator::NodePreviewClip> clips;
    const auto anyLaneSoloed = std::any_of(
        document.node.lanes.cbegin(),
        document.node.lanes.cend(),
        [](const dawg::node::LaneData& lane)
        {
            return lane.soloed;
        });
    for (const auto& lane : document.node.lanes)
    {
        if (lane.muted || (anyLaneSoloed && !lane.soloed))
        {
            continue;
        }

        for (const auto& clip : lane.audioClips)
        {
            if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
            {
                continue;
            }

            const auto durationMs = durationForPath
                ? durationForPath(clip.attachedAudio->assetPath)
                : dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
            if (!durationMs.has_value() || *durationMs <= 0)
            {
                continue;
            }

            const auto clipStartMs = std::clamp(
                clip.attachedAudio->clipStartMs,
                0,
                std::max(0, *durationMs - 1));
            const auto clipEndMs = std::clamp(
                clip.attachedAudio->clipEndMs.value_or(*durationMs),
                clipStartMs + 1,
                *durationMs);
            const auto previewTrackId = uuidFromNodeDocumentId(clip.id);
            if (previewTrackId.isNull())
            {
                continue;
            }
            const auto channelCount = channelCountForPath
                ? channelCountForPath(clip.attachedAudio->assetPath)
                : std::optional<int>{};

            clips.push_back(AudioPlaybackCoordinator::NodePreviewClip{
                .previewTrackId = previewTrackId,
                .laneId = lane.id,
                .assetPath = clip.attachedAudio->assetPath,
                .laneOffsetMs = clip.laneOffsetMs,
                .clipStartMs = clipStartMs,
                .clipEndMs = clipEndMs,
                .gainDb = clip.attachedAudio->gainDb,
                .loopEnabled = false,
                .useStereoMeter = channelCount.has_value() && *channelCount > 1
            });
        }
    }
    return clips;
}

int clampedNodeAudioClipOffsetMs(
    const dawg::node::AudioClipData& clip,
    const int desiredOffsetMs,
    const int nodeDurationMs,
    const AudioDurationFn& durationForPath)
{
    const auto maxOffsetMs = std::max(0, nodeDurationMs - nodeAudioClipDurationMs(clip, durationForPath));
    return std::clamp(desiredOffsetMs, 0, maxOffsetMs);
}

ViewStateBuildResult buildNodeEditorViewState(
    const QString& nodeDocumentPath,
    const QString& selectedNodeLabel,
    const AudioClipPreviewState& nodeTimelineState,
    const int nodeDurationMs,
    const AudioDurationFn& durationForPath,
    const AudioChannelCountFn& channelCountForPath)
{
    ViewStateBuildResult result;
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        return result;
    }

    QString errorMessage;
    const auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
    if (!nodeDocument.has_value())
    {
        return result;
    }

    const auto savedNodeLabel = nodeDocument->node.label.trimmed().isEmpty()
        ? nodeDocument->name.trimmed()
        : nodeDocument->node.label.trimmed();
    result.savedLabelDiffers = savedNodeLabel != selectedNodeLabel.trimmed();
    result.nodeTrackItems = nodeTrackItemsFromDocument(
        *nodeDocument,
        nodeDurationMs,
        durationForPath,
        channelCountForPath);

    for (const auto& lane : nodeDocument->node.lanes)
    {
        for (const auto& clip : lane.audioClips)
        {
            if (const auto previewState = clipStateFromNodeAudioClip(
                    clip,
                    nodeDocument->node.label,
                    durationForPath);
                previewState.has_value())
            {
                result.nodeEditorState = previewState;
                result.nodeEditorState->trackId = nodeTimelineState.trackId;
                result.nodeEditorState->nodeStartFrame = nodeTimelineState.nodeStartFrame;
                result.nodeEditorState->nodeEndFrame = nodeTimelineState.nodeEndFrame;
                result.nodeEditorState->loopEnabled = false;
                return result;
            }
        }
    }

    return result;
}

}
