#include "app/ClipEditorSession.h"

#include <algorithm>
#include <cmath>

#include "app/AudioPlaybackCoordinator.h"
#include "core/audio/AudioEngine.h"
#include "core/tracking/MotionTracker.h"
#include "core/tracking/TrackTypes.h"

namespace
{
constexpr float kMinMixGainDb = -100.0F;
constexpr float kMaxMixGainDb = 12.0F;

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
    const auto clipDurationMs = audioClipDurationMs(attachment, sourceDurationMs);
    if (attachment.loopEnabled && clipDurationMs > 0)
    {
        return clipStartMs + (std::max(0, elapsedWithinNodeMs) % clipDurationMs);
    }

    return clipStartMs + std::max(0, elapsedWithinNodeMs);
}

int trackEndFrame(const TrackPoint& track, const int totalFrames)
{
    const auto startFrame = std::max(0, track.startFrame);
    return track.endFrame.has_value()
        ? std::max(startFrame, *track.endFrame)
        : std::max(startFrame, totalFrames > 0 ? (totalFrames - 1) : startFrame);
}
}

ClipEditorSession::ClipEditorSession(
    MotionTracker& tracker,
    AudioEngine& audioEngine,
    AudioPlaybackCoordinator& audioPlaybackCoordinator,
    QTimer& previewStopTimer)
    : m_tracker(tracker)
    , m_audioEngine(audioEngine)
    , m_audioPlaybackCoordinator(audioPlaybackCoordinator)
    , m_previewStopTimer(previewStopTimer)
{
}

void ClipEditorSession::reset()
{
    m_playheadMsByTrack.clear();
    m_previewStopTimer.stop();
}

const QHash<QUuid, int>& ClipEditorSession::playheads() const
{
    return m_playheadMsByTrack;
}

void ClipEditorSession::setPlayheads(const QHash<QUuid, int>& playheads)
{
    m_playheadMsByTrack = playheads;
}

bool ClipEditorSession::setSelectedTrackClipPlayheadMs(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const int playheadMs,
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath)
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    if (!track)
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMsForPath(track->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    const auto clipStartMs = audioClipStartMs(*track->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*track->attachedAudio, *sourceDurationMs);
    const auto clampedPlayheadMs = std::clamp(playheadMs, 0, std::max(0, *sourceDurationMs - 1));
    const auto currentIt = m_playheadMsByTrack.constFind(selectedTrackId);
    const auto changed = currentIt == m_playheadMsByTrack.cend() || currentIt.value() != clampedPlayheadMs;
    if (!changed
        && !(m_audioPlaybackCoordinator.clipPreviewSourceTrackId() == selectedTrackId
            && m_audioPlaybackCoordinator.isClipPreviewPlaying()))
    {
        return false;
    }

    m_playheadMsByTrack.insert(selectedTrackId, clampedPlayheadMs);
    if (m_audioPlaybackCoordinator.clipPreviewSourceTrackId() != selectedTrackId
        || !m_audioPlaybackCoordinator.isClipPreviewPlaying())
    {
        return changed;
    }

    const auto previewStartMs = std::clamp(
        clampedPlayheadMs,
        clipStartMs,
        std::max(clipStartMs, clipEndMs - 1));
    const auto previewDurationMs = restartPreview(
        selectedTrackId,
        track->attachedAudio->assetPath,
        previewStartMs,
        clipStartMs,
        clipEndMs,
        track->attachedAudio->gainDb);
    return changed || previewDurationMs.has_value();
}

bool ClipEditorSession::setSelectedTrackAudioGainDb(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const float gainDb,
    const bool timelinePlaying,
    const std::function<void()>& applyLiveMixStateToCurrentPlayback)
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    if (!track)
    {
        return false;
    }

    const auto clampedGainDb = clampGainDb(gainDb);
    if (std::abs(track->attachedAudio->gainDb - clampedGainDb) < 0.001F)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioGainDb(selectedTrackId, clampedGainDb))
    {
        return false;
    }

    if (m_audioPlaybackCoordinator.clipPreviewSourceTrackId() == selectedTrackId
        && m_audioPlaybackCoordinator.isClipPreviewPlaying())
    {
        m_audioPlaybackCoordinator.setClipPreviewGain(clampedGainDb);
    }

    if (timelinePlaying)
    {
        applyLiveMixStateToCurrentPlayback();
    }

    return true;
}

bool ClipEditorSession::setSelectedTrackLoopEnabled(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const bool enabled,
    const bool timelinePlaying,
    const std::function<void()>& syncAttachedAudioForCurrentFrame)
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    if (!track)
    {
        return false;
    }

    if (track->attachedAudio->loopEnabled == enabled)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioLoopEnabled(selectedTrackId, enabled))
    {
        return false;
    }

    if (timelinePlaying)
    {
        syncAttachedAudioForCurrentFrame();
    }

    return true;
}

bool ClipEditorSession::startSelectedTrackClipPreview(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const bool timelinePlaying,
    const std::function<void(bool)>& pausePlayback,
    const std::function<void()>& stopAudioPoolPreview,
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath)
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    if (!track)
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMsForPath(track->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    if (timelinePlaying)
    {
        pausePlayback(false);
    }

    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();

    const auto clipStartMs = audioClipStartMs(*track->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*track->attachedAudio, *sourceDurationMs);
    const auto previewStartMs = std::clamp(
        m_playheadMsByTrack.value(track->id, clipStartMs),
        clipStartMs,
        std::max(clipStartMs, clipEndMs - 1));
    const auto previewDurationMs = restartPreview(
        track->id,
        track->attachedAudio->assetPath,
        previewStartMs,
        clipStartMs,
        clipEndMs,
        track->attachedAudio->gainDb);
    if (!previewDurationMs.has_value())
    {
        return false;
    }

    m_playheadMsByTrack.insert(track->id, previewStartMs);
    return true;
}

void ClipEditorSession::handlePreviewTimeout(
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath)
{
    const auto previewSourceTrackId = m_audioPlaybackCoordinator.clipPreviewSourceTrackId();
    if (previewSourceTrackId.isNull())
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto* track = findTrackWithAudio(previewSourceTrackId);
    if (!track)
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto sourceDurationMs = audioDurationMsForPath(track->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto clipStartMs = audioClipStartMs(*track->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*track->attachedAudio, *sourceDurationMs);
    if (!track->attachedAudio->loopEnabled)
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto previewDurationMs = restartPreview(
        track->id,
        track->attachedAudio->assetPath,
        clipStartMs,
        clipStartMs,
        clipEndMs,
        track->attachedAudio->gainDb);
    if (!previewDurationMs.has_value())
    {
        stopSelectedTrackClipPreview();
        return;
    }

    m_playheadMsByTrack.insert(track->id, clipStartMs);
}

void ClipEditorSession::stopSelectedTrackClipPreview()
{
    const auto previewSourceTrackId = m_audioPlaybackCoordinator.clipPreviewSourceTrackId();
    if (!previewSourceTrackId.isNull())
    {
        if (const auto playheadMs = currentSelectedTrackClipPreviewPlayheadMs(); playheadMs.has_value())
        {
            m_playheadMsByTrack.insert(previewSourceTrackId, *playheadMs);
        }
    }

    m_previewStopTimer.stop();
    m_audioPlaybackCoordinator.stopClipPreview();
}

bool ClipEditorSession::setSelectedTrackClipRangeMs(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const int clipStartMs,
    const int clipEndMs,
    const bool timelinePlaying,
    const std::function<void()>& syncAttachedAudioForCurrentFrame,
    const std::function<void()>& refreshOverlays,
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath)
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    if (!track)
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMsForPath(track->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    const auto clampedClipStartMs = std::clamp(clipStartMs, 0, std::max(0, *sourceDurationMs - 1));
    const auto clampedClipEndMs = std::clamp(clipEndMs, clampedClipStartMs + 1, *sourceDurationMs);
    const auto currentClipStartMs = audioClipStartMs(*track->attachedAudio);
    const auto currentClipEndMs = audioClipEndMs(*track->attachedAudio, *sourceDurationMs);
    const auto previewWasPlaying =
        m_audioPlaybackCoordinator.clipPreviewSourceTrackId() == selectedTrackId
        && m_audioPlaybackCoordinator.isClipPreviewPlaying();
    const auto playheadBeforeChange =
        previewWasPlaying
            ? currentSelectedTrackClipPreviewPlayheadMs().value_or(
                m_playheadMsByTrack.value(selectedTrackId, currentClipStartMs))
            : m_playheadMsByTrack.value(selectedTrackId, currentClipStartMs);
    if (clampedClipStartMs == currentClipStartMs && clampedClipEndMs == currentClipEndMs)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioClipRange(selectedTrackId, clampedClipStartMs, clampedClipEndMs))
    {
        return false;
    }

    const auto storedPlayheadMs = std::clamp(playheadBeforeChange, 0, std::max(0, *sourceDurationMs - 1));
    if (previewWasPlaying)
    {
        const auto previewPlayheadMs = std::clamp(
            storedPlayheadMs,
            clampedClipStartMs,
            std::max(clampedClipStartMs, clampedClipEndMs - 1));
        static_cast<void>(setSelectedTrackClipPlayheadMs(
            hasVideoLoaded,
            selectedTrackId,
            previewPlayheadMs,
            audioDurationMsForPath));
    }
    else
    {
        m_playheadMsByTrack.insert(selectedTrackId, storedPlayheadMs);
    }

    refreshOverlays();
    if (timelinePlaying)
    {
        syncAttachedAudioForCurrentFrame();
    }

    return true;
}

bool ClipEditorSession::selectedTrackLoopEnabled(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId) const
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return false;
    }

    const auto* track = findTrackWithAudio(selectedTrackId);
    return track != nullptr && track->attachedAudio->loopEnabled;
}

std::optional<ClipEditorState> ClipEditorSession::selectedClipEditorState(
    const bool hasVideoLoaded,
    const QUuid& selectedTrackId,
    const int currentFrameIndex,
    const int totalFrames,
    const bool timelinePlaying,
    const std::function<std::optional<int>(const QString&)>& audioDurationMsForPath,
    const std::function<double(int)>& frameTimestampSecondsForFrame) const
{
    if (!hasVideoLoaded || selectedTrackId.isNull())
    {
        return std::nullopt;
    }

    const auto* track = findTrack(selectedTrackId);
    if (!track)
    {
        return std::nullopt;
    }

    ClipEditorState state;
    state.trackId = track->id;
    state.label = track->label;
    state.color = track->color;
    state.nodeStartFrame = std::max(0, track->startFrame);
    state.nodeEndFrame = trackEndFrame(*track, totalFrames);
    state.hasAttachedAudio = track->attachedAudio.has_value();
    if (!track->attachedAudio.has_value())
    {
        return state;
    }

    state.assetPath = track->attachedAudio->assetPath;
    const auto sourceDurationMs = audioDurationMsForPath(track->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return state;
    }

    state.sourceDurationMs = *sourceDurationMs;
    state.clipStartMs = audioClipStartMs(*track->attachedAudio);
    state.clipEndMs = audioClipEndMs(*track->attachedAudio, *sourceDurationMs);
    state.gainDb = track->attachedAudio->gainDb;
    state.loopEnabled = track->attachedAudio->loopEnabled;
    if (m_audioPlaybackCoordinator.clipPreviewSourceTrackId() == track->id
        && m_audioPlaybackCoordinator.isClipPreviewPlaying())
    {
        state.level = m_audioEngine.trackLevel(m_audioPlaybackCoordinator.clipPreviewTrackId());
    }
    else
    {
        state.level = m_audioEngine.trackLevel(track->id);
    }

    auto playheadMs = m_playheadMsByTrack.value(track->id, state.clipStartMs);
    if (const auto previewPlayheadMs = currentSelectedTrackClipPreviewPlayheadMs();
        previewPlayheadMs.has_value() && m_audioPlaybackCoordinator.clipPreviewSourceTrackId() == track->id)
    {
        playheadMs = *previewPlayheadMs;
    }
    else if (timelinePlaying
        && currentFrameIndex >= state.nodeStartFrame
        && currentFrameIndex <= state.nodeEndFrame)
    {
        const auto elapsedSeconds =
            frameTimestampSecondsForFrame(currentFrameIndex) - frameTimestampSecondsForFrame(state.nodeStartFrame);
        const auto elapsedWithinNodeMs = std::max(0, static_cast<int>(std::lround(elapsedSeconds * 1000.0)));
        playheadMs = audioClipPlaybackOffsetMs(*track->attachedAudio, *sourceDurationMs, elapsedWithinNodeMs);
    }
    else if (!m_playheadMsByTrack.contains(track->id)
        && currentFrameIndex >= state.nodeStartFrame
        && currentFrameIndex <= state.nodeEndFrame)
    {
        const auto elapsedSeconds =
            frameTimestampSecondsForFrame(currentFrameIndex) - frameTimestampSecondsForFrame(state.nodeStartFrame);
        playheadMs = state.clipStartMs + std::max(0, static_cast<int>(std::lround(elapsedSeconds * 1000.0)));
    }

    state.playheadMs = std::clamp(playheadMs, 0, std::max(0, state.sourceDurationMs - 1));
    return state;
}

std::optional<int> ClipEditorSession::currentSelectedTrackClipPreviewPlayheadMs() const
{
    return m_audioPlaybackCoordinator.currentClipPreviewPlayheadMs();
}

float ClipEditorSession::clampGainDb(const float gainDb)
{
    return std::clamp(gainDb, kMinMixGainDb, kMaxMixGainDb);
}

const TrackPoint* ClipEditorSession::findTrack(const QUuid& trackId) const
{
    if (trackId.isNull())
    {
        return nullptr;
    }

    const auto& tracks = m_tracker.tracks();
    const auto trackIt = std::find_if(
        tracks.cbegin(),
        tracks.cend(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });
    return trackIt != tracks.cend() ? &(*trackIt) : nullptr;
}

const TrackPoint* ClipEditorSession::findTrackWithAudio(const QUuid& trackId) const
{
    const auto* track = findTrack(trackId);
    return track != nullptr && track->attachedAudio.has_value() ? track : nullptr;
}

std::optional<int> ClipEditorSession::restartPreview(
    const QUuid& trackId,
    const QString& assetPath,
    const int previewStartMs,
    const int clipStartMs,
    const int clipEndMs,
    const float gainDb)
{
    const auto previewDurationMs = m_audioPlaybackCoordinator.playClipPreview({
        .sourceTrackId = trackId,
        .assetPath = assetPath,
        .previewStartMs = previewStartMs,
        .clipStartMs = clipStartMs,
        .clipEndMs = clipEndMs,
        .gainDb = gainDb
    });
    if (!previewDurationMs.has_value())
    {
        return std::nullopt;
    }

    m_previewStopTimer.start(*previewDurationMs);
    return previewDurationMs;
}
