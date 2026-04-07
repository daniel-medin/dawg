#include "app/AudioPlaybackCoordinator.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <QHash>

#include "core/audio/AudioEngine.h"
#include "core/tracking/TrackTypes.h"
#include "ui/TimelineTypes.h"

namespace
{
constexpr float kSilentMixGainDb = -100.0F;

bool containsTrackId(const std::vector<QUuid>& trackIds, const QUuid& trackId)
{
    return std::find(trackIds.cbegin(), trackIds.cend(), trackId) != trackIds.cend();
}

int audioClipStartMs(const AudioAttachment& attachment)
{
    return std::max(0, attachment.clipStartMs);
}

int audioClipEndMs(const AudioAttachment& attachment, const int sourceDurationMs)
{
    if (sourceDurationMs <= 0)
    {
        return 0;
    }

    if (!attachment.clipEndMs.has_value())
    {
        return sourceDurationMs;
    }

    return std::clamp(*attachment.clipEndMs, audioClipStartMs(attachment) + 1, sourceDurationMs);
}

int audioClipDurationMs(const AudioAttachment& attachment, const int sourceDurationMs)
{
    return std::max(1, audioClipEndMs(attachment, sourceDurationMs) - audioClipStartMs(attachment));
}

int audioClipPlaybackOffsetMs(
    const AudioAttachment& attachment,
    const int sourceDurationMs,
    const int elapsedWithinNodeMs)
{
    const auto clipStartMs = audioClipStartMs(attachment);
    Q_UNUSED(sourceDurationMs);
    return clipStartMs + std::max(0, elapsedWithinNodeMs);
}

QHash<QUuid, int> laneByTrackId(const std::vector<TimelineTrackSpan>& spans)
{
    QHash<QUuid, int> lanes;
    lanes.reserve(static_cast<qsizetype>(spans.size()));
    for (const auto& span : spans)
    {
        lanes.insert(span.id, span.laneIndex);
    }
    return lanes;
}

bool anyLaneSoloed(const std::unordered_map<int, bool>& soloByLane)
{
    return std::any_of(
        soloByLane.begin(),
        soloByLane.end(),
        [](const auto& entry)
        {
            return entry.second;
        });
}

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

int trackEndFrame(const TrackPoint& track, const int totalFrames)
{
    const auto startFrame = std::max(0, track.startFrame);
    return track.endFrame.has_value()
        ? std::max(startFrame, *track.endFrame)
        : std::max(startFrame, totalFrames > 0 ? (totalFrames - 1) : startFrame);
}
}

AudioPlaybackCoordinator::AudioPlaybackCoordinator(AudioEngine& audioEngine)
    : m_audioEngine(audioEngine)
{
}

const QUuid& AudioPlaybackCoordinator::audioPoolPreviewTrackId() const
{
    return m_audioPoolPreviewTrackId;
}

const QUuid& AudioPlaybackCoordinator::clipPreviewTrackId() const
{
    return m_clipPreviewTrackId;
}

bool AudioPlaybackCoordinator::startAudioPoolPreview(const QString& filePath, bool* stateChanged)
{
    if (stateChanged)
    {
        *stateChanged = false;
    }

    if (filePath.isEmpty())
    {
        return false;
    }

    const auto wasPreviewing = m_audioEngine.isTrackPlaying(m_audioPoolPreviewTrackId);
    const auto previousAssetPath = m_audioPoolPreviewAssetPath;
    if (!m_audioEngine.playTrack(m_audioPoolPreviewTrackId, filePath))
    {
        return false;
    }

    m_audioEngine.setTrackGain(m_audioPoolPreviewTrackId, 0.0F);
    m_audioEngine.setTrackPan(m_audioPoolPreviewTrackId, 0.0F);
    m_audioPoolPreviewAssetPath = filePath;
    if (stateChanged)
    {
        *stateChanged = !wasPreviewing || previousAssetPath != filePath;
    }
    return true;
}

void AudioPlaybackCoordinator::stopAudioPoolPreview(bool* stateChanged)
{
    const auto wasPreviewing = m_audioEngine.isTrackPlaying(m_audioPoolPreviewTrackId);
    m_audioEngine.stopTrack(m_audioPoolPreviewTrackId);
    if (stateChanged)
    {
        *stateChanged = wasPreviewing || !m_audioPoolPreviewAssetPath.isEmpty();
    }
    m_audioPoolPreviewAssetPath.clear();
}

bool AudioPlaybackCoordinator::isAudioPoolPreviewPlaying() const
{
    return m_audioEngine.isTrackPlaying(m_audioPoolPreviewTrackId);
}

QString AudioPlaybackCoordinator::audioPoolPreviewAssetPath() const
{
    return m_audioPoolPreviewAssetPath;
}

bool AudioPlaybackCoordinator::isClipPreviewPlaying() const
{
    return m_audioEngine.isTrackPlaying(m_clipPreviewTrackId);
}

QUuid AudioPlaybackCoordinator::clipPreviewSourceTrackId() const
{
    return m_clipPreviewSourceTrackId;
}

std::optional<int> AudioPlaybackCoordinator::currentClipPreviewPlayheadMs() const
{
    if (m_clipPreviewSourceTrackId.isNull() || !isClipPreviewPlaying())
    {
        return std::nullopt;
    }

    const auto clipEndMs = std::max(m_clipPreviewClipStartMs + 1, m_clipPreviewClipEndMs);
    const auto elapsedMs = m_clipPreviewElapsedTimer.isValid()
        ? static_cast<int>(m_clipPreviewElapsedTimer.elapsed())
        : 0;
    return std::clamp(
        m_clipPreviewStartMs + std::max(0, elapsedMs),
        m_clipPreviewClipStartMs,
        clipEndMs - 1);
}

std::optional<int> AudioPlaybackCoordinator::playClipPreview(const ClipPreviewRequest& request)
{
    if (request.sourceTrackId.isNull()
        || request.assetPath.isEmpty()
        || request.clipEndMs <= request.clipStartMs)
    {
        return std::nullopt;
    }

    const auto previewStartMs = std::clamp(
        request.previewStartMs,
        request.clipStartMs,
        std::max(request.clipStartMs, request.clipEndMs - 1));
    if (!m_audioEngine.playTrack(m_clipPreviewTrackId, request.assetPath, previewStartMs))
    {
        return std::nullopt;
    }

    m_clipPreviewSourceTrackId = request.sourceTrackId;
    m_clipPreviewStartMs = previewStartMs;
    m_clipPreviewClipStartMs = request.clipStartMs;
    m_clipPreviewClipEndMs = request.clipEndMs;
    m_clipPreviewElapsedTimer.restart();
    m_audioEngine.setTrackGain(m_clipPreviewTrackId, request.gainDb);
    m_audioEngine.setTrackPan(m_clipPreviewTrackId, 0.0F);
    return std::max(1, request.clipEndMs - previewStartMs);
}

void AudioPlaybackCoordinator::setClipPreviewGain(const float gainDb)
{
    if (!isClipPreviewPlaying())
    {
        return;
    }

    m_audioEngine.setTrackGain(m_clipPreviewTrackId, gainDb);
}

void AudioPlaybackCoordinator::stopClipPreview()
{
    m_audioEngine.stopTrack(m_clipPreviewTrackId);
    m_clipPreviewSourceTrackId = {};
    m_clipPreviewStartMs = 0;
    m_clipPreviewClipStartMs = 0;
    m_clipPreviewClipEndMs = 0;
}

bool AudioPlaybackCoordinator::syncNodePreview(
    const std::vector<NodePreviewClip>& clips,
    const int nodeDurationMs,
    const int playheadMs,
    const bool forceRepositionActiveTracks)
{
    if (nodeDurationMs <= 0)
    {
        stopNodePreview();
        return false;
    }

    m_nodePreviewActive = true;
    m_nodePreviewClips = clips;
    std::vector<QUuid> previewTrackIds;
    std::vector<QUuid> activeTrackIds;
    previewTrackIds.reserve(clips.size());
    activeTrackIds.reserve(clips.size());
    const auto clampedPlayheadMs = std::clamp(playheadMs, 0, nodeDurationMs);
    bool anyClipStarted = false;

    for (const auto& clip : clips)
    {
        if (clip.previewTrackId.isNull()
            || clip.assetPath.isEmpty()
            || clip.clipEndMs <= clip.clipStartMs)
        {
            continue;
        }

        previewTrackIds.push_back(clip.previewTrackId);
        const auto elapsedWithinClipMs = clampedPlayheadMs - std::max(0, clip.laneOffsetMs);
        if (elapsedWithinClipMs < 0)
        {
            continue;
        }

        const auto clipDurationMs = std::max(1, clip.clipEndMs - clip.clipStartMs);
        if (!clip.loopEnabled && elapsedWithinClipMs >= clipDurationMs)
        {
            continue;
        }

        activeTrackIds.push_back(clip.previewTrackId);
        AudioEngine::TrackPlaybackOptions options;
        options.offsetMs = clip.loopEnabled
            ? clip.clipStartMs + (elapsedWithinClipMs % clipDurationMs)
            : clip.clipStartMs + elapsedWithinClipMs;
        options.clipStartMs = clip.clipStartMs;
        options.clipEndMs = clip.clipEndMs;
        options.loopEnabled = clip.loopEnabled;

        const auto wasActive = containsTrackId(m_nodePreviewActiveTrackIds, clip.previewTrackId);
        const auto shouldStartOrReposition = forceRepositionActiveTracks
            || !wasActive
            || !m_audioEngine.isTrackPlaying(clip.previewTrackId);
        if (shouldStartOrReposition
            && !m_audioEngine.playTrack(clip.previewTrackId, clip.assetPath, options))
        {
            activeTrackIds.pop_back();
            continue;
        }

        m_audioEngine.setTrackGain(clip.previewTrackId, clip.gainDb);
        m_audioEngine.setTrackPan(clip.previewTrackId, 0.0F);
        anyClipStarted = true;
    }

    for (const auto& previousTrackId : m_nodePreviewTrackIds)
    {
        if (!containsTrackId(previewTrackIds, previousTrackId))
        {
            m_audioEngine.stopTrack(previousTrackId);
        }
    }
    m_nodePreviewTrackIds = std::move(previewTrackIds);
    m_nodePreviewActiveTrackIds = std::move(activeTrackIds);
    return anyClipStarted || !m_nodePreviewTrackIds.empty() || clips.empty();
}

void AudioPlaybackCoordinator::stopNodePreview()
{
    for (const auto& trackId : m_nodePreviewTrackIds)
    {
        m_audioEngine.stopTrack(trackId);
    }
    m_nodePreviewClips.clear();
    m_nodePreviewTrackIds.clear();
    m_nodePreviewActiveTrackIds.clear();
    m_nodePreviewActive = false;
}

bool AudioPlaybackCoordinator::isNodePreviewPlaying() const
{
    return m_nodePreviewActive;
}

AudioEngine::StereoLevels AudioPlaybackCoordinator::nodePreviewStereoLevels() const
{
    AudioEngine::StereoLevels levels;
    for (const auto& trackId : m_nodePreviewTrackIds)
    {
        const auto trackLevels = m_audioEngine.trackStereoLevels(trackId);
        levels.left = std::max(levels.left, trackLevels.left);
        levels.right = std::max(levels.right, trackLevels.right);
    }
    return levels;
}

std::vector<AudioPlaybackCoordinator::NodePreviewLaneMeterState>
AudioPlaybackCoordinator::nodePreviewLaneMeterStates() const
{
    std::vector<NodePreviewLaneMeterState> states;
    if (!m_nodePreviewActive)
    {
        return states;
    }

    for (const auto& clip : m_nodePreviewClips)
    {
        if (clip.laneId.isEmpty()
            || clip.previewTrackId.isNull()
            || !containsTrackId(m_nodePreviewTrackIds, clip.previewTrackId))
        {
            continue;
        }

        auto stateIt = std::find_if(
            states.begin(),
            states.end(),
            [&clip](const NodePreviewLaneMeterState& state)
            {
                return state.laneId == clip.laneId;
            });
        if (stateIt == states.end())
        {
            states.push_back(NodePreviewLaneMeterState{
                .laneId = clip.laneId,
                .useStereoMeter = clip.useStereoMeter
            });
            stateIt = std::prev(states.end());
        }

        const auto trackLevels = m_audioEngine.trackStereoLevels(clip.previewTrackId);
        stateIt->meterLeftLevel = std::max(stateIt->meterLeftLevel, trackLevels.left);
        stateIt->meterRightLevel = std::max(stateIt->meterRightLevel, trackLevels.right);
        stateIt->meterLevel = std::max(
            stateIt->meterLevel,
            std::max(trackLevels.left, trackLevels.right));
        stateIt->useStereoMeter = stateIt->useStereoMeter || clip.useStereoMeter;
    }

    return states;
}

void AudioPlaybackCoordinator::applyLiveMixStateToCurrentPlayback(
    const PlaybackSyncRequest& request,
    const std::vector<TimelineTrackSpan>& spans,
    const std::vector<TrackPoint>& tracks,
    const std::unordered_map<int, float>& gainByLane,
    const std::unordered_map<int, bool>& mutedByLane,
    const std::unordered_map<int, bool>& soloByLane)
{
    if (!request.hasVideoLoaded)
    {
        return;
    }

    m_audioEngine.setMasterGain(request.masterMuted ? kSilentMixGainDb : request.masterGainDb);

    const auto lanes = laneByTrackId(spans);
    const auto anySoloed = anyLaneSoloed(soloByLane);
    if (!request.embeddedVideoAudioPath.isEmpty() && !request.embeddedVideoAudioMuted)
    {
        m_audioEngine.setTrackGain(request.embeddedVideoAudioTrackId, anySoloed ? kSilentMixGainDb : 0.0F);
    }

    for (const auto& track : tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }

        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = trackEndFrame(track, request.totalFrames);
        if (request.currentFrameIndex < startFrame || request.currentFrameIndex > endFrame)
        {
            continue;
        }

        const auto laneIt = lanes.find(track.id);
        const auto laneIndex = laneIt != lanes.end() ? laneIt.value() : 0;
        const auto laneAudible = !laneMuted(mutedByLane, laneIndex) && (!anySoloed || laneSoloed(soloByLane, laneIndex));
        const auto trackGainDb = laneAudible
            ? (track.attachedAudio->gainDb + laneGainDb(gainByLane, laneIndex))
            : kSilentMixGainDb;
        m_audioEngine.setTrackGain(track.id, trackGainDb);
    }
}

void AudioPlaybackCoordinator::syncAttachedAudioForCurrentFrame(
    const PlaybackSyncRequest& request,
    const std::vector<TimelineTrackSpan>& spans,
    const std::vector<TrackPoint>& tracks,
    const std::unordered_map<int, float>& gainByLane,
    const std::unordered_map<int, bool>& mutedByLane,
    const std::unordered_map<int, bool>& soloByLane,
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath,
    const std::function<double(int)>& frameTimestampSecondsForFrame)
{
    if (!request.hasVideoLoaded)
    {
        m_audioEngine.stopAll();
        return;
    }

    const auto currentTimestampSeconds = frameTimestampSecondsForFrame(request.currentFrameIndex);
    const auto currentOffsetMs =
        std::max(0, static_cast<int>(std::lround(currentTimestampSeconds * 1000.0)));
    m_audioEngine.setMasterGain(request.masterMuted ? kSilentMixGainDb : request.masterGainDb);

    const auto lanes = laneByTrackId(spans);
    const auto anySoloed = anyLaneSoloed(soloByLane);

    if (!request.embeddedVideoAudioPath.isEmpty() && !request.embeddedVideoAudioMuted)
    {
        m_audioEngine.playTrack(request.embeddedVideoAudioTrackId, request.embeddedVideoAudioPath, currentOffsetMs);
        m_audioEngine.setTrackGain(request.embeddedVideoAudioTrackId, anySoloed ? kSilentMixGainDb : 0.0F);
        m_audioEngine.setTrackPan(request.embeddedVideoAudioTrackId, 0.0F);
    }
    else
    {
        m_audioEngine.stopTrack(request.embeddedVideoAudioTrackId);
    }

    for (const auto& track : tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            m_audioEngine.stopTrack(track.id);
            continue;
        }

        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = trackEndFrame(track, request.totalFrames);
        if (request.currentFrameIndex < startFrame || request.currentFrameIndex > endFrame)
        {
            m_audioEngine.stopTrack(track.id);
            continue;
        }

        const auto startTimestampSeconds = frameTimestampSecondsForFrame(startFrame);
        const auto elapsedWithinNodeMs = std::max(
            0,
            static_cast<int>(std::lround((currentTimestampSeconds - startTimestampSeconds) * 1000.0)));
        const auto sourceDurationMs = audioDurationMsForPath(track.attachedAudio->assetPath).value_or(0);
        if (sourceDurationMs <= 0)
        {
            m_audioEngine.stopTrack(track.id);
            continue;
        }

        const auto clipStartMs = audioClipStartMs(*track.attachedAudio);
        const auto clipEndMs = audioClipEndMs(*track.attachedAudio, sourceDurationMs);
        const auto clipDurationMs = std::max(1, clipEndMs - clipStartMs);
        if (elapsedWithinNodeMs >= clipDurationMs)
        {
            m_audioEngine.stopTrack(track.id);
            continue;
        }

        AudioEngine::TrackPlaybackOptions playbackOptions;
        playbackOptions.offsetMs = audioClipPlaybackOffsetMs(
            *track.attachedAudio,
            sourceDurationMs,
            elapsedWithinNodeMs);
        playbackOptions.clipStartMs = clipStartMs;
        playbackOptions.clipEndMs = clipEndMs;
        playbackOptions.loopEnabled = false;
        m_audioEngine.playTrack(track.id, track.attachedAudio->assetPath, playbackOptions);

        const auto laneIt = lanes.find(track.id);
        const auto laneIndex = laneIt != lanes.end() ? laneIt.value() : 0;
        const auto laneAudible = !laneMuted(mutedByLane, laneIndex) && (!anySoloed || laneSoloed(soloByLane, laneIndex));
        const auto trackGainDb = laneAudible
            ? (track.attachedAudio->gainDb + laneGainDb(gainByLane, laneIndex))
            : kSilentMixGainDb;
        m_audioEngine.setTrackGain(track.id, trackGainDb);

        float pan = 0.0F;
        if (track.autoPanEnabled && request.currentFrameWidth > 1)
        {
            const auto samplePoint = track.interpolatedSampleAt(request.currentFrameIndex).value_or(QPointF{});
            const auto normalizedPan =
                ((samplePoint.x() / static_cast<double>(request.currentFrameWidth - 1)) * 2.0) - 1.0;
            pan = std::clamp(static_cast<float>(normalizedPan), -1.0F, 1.0F);
        }
        m_audioEngine.setTrackPan(track.id, pan);
    }
}

void AudioPlaybackCoordinator::reset()
{
    stopAudioPoolPreview();
    stopClipPreview();
    stopNodePreview();
}
