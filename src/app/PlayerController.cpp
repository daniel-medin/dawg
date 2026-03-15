#include "app/PlayerController.h"

#include <algorithm>
#include <cmath>

#include <QFileInfo>
#include <QElapsedTimer>
#include <QHash>

#include "core/audio/VideoAudioExtractor.h"

namespace
{
constexpr float kMinMixGainDb = -100.0F;
constexpr float kMaxMixGainDb = 12.0F;
constexpr float kSilentMixGainDb = -100.0F;

float clampMixGainDb(const float gainDb)
{
    return std::clamp(gainDb, kMinMixGainDb, kMaxMixGainDb);
}

int trackAnchorFrame(const TrackPoint& track)
{
    if (track.seedFrameIndex >= 0)
    {
        return track.seedFrameIndex;
    }

    if (!track.samples.empty())
    {
        return track.samples.begin()->first;
    }

    return std::max(0, track.startFrame);
}

QPointF trackAnchorPoint(const TrackPoint& track)
{
    const auto anchorFrame = trackAnchorFrame(track);
    if (track.hasSample(anchorFrame))
    {
        return track.sampleAt(anchorFrame);
    }

    if (!track.samples.empty())
    {
        return track.samples.begin()->second;
    }

    return QPointF{};
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
    const auto clipDurationMs = audioClipDurationMs(attachment, sourceDurationMs);
    if (attachment.loopEnabled && clipDurationMs > 0)
    {
        return clipStartMs + (std::max(0, elapsedWithinNodeMs) % clipDurationMs);
    }

    return clipStartMs + std::max(0, elapsedWithinNodeMs);
}
}

PlayerController::PlayerController(QObject* parent)
    : QObject(parent)
    , m_transport(this)
    , m_audioEngine(AudioEngine::create(this))
{
    connect(&m_transport, &TransportController::playbackAdvanceRequested, this, &PlayerController::advancePlayback);
    connect(&m_transport, &TransportController::playbackStateChanged, this, &PlayerController::playbackStateChanged);
    connect(
        &m_transport,
        &TransportController::insertionFollowsPlaybackChanged,
        this,
        &PlayerController::insertionFollowsPlaybackChanged);
    connect(m_audioEngine.get(), &AudioEngine::statusChanged, this, &PlayerController::statusChanged);
    m_audioEngine->setMasterGain(m_masterMixGainDb);
    m_selectionFadeTimer.setInterval(30);
    connect(
        &m_selectionFadeTimer,
        &QTimer::timeout,
        this,
        &PlayerController::advanceSelectionFade);
    m_clipEditorPreviewStopTimer.setSingleShot(true);
    connect(&m_clipEditorPreviewStopTimer, &QTimer::timeout, this, &PlayerController::handleClipEditorPreviewTimeout);
}

void PlayerController::clearProjectStateAfterMediaStop()
{
    m_tracker.reset();
    m_currentOverlays.clear();
    m_selectedTrackIds.clear();
    m_selectedTrackId = {};
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();
    m_audioPool.clear();
    m_copiedTracks.clear();
    m_undoTrackerState.reset();
    m_undoSelectedTrackIds.clear();
    m_redoTrackerState.reset();
    m_redoSelectedTrackIds.clear();
    m_loopStartFrame.reset();
    m_loopEndFrame.reset();
    m_masterMixGainDb = 0.0F;
    m_masterMixMuted = false;
    m_mixLaneGainDbByLane.clear();
    m_mixLaneMutedByLane.clear();
    m_mixLaneSoloByLane.clear();
    m_audioDurationMsByPath.clear();
    m_clipEditorPlayheadMsByTrack.clear();
    m_clipEditorPreviewSourceTrackId = {};
    m_clipEditorPreviewStartMs = 0;
    m_clipEditorPreviewClipStartMs = 0;
    m_clipEditorPreviewClipEndMs = 0;
    m_audioEngine->setMasterGain(m_masterMixGainDb);
    m_loadedPath.clear();
    m_embeddedVideoAudioPath.clear();
    m_embeddedVideoAudioDisplayName.clear();
    m_embeddedVideoAudioMuted = true;
    m_totalFrames = 0;
    m_fps = 0.0;
    m_currentFrame = {};
    m_currentGrayFrame.release();
}

void PlayerController::resetProjectState()
{
    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();
    pause(false);
    m_audioEngine->stopAll();
    m_videoPlayback.close();
    m_videoPlayback.setPresentationScale(1.0);
    clearProjectStateAfterMediaStop();
    m_renderService.setFastPlaybackEnabled(false);
    m_transport.setInsertionFollowsPlayback(false);
    m_motionTrackingEnabled = false;
    refreshOverlays();
    emitCurrentFrame();
    emit videoLoaded({}, 0, 0.0);
    emit videoAudioStateChanged();
    emit loopRangeChanged();
    emit selectionChanged(false);
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
    emit editStateChanged();
    emit motionTrackingChanged(false);
}

bool PlayerController::openVideo(const QString& filePath)
{
    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();
    pause(false);
    m_audioEngine->stopTrack(m_embeddedVideoAudioTrackId);
    m_videoPlayback.setPresentationScale(1.0);

    if (!m_videoPlayback.open(filePath))
    {
        emit statusChanged(QStringLiteral("Failed to open video: %1").arg(filePath));
        return false;
    }

    clearProjectStateAfterMediaStop();
    m_loadedPath = filePath;
    m_totalFrames = m_videoPlayback.totalFrames();
    m_fps = m_videoPlayback.fps();
    m_currentFrame = m_videoPlayback.currentFrame();
    if (const auto extractedAudioPath = dawg::audio::extractEmbeddedAudioToWave(filePath); extractedAudioPath.has_value())
    {
        m_embeddedVideoAudioPath = *extractedAudioPath;
        m_embeddedVideoAudioDisplayName = QFileInfo(filePath).fileName();
    }

    updateCurrentGrayFrameIfNeeded();
    const auto safeFps = m_fps > 1.0 ? m_fps : 1.0;
    const auto playbackIntervalMs = static_cast<int>(1000.0 / safeFps);
    m_transport.setPlaybackIntervalMs(playbackIntervalMs);

    refreshOverlays();
    emitCurrentFrame();
    emit videoLoaded(m_loadedPath, m_totalFrames, m_fps);
    emit videoAudioStateChanged();
    emit loopRangeChanged();
    emit selectionChanged(false);
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
    emit editStateChanged();
    m_lastLoggedQueueStarvationCount = 0;
    m_perfLogger.startSession(
        filePath,
        m_videoPlayback.decoderBackendName(),
        m_renderService.backendName(),
        m_fps,
        m_totalFrames);
    emit statusChanged(
        QStringLiteral("Loaded %1 via %2 decode and %3 render.")
            .arg(filePath)
            .arg(m_videoPlayback.decoderBackendName())
            .arg(m_renderService.backendName()));

    return true;
}

dawg::project::ControllerState PlayerController::snapshotProjectState() const
{
    dawg::project::ControllerState state;
    state.videoPath = m_loadedPath;
    state.audioPoolAssetPaths = m_audioPool.assetPaths();
    state.trackerState = m_tracker.snapshotState();
    state.selectedTrackIds = m_selectedTrackIds;
    state.currentFrameIndex = m_currentFrame.index;
    state.motionTrackingEnabled = m_motionTrackingEnabled;
    state.insertionFollowsPlayback = m_transport.insertionFollowsPlayback();
    state.fastPlaybackEnabled = m_renderService.fastPlaybackEnabled();
    state.embeddedVideoAudioMuted = m_embeddedVideoAudioMuted;
    state.loopStartFrame = m_loopStartFrame;
    state.loopEndFrame = m_loopEndFrame;
    state.masterMixGainDb = m_masterMixGainDb;
    state.masterMixMuted = m_masterMixMuted;

    std::vector<int> laneIndices;
    laneIndices.reserve(
        m_mixLaneGainDbByLane.size()
        + m_mixLaneMutedByLane.size()
        + m_mixLaneSoloByLane.size());
    for (const auto& [laneIndex, _] : m_mixLaneGainDbByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    for (const auto& [laneIndex, _] : m_mixLaneMutedByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    for (const auto& [laneIndex, _] : m_mixLaneSoloByLane)
    {
        laneIndices.push_back(laneIndex);
    }
    std::sort(laneIndices.begin(), laneIndices.end());
    laneIndices.erase(std::unique(laneIndices.begin(), laneIndices.end()), laneIndices.end());
    for (const auto laneIndex : laneIndices)
    {
        state.mixLanes.push_back(dawg::project::MixLaneState{
            .laneIndex = laneIndex,
            .gainDb = mixLaneGainDb(laneIndex),
            .muted = isMixLaneMuted(laneIndex),
            .soloed = isMixLaneSoloed(laneIndex)
        });
    }

    state.clipEditorPlayheads.reserve(m_clipEditorPlayheadMsByTrack.size());
    for (auto it = m_clipEditorPlayheadMsByTrack.cbegin(); it != m_clipEditorPlayheadMsByTrack.cend(); ++it)
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

bool PlayerController::restoreProjectState(const dawg::project::ControllerState& state, QString* errorMessage)
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

    if (state.videoPath.isEmpty())
    {
        resetProjectState();
    }
    else if (!openVideo(state.videoPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open project video: %1").arg(state.videoPath);
        }
        return false;
    }

    m_audioPool.setAssetPaths(state.audioPoolAssetPaths);
    m_tracker.restoreState(state.trackerState);
    m_loopStartFrame = state.loopStartFrame;
    m_loopEndFrame = state.loopEndFrame;
    m_masterMixGainDb = clampMixGainDb(state.masterMixGainDb);
    m_masterMixMuted = state.masterMixMuted;
    m_mixLaneGainDbByLane.clear();
    m_mixLaneMutedByLane.clear();
    m_mixLaneSoloByLane.clear();
    for (const auto& lane : state.mixLanes)
    {
        if (lane.laneIndex < 0)
        {
            continue;
        }
        if (std::abs(lane.gainDb) >= 0.001F)
        {
            m_mixLaneGainDbByLane[lane.laneIndex] = clampMixGainDb(lane.gainDb);
        }
        if (lane.muted)
        {
            m_mixLaneMutedByLane[lane.laneIndex] = true;
        }
        if (lane.soloed)
        {
            m_mixLaneSoloByLane[lane.laneIndex] = true;
        }
    }
    m_clipEditorPlayheadMsByTrack.clear();
    for (const auto& [trackId, playheadMs] : state.clipEditorPlayheads)
    {
        if (!trackId.isNull())
        {
            m_clipEditorPlayheadMsByTrack.insert(trackId, playheadMs);
        }
    }

    m_motionTrackingEnabled = state.motionTrackingEnabled;
    m_transport.setInsertionFollowsPlayback(state.insertionFollowsPlayback);
    m_renderService.setFastPlaybackEnabled(state.fastPlaybackEnabled);
    m_embeddedVideoAudioMuted = state.embeddedVideoAudioMuted;
    m_audioEngine->setMasterGain(m_masterMixMuted ? kSilentMixGainDb : m_masterMixGainDb);

    std::vector<QUuid> validSelection;
    validSelection.reserve(state.selectedTrackIds.size());
    for (const auto& selectedTrackId : state.selectedTrackIds)
    {
        const auto trackIt = std::find_if(
            m_tracker.tracks().cbegin(),
            m_tracker.tracks().cend(),
            [&selectedTrackId](const TrackPoint& track)
            {
                return track.id == selectedTrackId;
            });
        if (trackIt != m_tracker.tracks().cend())
        {
            validSelection.push_back(selectedTrackId);
        }
    }
    m_selectedTrackIds = validSelection;
    m_selectedTrackId = validSelection.empty() ? QUuid{} : validSelection.front();
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();

    updateCurrentGrayFrameIfNeeded();
    refreshOverlays();
    if (hasVideoLoaded())
    {
        const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
        const auto clampedFrameIndex = std::clamp(state.currentFrameIndex, 0, maxFrameIndex);
        if (!loadFrameAt(clampedFrameIndex))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to restore project frame %1.").arg(clampedFrameIndex);
            }
            return false;
        }
    }
    else
    {
        emitCurrentFrame();
    }

    emit videoAudioStateChanged();
    emit loopRangeChanged();
    emit selectionChanged(!m_selectedTrackIds.empty());
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();
    emit editStateChanged();
    emit motionTrackingChanged(m_motionTrackingEnabled);
    return true;
}

bool PlayerController::importAudioToPool(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    if (m_audioPool.import(filePath))
    {
        emit audioPoolChanged();
    }

    return true;
}

bool PlayerController::setSelectedTrackClipPlayheadMs(const int playheadMs)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMs(trackIt->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    const auto clipStartMs = audioClipStartMs(*trackIt->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*trackIt->attachedAudio, *sourceDurationMs);
    const auto clampedPlayheadMs = std::clamp(playheadMs, 0, std::max(0, *sourceDurationMs - 1));
    const auto currentIt = m_clipEditorPlayheadMsByTrack.constFind(m_selectedTrackId);
    const auto changed =
        currentIt == m_clipEditorPlayheadMsByTrack.cend() || currentIt.value() != clampedPlayheadMs;
    if (!changed
        && !(m_clipEditorPreviewSourceTrackId == m_selectedTrackId
            && m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId)))
    {
        return false;
    }

    m_clipEditorPlayheadMsByTrack.insert(m_selectedTrackId, clampedPlayheadMs);
    if (m_clipEditorPreviewSourceTrackId != m_selectedTrackId
        || !m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId))
    {
        return changed;
    }

    const auto previewStartMs = std::clamp(
        clampedPlayheadMs,
        clipStartMs,
        std::max(clipStartMs, clipEndMs - 1));
    if (!m_audioEngine->playTrack(m_clipEditorPreviewTrackId, trackIt->attachedAudio->assetPath, previewStartMs))
    {
        return changed;
    }

    m_clipEditorPreviewStartMs = previewStartMs;
    m_clipEditorPreviewClipStartMs = clipStartMs;
    m_clipEditorPreviewClipEndMs = clipEndMs;
    m_clipEditorPreviewElapsedTimer.restart();
    m_audioEngine->setTrackGain(m_clipEditorPreviewTrackId, trackIt->attachedAudio->gainDb);
    m_audioEngine->setTrackPan(m_clipEditorPreviewTrackId, 0.0F);
    m_clipEditorPreviewStopTimer.start(std::max(1, clipEndMs - previewStartMs));
    return true;
}

bool PlayerController::setSelectedTrackAudioGainDb(const float gainDb)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto clampedGainDb = clampMixGainDb(gainDb);
    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        return false;
    }

    if (std::abs(trackIt->attachedAudio->gainDb - clampedGainDb) < 0.001F)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioGainDb(m_selectedTrackId, clampedGainDb))
    {
        return false;
    }

    if (m_clipEditorPreviewSourceTrackId == m_selectedTrackId
        && m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId))
    {
        m_audioEngine->setTrackGain(m_clipEditorPreviewTrackId, clampedGainDb);
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }

    emit editStateChanged();
    return true;
}

bool PlayerController::setSelectedTrackLoopEnabled(const bool enabled)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        return false;
    }

    if (trackIt->attachedAudio->loopEnabled == enabled)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioLoopEnabled(m_selectedTrackId, enabled))
    {
        return false;
    }

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }

    emit editStateChanged();
    return true;
}

bool PlayerController::startAudioPoolPreview(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    const auto wasPreviewing = m_audioEngine->isTrackPlaying(m_audioPoolPreviewTrackId);
    const auto previousAssetPath = m_audioPoolPreviewAssetPath;
    if (!m_audioEngine->playTrack(m_audioPoolPreviewTrackId, filePath))
    {
        return false;
    }

    m_audioEngine->setTrackGain(m_audioPoolPreviewTrackId, 0.0F);
    m_audioEngine->setTrackPan(m_audioPoolPreviewTrackId, 0.0F);
    m_audioPoolPreviewAssetPath = filePath;
    if (!wasPreviewing || previousAssetPath != filePath)
    {
        emit audioPoolPlaybackStateChanged();
    }
    return true;
}

void PlayerController::stopAudioPoolPreview()
{
    const auto wasPreviewing = m_audioEngine->isTrackPlaying(m_audioPoolPreviewTrackId);
    m_audioEngine->stopTrack(m_audioPoolPreviewTrackId);
    if (wasPreviewing || !m_audioPoolPreviewAssetPath.isEmpty())
    {
        m_audioPoolPreviewAssetPath.clear();
        emit audioPoolPlaybackStateChanged();
        return;
    }

    m_audioPoolPreviewAssetPath.clear();
}

bool PlayerController::startSelectedTrackClipPreview()
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMs(trackIt->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    if (m_transport.isPlaying())
    {
        pause(false);
    }

    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();

    const auto clipStartMs = audioClipStartMs(*trackIt->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*trackIt->attachedAudio, *sourceDurationMs);
    const auto previewStartMs = std::clamp(
        m_clipEditorPlayheadMsByTrack.value(trackIt->id, clipStartMs),
        clipStartMs,
        std::max(clipStartMs, clipEndMs - 1));
    const auto clipDurationMs = std::max(1, clipEndMs - previewStartMs);
    if (!m_audioEngine->playTrack(m_clipEditorPreviewTrackId, trackIt->attachedAudio->assetPath, previewStartMs))
    {
        return false;
    }

    m_clipEditorPlayheadMsByTrack.insert(trackIt->id, previewStartMs);
    m_clipEditorPreviewSourceTrackId = trackIt->id;
    m_clipEditorPreviewStartMs = previewStartMs;
    m_clipEditorPreviewClipStartMs = clipStartMs;
    m_clipEditorPreviewClipEndMs = clipEndMs;
    m_clipEditorPreviewElapsedTimer.restart();
    m_audioEngine->setTrackGain(m_clipEditorPreviewTrackId, trackIt->attachedAudio->gainDb);
    m_audioEngine->setTrackPan(m_clipEditorPreviewTrackId, 0.0F);
    m_clipEditorPreviewStopTimer.start(std::max(1, clipDurationMs));
    return true;
}

void PlayerController::handleClipEditorPreviewTimeout()
{
    if (m_clipEditorPreviewSourceTrackId.isNull())
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_clipEditorPreviewSourceTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto sourceDurationMs = audioDurationMs(trackIt->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        stopSelectedTrackClipPreview();
        return;
    }

    const auto clipStartMs = audioClipStartMs(*trackIt->attachedAudio);
    const auto clipEndMs = audioClipEndMs(*trackIt->attachedAudio, *sourceDurationMs);
    const auto clipDurationMs = std::max(1, clipEndMs - clipStartMs);

    if (!trackIt->attachedAudio->loopEnabled)
    {
        stopSelectedTrackClipPreview();
        return;
    }

    if (!m_audioEngine->playTrack(m_clipEditorPreviewTrackId, trackIt->attachedAudio->assetPath, clipStartMs))
    {
        stopSelectedTrackClipPreview();
        return;
    }

    m_clipEditorPlayheadMsByTrack.insert(trackIt->id, clipStartMs);
    m_clipEditorPreviewStartMs = clipStartMs;
    m_clipEditorPreviewClipStartMs = clipStartMs;
    m_clipEditorPreviewClipEndMs = clipEndMs;
    m_clipEditorPreviewElapsedTimer.restart();
    m_audioEngine->setTrackGain(m_clipEditorPreviewTrackId, trackIt->attachedAudio->gainDb);
    m_audioEngine->setTrackPan(m_clipEditorPreviewTrackId, 0.0F);
    m_clipEditorPreviewStopTimer.start(clipDurationMs);
}

void PlayerController::stopSelectedTrackClipPreview()
{
    if (!m_clipEditorPreviewSourceTrackId.isNull())
    {
        if (const auto playheadMs = currentSelectedTrackClipPreviewPlayheadMs(); playheadMs.has_value())
        {
            m_clipEditorPlayheadMsByTrack.insert(m_clipEditorPreviewSourceTrackId, *playheadMs);
        }
    }

    m_clipEditorPreviewStopTimer.stop();
    m_audioEngine->stopTrack(m_clipEditorPreviewTrackId);
    m_clipEditorPreviewSourceTrackId = {};
    m_clipEditorPreviewStartMs = 0;
    m_clipEditorPreviewClipStartMs = 0;
    m_clipEditorPreviewClipEndMs = 0;
}

bool PlayerController::setSelectedTrackClipRangeMs(const int clipStartMs, const int clipEndMs)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end() || !trackIt->attachedAudio.has_value())
    {
        return false;
    }

    const auto sourceDurationMs = audioDurationMs(trackIt->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return false;
    }

    const auto clampedClipStartMs = std::clamp(clipStartMs, 0, std::max(0, *sourceDurationMs - 1));
    const auto clampedClipEndMs = std::clamp(clipEndMs, clampedClipStartMs + 1, *sourceDurationMs);
    const auto currentClipStartMs = audioClipStartMs(*trackIt->attachedAudio);
    const auto currentClipEndMs = audioClipEndMs(*trackIt->attachedAudio, *sourceDurationMs);
    const auto previewWasPlaying =
        m_clipEditorPreviewSourceTrackId == m_selectedTrackId
        && m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId);
    const auto playheadBeforeChange =
        previewWasPlaying
            ? currentSelectedTrackClipPreviewPlayheadMs().value_or(
                m_clipEditorPlayheadMsByTrack.value(m_selectedTrackId, currentClipStartMs))
            : m_clipEditorPlayheadMsByTrack.value(m_selectedTrackId, currentClipStartMs);
    if (clampedClipStartMs == currentClipStartMs && clampedClipEndMs == currentClipEndMs)
    {
        return false;
    }

    if (!m_tracker.setTrackAudioClipRange(m_selectedTrackId, clampedClipStartMs, clampedClipEndMs))
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
        static_cast<void>(setSelectedTrackClipPlayheadMs(previewPlayheadMs));
    }
    else
    {
        m_clipEditorPlayheadMsByTrack.insert(m_selectedTrackId, storedPlayheadMs);
    }
    refreshOverlays();
    emit editStateChanged();

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }

    return true;
}

void PlayerController::setLoopStartFrame(const int frameIndex)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, maxFrameIndex);
    auto nextLoopStartFrame = std::optional<int>{clampedFrameIndex};
    auto nextLoopEndFrame = m_loopEndFrame;
    if (nextLoopEndFrame.has_value() && clampedFrameIndex > *nextLoopEndFrame)
    {
        nextLoopEndFrame = clampedFrameIndex;
    }

    if (m_loopStartFrame == nextLoopStartFrame && m_loopEndFrame == nextLoopEndFrame)
    {
        return;
    }

    m_loopStartFrame = nextLoopStartFrame;
    m_loopEndFrame = nextLoopEndFrame;
    emit loopRangeChanged();
}

void PlayerController::setLoopEndFrame(const int frameIndex)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, maxFrameIndex);
    auto nextLoopStartFrame = m_loopStartFrame;
    auto nextLoopEndFrame = std::optional<int>{clampedFrameIndex};
    if (nextLoopStartFrame.has_value() && clampedFrameIndex < *nextLoopStartFrame)
    {
        nextLoopStartFrame = clampedFrameIndex;
    }

    if (m_loopStartFrame == nextLoopStartFrame && m_loopEndFrame == nextLoopEndFrame)
    {
        return;
    }

    m_loopStartFrame = nextLoopStartFrame;
    m_loopEndFrame = nextLoopEndFrame;
    emit loopRangeChanged();
}

void PlayerController::clearLoopRange()
{
    if (!m_loopStartFrame.has_value() && !m_loopEndFrame.has_value())
    {
        return;
    }

    m_loopStartFrame.reset();
    m_loopEndFrame.reset();
    emit loopRangeChanged();
}

void PlayerController::setMasterMixGainDb(const float gainDb)
{
    const auto clampedGainDb = clampMixGainDb(gainDb);
    if (std::abs(m_masterMixGainDb - clampedGainDb) < 0.001F)
    {
        return;
    }

    m_masterMixGainDb = clampedGainDb;
    m_audioEngine->setMasterGain(m_masterMixMuted ? kSilentMixGainDb : m_masterMixGainDb);
}

void PlayerController::setMasterMixMuted(const bool muted)
{
    if (m_masterMixMuted == muted)
    {
        return;
    }

    m_masterMixMuted = muted;
    m_audioEngine->setMasterGain(m_masterMixMuted ? kSilentMixGainDb : m_masterMixGainDb);
}

void PlayerController::setMixLaneGainDb(const int laneIndex, const float gainDb)
{
    if (laneIndex < 0)
    {
        return;
    }

    const auto clampedGainDb = clampMixGainDb(gainDb);
    const auto existingIt = m_mixLaneGainDbByLane.find(laneIndex);
    if (existingIt != m_mixLaneGainDbByLane.end()
        && std::abs(existingIt->second - clampedGainDb) < 0.001F)
    {
        return;
    }

    if (std::abs(clampedGainDb) < 0.001F)
    {
        m_mixLaneGainDbByLane.erase(laneIndex);
    }
    else
    {
        m_mixLaneGainDbByLane[laneIndex] = clampedGainDb;
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

void PlayerController::setMixLaneMuted(const int laneIndex, const bool muted)
{
    if (laneIndex < 0)
    {
        return;
    }

    const auto existingIt = m_mixLaneMutedByLane.find(laneIndex);
    if (existingIt != m_mixLaneMutedByLane.end() && existingIt->second == muted)
    {
        return;
    }

    if (muted)
    {
        m_mixLaneMutedByLane[laneIndex] = true;
    }
    else
    {
        m_mixLaneMutedByLane.erase(laneIndex);
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

void PlayerController::setMixLaneSoloed(const int laneIndex, const bool soloed)
{
    if (laneIndex < 0)
    {
        return;
    }

    const auto existingIt = m_mixLaneSoloByLane.find(laneIndex);
    if (existingIt != m_mixLaneSoloByLane.end() && existingIt->second == soloed)
    {
        return;
    }

    if (soloed)
    {
        m_mixLaneSoloByLane[laneIndex] = true;
    }
    else
    {
        m_mixLaneSoloByLane.erase(laneIndex);
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

std::optional<float> PlayerController::adjustMixLaneGainForTrack(const QUuid& trackId, const float deltaDb)
{
    if (trackId.isNull() || std::abs(deltaDb) < 0.001F)
    {
        return std::nullopt;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().cbegin(),
        m_tracker.tracks().cend(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });
    if (trackIt == m_tracker.tracks().cend() || !trackIt->attachedAudio.has_value())
    {
        return std::nullopt;
    }

    const auto spans = timelineTrackSpans();
    const auto spanIt = std::find_if(
        spans.cbegin(),
        spans.cend(),
        [&trackId](const TimelineTrackSpan& span)
        {
            return span.id == trackId;
        });
    if (spanIt == spans.cend())
    {
        return std::nullopt;
    }

    const auto nextGainDb = clampMixGainDb(mixLaneGainDb(spanIt->laneIndex) + deltaDb);
    setMixLaneGainDb(spanIt->laneIndex, nextGainDb);
    return nextGainDb;
}

void PlayerController::goToStart()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause(false);
    if (loadFrameAt(0))
    {
        emit statusChanged(QStringLiteral("Returned to the start of the clip."));
    }
}

void PlayerController::togglePlayback()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    if (m_transport.isPlaying())
    {
        pause(true);
        return;
    }

    stopSelectedTrackClipPreview();
    applyPresentationScaleForPlaybackState(true);
    emitCurrentFrame();
    m_transport.startPlayback(m_currentFrame.index);
    m_playbackStartTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
    m_playbackElapsedTimer.restart();
    m_perfPlaybackTickTimer.restart();
    m_perfLogger.logEvent(
        QStringLiteral("play"),
        QStringLiteral("frame=%1 time=%2s queue=%3/%4")
            .arg(m_currentFrame.index)
            .arg(m_currentFrame.timestampSeconds, 0, 'f', 3)
            .arg(m_videoPlayback.runtimeStats().queuedFrames)
            .arg(m_videoPlayback.runtimeStats().prefetchTargetFrames));
    syncAttachedAudioForCurrentFrame();
}

void PlayerController::pause(const bool restorePlaybackAnchor)
{
    m_clipEditorPreviewStopTimer.stop();
    m_audioEngine->stopAll();
    m_perfLogger.logEvent(
        QStringLiteral("stop"),
        QStringLiteral("frame=%1 time=%2s")
            .arg(m_currentFrame.index)
            .arg(m_currentFrame.timestampSeconds, 0, 'f', 3));
    const auto restoreFrame = m_transport.stopPlayback(m_currentFrame.index, restorePlaybackAnchor);
    if (restoreFrame < 0)
    {
        return;
    }

    applyPresentationScaleForPlaybackState(false);
    loadFrameAt(restoreFrame);
}

void PlayerController::seekToFrame(const int frameIndex)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto clampedFrameIndex = frameIndex < 0 ? 0 : (frameIndex > maxFrameIndex ? maxFrameIndex : frameIndex);
    if (clampedFrameIndex == m_currentFrame.index)
    {
        return;
    }

    QElapsedTimer seekTimer;
    seekTimer.start();
    if (!loadFrameAt(clampedFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to seek to frame %1.").arg(clampedFrameIndex));
        m_perfLogger.logEvent(
            QStringLiteral("seek_failed"),
            QStringLiteral("targetFrame=%1").arg(clampedFrameIndex));
        return;
    }

    const auto stats = m_videoPlayback.runtimeStats();
    m_perfLogger.logEvent(
        seekTimer.elapsed() >= 12 ? QStringLiteral("seek_slow") : QStringLiteral("seek"),
        QStringLiteral("targetFrame=%1 elapsedMs=%2 queue=%3/%4 fallbackStarvations=%5")
            .arg(clampedFrameIndex)
            .arg(seekTimer.elapsed())
            .arg(stats.queuedFrames)
            .arg(stats.prefetchTargetFrames)
            .arg(stats.queueStarvationCount));

    if (m_transport.isPlaying())
    {
        m_transport.setPlaybackAnchorFrame(clampedFrameIndex);
        m_playbackStartTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
        m_playbackElapsedTimer.restart();
        syncAttachedAudioForCurrentFrame();
    }
}

void PlayerController::stepForward()
{
    static_cast<void>(advanceOneFrame(true, m_transport.isPlaying()));
}

bool PlayerController::advanceOneFrame(const bool presentFrame, const bool syncAudio)
{
    if (!hasVideoLoaded())
    {
        return false;
    }

    updateCurrentGrayFrameIfNeeded();
    const auto previousGrayFrame = m_currentGrayFrame;
    const auto nextFrame = m_videoPlayback.stepForward();
    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        pause();
        emit statusChanged(QStringLiteral("Reached the end of the clip."));
        return false;
    }

    cv::Mat nextGrayFrame;
    if (needsTrackingFrameProcessing())
    {
        nextGrayFrame = m_analysisFrameProvider.grayscaleFrame(*nextFrame);
        m_tracker.trackForward(previousGrayFrame, nextGrayFrame, nextFrame->index);
    }

    m_currentFrame = *nextFrame;
    m_currentGrayFrame = nextGrayFrame;

    if (presentFrame)
    {
        refreshOverlays();
        emitCurrentFrame();
    }
    if (syncAudio)
    {
        syncAttachedAudioForCurrentFrame();
    }

    return true;
}

void PlayerController::stepBackward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause(false);

    const auto targetFrameIndex = m_currentFrame.index > 0 ? (m_currentFrame.index - 1) : 0;
    if (targetFrameIndex == m_currentFrame.index)
    {
        emit statusChanged(QStringLiteral("Already at the first frame."));
        return;
    }

    if (!loadFrameAt(targetFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to step backward."));
    }
}

void PlayerController::stepFastBackward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause(false);

    const auto targetFrameIndex = std::max(0, m_currentFrame.index - 5);
    if (targetFrameIndex == m_currentFrame.index)
    {
        emit statusChanged(QStringLiteral("Already at the first frame."));
        return;
    }

    if (!loadFrameAt(targetFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to step fast backward."));
    }
}

void PlayerController::stepFastForward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause(false);

    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto targetFrameIndex = std::min(maxFrameIndex, m_currentFrame.index + 5);
    if (targetFrameIndex == m_currentFrame.index)
    {
        emit statusChanged(QStringLiteral("Already at the last frame."));
        return;
    }

    if (!loadFrameAt(targetFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to step fast forward."));
    }
}

void PlayerController::seedTrack(const QPointF& imagePoint)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    auto& track = m_tracker.seedTrack(m_currentFrame.index, imagePoint, m_motionTrackingEnabled);
    const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
    const auto placeholderFrameCount = std::max(1, static_cast<int>(std::lround(safeFps)));
    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto defaultEndFrame = std::clamp(
        track.startFrame + placeholderFrameCount - 1,
        track.startFrame,
        maxFrameIndex);
    m_tracker.setTrackEndFrame(track.id, defaultEndFrame);
    setSelectedTrackId(track.id);
    refreshOverlays();

    emit statusChanged(
        QStringLiteral("Added %1 at frame %2 (%3)")
            .arg(track.label)
            .arg(m_currentFrame.index)
            .arg(track.motionTracked ? QStringLiteral("tracked") : QStringLiteral("manual")));
    emit trackAvailabilityChanged(true);
    emit editStateChanged();
}

bool PlayerController::createTrackWithAudioAtCurrentFrame(const QString& filePath)
{
    if (!hasVideoLoaded())
    {
        emit statusChanged(QStringLiteral("Open a video before adding audio nodes."));
        return false;
    }

    const auto imageCenter = QPointF{
        static_cast<double>(m_currentFrame.cpuBgr.cols) * 0.5,
        static_cast<double>(m_currentFrame.cpuBgr.rows) * 0.5
    };
    return createTrackWithAudioAtCurrentFrame(filePath, imageCenter);
}

bool PlayerController::createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint)
{
    if (!hasVideoLoaded())
    {
        emit statusChanged(QStringLiteral("Open a video before adding audio nodes."));
        return false;
    }

    if (filePath.isEmpty())
    {
        return false;
    }

    importAudioToPool(filePath);

    auto& track = m_tracker.seedTrack(m_currentFrame.index, imagePoint, m_motionTrackingEnabled);
    if (!m_tracker.setTrackAudioAttachment(track.id, filePath))
    {
        m_tracker.removeTrack(track.id);
        emit statusChanged(QStringLiteral("Failed to attach sound to the new node."));
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [&track](const TrackPoint& existingTrack)
        {
            return existingTrack.id == track.id;
        });
    if (trackIt != m_tracker.tracks().end())
    {
        if (const auto endFrame = trimmedEndFrameForTrack(*trackIt); endFrame.has_value())
        {
            m_tracker.setTrackEndFrame(track.id, *endFrame);
        }
    }

    setSelectedTrackId(track.id);
    refreshOverlays();
    emit audioPoolChanged();
    emit trackAvailabilityChanged(true);
    emit editStateChanged();
    emit statusChanged(
        QStringLiteral("Added %1 at frame %2.")
            .arg(QFileInfo(filePath).fileName())
            .arg(m_currentFrame.index));
    return true;
}

bool PlayerController::importSoundForSelectedTrack(const QString& filePath)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        emit statusChanged(QStringLiteral("Select a node before importing sound."));
        return false;
    }

    importAudioToPool(filePath);

    if (!m_tracker.setTrackAudioAttachment(m_selectedTrackId, filePath))
    {
        emit statusChanged(QStringLiteral("Failed to attach sound to the selected node."));
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt != m_tracker.tracks().end())
    {
        if (const auto endFrame = trimmedEndFrameForTrack(*trackIt); endFrame.has_value())
        {
            m_tracker.setTrackEndFrame(m_selectedTrackId, *endFrame);
        }
    }

    refreshOverlays();
    emit audioPoolChanged();
    emit editStateChanged();
    emit statusChanged(QStringLiteral("Attached %1 to the selected node.").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool PlayerController::selectTrackAndJumpToStart(const QUuid& trackId)
{
    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });
    if (trackIt == m_tracker.tracks().end())
    {
        clearSelection();
        return false;
    }

    setSelectedTrackId(trackId);
    seekToFrame(std::max(0, trackIt->startFrame));
    return true;
}

void PlayerController::selectAllVisibleTracks()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    std::vector<QUuid> visibleTrackIds;
    visibleTrackIds.reserve(m_currentOverlays.size());

    for (const auto& overlay : m_currentOverlays)
    {
        visibleTrackIds.push_back(overlay.id);
    }

    if (visibleTrackIds.empty())
    {
        clearSelection();
        emit statusChanged(QStringLiteral("No nodes are visible on the current frame."));
        return;
    }

    m_selectedTrackIds = visibleTrackIds;
    m_selectedTrackId = visibleTrackIds.front();
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();
    refreshOverlays();
    emit selectionChanged(true);
    emit statusChanged(QStringLiteral("Selected %1 node(s) on the current frame.").arg(visibleTrackIds.size()));
}

void PlayerController::selectTracks(const QList<QUuid>& trackIds)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    std::vector<QUuid> validTrackIds;
    validTrackIds.reserve(trackIds.size());

    for (const auto& trackId : trackIds)
    {
        if (trackId.isNull()
            || !m_tracker.hasTrack(trackId)
            || std::find(validTrackIds.begin(), validTrackIds.end(), trackId) != validTrackIds.end())
        {
            continue;
        }

        validTrackIds.push_back(trackId);
    }

    if (validTrackIds.empty())
    {
        clearSelection();
        return;
    }

    m_selectedTrackIds = validTrackIds;
    m_selectedTrackId = validTrackIds.front();
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();
    refreshOverlays();
    emit selectionChanged(true);
    emit statusChanged(QStringLiteral("Selected %1 node(s).").arg(validTrackIds.size()));
}

void PlayerController::selectTrack(const QUuid& trackId)
{
    if (!m_tracker.hasTrack(trackId))
    {
        clearSelection();
        return;
    }

    setSelectedTrackId(trackId);
}

void PlayerController::selectNextVisibleTrack()
{
    if (!hasVideoLoaded() || m_currentOverlays.empty())
    {
        clearSelection();
        emit statusChanged(QStringLiteral("No nodes are visible on the current frame."));
        return;
    }

    auto nextIndex = 0;
    if (!m_selectedTrackId.isNull())
    {
        const auto currentIt = std::find_if(
            m_currentOverlays.begin(),
            m_currentOverlays.end(),
            [this](const TrackOverlay& overlay)
            {
                return overlay.id == m_selectedTrackId;
            });
        if (currentIt != m_currentOverlays.end())
        {
            nextIndex = static_cast<int>(std::distance(m_currentOverlays.begin(), currentIt) + 1)
                % static_cast<int>(m_currentOverlays.size());
        }
    }

    setSelectedTrackId(m_currentOverlays[static_cast<std::size_t>(nextIndex)].id);
}

void PlayerController::clearSelection()
{
    setSelectedTrackId({});
}

bool PlayerController::copySelectedTracks()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return false;
    }

    m_copiedTracks.clear();
    m_copiedTracks.reserve(m_selectedTrackIds.size());

    for (const auto& trackId : m_selectedTrackIds)
    {
        const auto trackIt = std::find_if(
            m_tracker.tracks().begin(),
            m_tracker.tracks().end(),
            [&trackId](const TrackPoint& track)
            {
                return track.id == trackId;
            });
        if (trackIt != m_tracker.tracks().end())
        {
            m_copiedTracks.push_back(*trackIt);
        }
    }

    if (m_copiedTracks.empty())
    {
        return false;
    }

    emit editStateChanged();
    emit statusChanged(
        m_copiedTracks.size() == 1
            ? QStringLiteral("Copied selected node.")
            : QStringLiteral("Copied %1 selected nodes.").arg(m_copiedTracks.size()));
    return true;
}

bool PlayerController::pasteCopiedTracksAtCurrentFrame()
{
    if (!hasVideoLoaded() || m_copiedTracks.empty() || m_currentFrame.cpuBgr.empty())
    {
        return false;
    }

    saveUndoState();

    const auto imageCenter = QPointF{
        static_cast<double>(m_currentFrame.cpuBgr.cols) * 0.5,
        static_cast<double>(m_currentFrame.cpuBgr.rows) * 0.5
    };
    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);

    std::vector<QUuid> pastedTrackIds;
    pastedTrackIds.reserve(m_copiedTracks.size());

    for (const auto& copiedTrack : m_copiedTracks)
    {
        auto pastedTrack = copiedTrack;
        pastedTrack.id = QUuid::createUuid();

        const auto anchorFrame = trackAnchorFrame(copiedTrack);
        const auto anchorPoint = trackAnchorPoint(copiedTrack);
        const auto deltaFrames = m_currentFrame.index - anchorFrame;
        const auto deltaPoint = imageCenter - anchorPoint;

        std::map<int, QPointF> shiftedSamples;
        for (const auto& [frameIndex, point] : copiedTrack.samples)
        {
            const auto shiftedFrameIndex = frameIndex + deltaFrames;
            if (shiftedFrameIndex < 0 || shiftedFrameIndex > maxFrameIndex)
            {
                continue;
            }

            shiftedSamples.insert_or_assign(shiftedFrameIndex, point + deltaPoint);
        }

        if (shiftedSamples.empty())
        {
            shiftedSamples.emplace(m_currentFrame.index, imageCenter);
        }

        pastedTrack.samples = std::move(shiftedSamples);
        pastedTrack.seedFrameIndex = std::clamp(copiedTrack.seedFrameIndex + deltaFrames, 0, maxFrameIndex);
        pastedTrack.startFrame = std::clamp(copiedTrack.startFrame + deltaFrames, 0, maxFrameIndex);
        if (pastedTrack.endFrame.has_value())
        {
            pastedTrack.endFrame = std::clamp(*copiedTrack.endFrame + deltaFrames, pastedTrack.startFrame, maxFrameIndex);
        }

        m_tracker.addTrack(pastedTrack);
        pastedTrackIds.push_back(pastedTrack.id);
    }

    if (pastedTrackIds.empty())
    {
        m_undoTrackerState.reset();
        m_undoSelectedTrackIds.clear();
        emit editStateChanged();
        return false;
    }

    m_selectedTrackIds = pastedTrackIds;
    m_selectedTrackId = pastedTrackIds.front();
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();
    refreshOverlays();
    emit selectionChanged(true);
    emit trackAvailabilityChanged(true);
    emit audioPoolChanged();
    emit editStateChanged();
    emit statusChanged(
        pastedTrackIds.size() == 1
            ? QStringLiteral("Pasted node at the center of the frame.")
            : QStringLiteral("Pasted %1 nodes at the center of the frame.").arg(pastedTrackIds.size()));
    return true;
}

bool PlayerController::cutSelectedTracks()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return false;
    }

    m_copiedTracks.clear();
    m_copiedTracks.reserve(m_selectedTrackIds.size());
    for (const auto& trackId : m_selectedTrackIds)
    {
        const auto trackIt = std::find_if(
            m_tracker.tracks().begin(),
            m_tracker.tracks().end(),
            [&trackId](const TrackPoint& track)
            {
                return track.id == trackId;
            });
        if (trackIt != m_tracker.tracks().end())
        {
            m_copiedTracks.push_back(*trackIt);
        }
    }
    if (m_copiedTracks.empty())
    {
        return false;
    }

    saveUndoState();
    const auto removedCount = m_selectedTrackIds.size() == 1
        ? (m_tracker.removeTrack(m_selectedTrackIds.front()) ? 1 : 0)
        : m_tracker.removeTracks(m_selectedTrackIds);
    if (removedCount <= 0)
    {
        m_undoTrackerState.reset();
        m_undoSelectedTrackIds.clear();
        emit editStateChanged();
        return false;
    }

    setSelectedTrackId({}, false);
    refreshOverlays();
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();
    emit editStateChanged();
    emit statusChanged(
        removedCount == 1
            ? QStringLiteral("Cut selected node.")
            : QStringLiteral("Cut %1 selected nodes.").arg(removedCount));
    return true;
}

bool PlayerController::undoLastTrackEdit()
{
    if (!m_undoTrackerState.has_value())
    {
        return false;
    }

    m_redoTrackerState = m_tracker.snapshotState();
    m_redoSelectedTrackIds = m_selectedTrackIds;
    restoreTrackEditState(*m_undoTrackerState, m_undoSelectedTrackIds);
    m_undoTrackerState.reset();
    m_undoSelectedTrackIds.clear();
    emit editStateChanged();
    emit statusChanged(QStringLiteral("Undid last node edit."));
    return true;
}

bool PlayerController::redoLastTrackEdit()
{
    if (!m_redoTrackerState.has_value())
    {
        return false;
    }

    m_undoTrackerState = m_tracker.snapshotState();
    m_undoSelectedTrackIds = m_selectedTrackIds;
    restoreTrackEditState(*m_redoTrackerState, m_redoSelectedTrackIds);
    m_redoTrackerState.reset();
    m_redoSelectedTrackIds.clear();
    emit editStateChanged();
    emit statusChanged(QStringLiteral("Redid last node edit."));
    return true;
}

bool PlayerController::renameTrack(const QUuid& trackId, const QString& label)
{
    if (!hasVideoLoaded() || trackId.isNull())
    {
        return false;
    }

    const auto trimmedLabel = label.trimmed();
    if (trimmedLabel.isEmpty())
    {
        emit statusChanged(QStringLiteral("Node name cannot be empty."));
        return false;
    }

    if (!m_tracker.setTrackLabel(trackId, trimmedLabel))
    {
        emit statusChanged(QStringLiteral("Failed to rename the selected node."));
        return false;
    }

    refreshOverlays();
    emit statusChanged(QStringLiteral("Renamed node to %1.").arg(trimmedLabel));
    return true;
}

void PlayerController::setTrackStartFrame(const QUuid& trackId, const int frameIndex)
{
    if (!hasVideoLoaded() || trackId.isNull())
    {
        return;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto clampedFrameIndex = frameIndex < 0 ? 0 : (frameIndex > maxFrameIndex ? maxFrameIndex : frameIndex);
    if (!m_tracker.setTrackStartFrame(trackId, clampedFrameIndex))
    {
        return;
    }

    refreshOverlays();
}

void PlayerController::setTrackEndFrame(const QUuid& trackId, const int frameIndex)
{
    if (!hasVideoLoaded() || trackId.isNull())
    {
        return;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto clampedFrameIndex = frameIndex < 0 ? 0 : (frameIndex > maxFrameIndex ? maxFrameIndex : frameIndex);
    if (!m_tracker.setTrackEndFrame(trackId, clampedFrameIndex))
    {
        return;
    }

    refreshOverlays();
}

void PlayerController::moveTrackFrameSpan(const QUuid& trackId, const int deltaFrames)
{
    if (!hasVideoLoaded() || trackId.isNull() || deltaFrames == 0)
    {
        return;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    if (!m_tracker.moveTrackFrameSpan(trackId, deltaFrames, maxFrameIndex))
    {
        return;
    }

    refreshOverlays();
}

void PlayerController::moveSelectedTrack(const QPointF& imagePoint)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return;
    }

    if (m_tracker.updateTrackSample(m_selectedTrackId, m_currentFrame.index, imagePoint))
    {
        refreshOverlays();
    }
}

void PlayerController::nudgeSelectedTracks(const QPointF& delta)
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty() || delta.isNull())
    {
        return;
    }

    auto movedAny = false;
    for (const auto& trackId : m_selectedTrackIds)
    {
        const auto trackIt = std::find_if(
            m_tracker.tracks().begin(),
            m_tracker.tracks().end(),
            [&trackId](const TrackPoint& track)
            {
                return track.id == trackId;
            });
        if (trackIt == m_tracker.tracks().end())
        {
            continue;
        }

        auto basePoint = trackIt->interpolatedSampleAt(m_currentFrame.index);
        if (!basePoint.has_value())
        {
            if (!trackIt->samples.empty())
            {
                basePoint = trackIt->samples.begin()->second;
            }
            else
            {
                continue;
            }
        }

        if (m_tracker.updateTrackSample(trackId, m_currentFrame.index, *basePoint + delta))
        {
            movedAny = true;
        }
    }

    if (movedAny)
    {
        refreshOverlays();
    }
}

void PlayerController::deleteSelectedTrack()
{
    if (m_selectedTrackIds.empty())
    {
        return;
    }

    saveUndoState();
    const auto removedCount = m_selectedTrackIds.size() == 1
        ? (m_tracker.removeTrack(m_selectedTrackIds.front()) ? 1 : 0)
        : m_tracker.removeTracks(m_selectedTrackIds);
    if (removedCount <= 0)
    {
        m_undoTrackerState.reset();
        m_undoSelectedTrackIds.clear();
        emit editStateChanged();
        setSelectedTrackId({}, false);
        emit statusChanged(QStringLiteral("The selected node selection no longer exists."));
        return;
    }

    setSelectedTrackId({}, false);
    refreshOverlays();
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();
    emit editStateChanged();
    emit statusChanged(
        removedCount == 1
            ? QStringLiteral("Deleted selected node.")
            : QStringLiteral("Deleted %1 selected nodes.").arg(removedCount));
}

void PlayerController::clearAllTracks()
{
    if (!hasTracks())
    {
        return;
    }

    saveUndoState();
    m_tracker.reset();
    m_selectedTrackIds.clear();
    setSelectedTrackId({}, false);
    refreshOverlays();
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
    emit editStateChanged();
    emit statusChanged(QStringLiteral("Cleared all nodes."));
}

void PlayerController::setSelectedTrackStartToCurrentFrame()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return;
    }

    const auto updatedCount = m_selectedTrackIds.size() == 1
        ? (m_tracker.setTrackStartFrame(m_selectedTrackIds.front(), m_currentFrame.index) ? 1 : 0)
        : m_tracker.setTrackStartFrames(m_selectedTrackIds, m_currentFrame.index);

    if (updatedCount > 0)
    {
        refreshOverlays();
        emit statusChanged(
            updatedCount == 1
                ? QStringLiteral("Set selected node start to frame %1.").arg(m_currentFrame.index)
                : QStringLiteral("Set %1 selected node starts to frame %2.").arg(updatedCount).arg(m_currentFrame.index));
        return;
    }

    if (m_selectedTrackIds.size() > 1)
    {
        emit statusChanged(QStringLiteral("No selected node starts were earlier than frame %1.").arg(m_currentFrame.index));
    }
}

void PlayerController::setSelectedTrackEndToCurrentFrame()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return;
    }

    const auto updatedCount = m_selectedTrackIds.size() == 1
        ? (m_tracker.setTrackEndFrame(m_selectedTrackIds.front(), m_currentFrame.index) ? 1 : 0)
        : m_tracker.setTrackEndFrames(m_selectedTrackIds, m_currentFrame.index);

    if (updatedCount > 0)
    {
        refreshOverlays();
        emit statusChanged(
            updatedCount == 1
                ? QStringLiteral("Set selected node end to frame %1.").arg(m_currentFrame.index)
                : QStringLiteral("Set %1 selected node ends to frame %2.").arg(updatedCount).arg(m_currentFrame.index));
        return;
    }

    if (m_selectedTrackIds.size() > 1)
    {
        emit statusChanged(QStringLiteral("No selected node ends were later than frame %1.").arg(m_currentFrame.index));
    }
}

void PlayerController::toggleSelectedTrackLabels()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return;
    }

    const auto allLabelsVisible = std::all_of(
        m_selectedTrackIds.begin(),
        m_selectedTrackIds.end(),
        [this](const QUuid& trackId)
        {
            return m_tracker.isTrackLabelVisible(trackId);
        });
    const auto newVisibleState = !allLabelsVisible;
    const auto updatedCount = m_tracker.setTrackLabelsVisible(m_selectedTrackIds, newVisibleState);

    if (updatedCount <= 0)
    {
        return;
    }

    refreshOverlays();
    emit statusChanged(
        newVisibleState
            ? QStringLiteral("Showing labels for %1 selected node(s).").arg(m_selectedTrackIds.size())
            : QStringLiteral("Hiding labels for %1 selected node(s).").arg(m_selectedTrackIds.size()));
}

void PlayerController::setAllTracksStartToCurrentFrame()
{
    if (!hasVideoLoaded() || !hasTracks())
    {
        return;
    }

    const auto updatedCount = m_tracker.setAllTrackStartFrames(m_currentFrame.index);
    if (updatedCount <= 0)
    {
        emit statusChanged(QStringLiteral("No node starts were earlier than frame %1.").arg(m_currentFrame.index));
        return;
    }

    refreshOverlays();
    emit statusChanged(
        QStringLiteral("Set all node starts to frame %1.").arg(m_currentFrame.index));
}

void PlayerController::setAllTracksEndToCurrentFrame()
{
    if (!hasVideoLoaded() || !hasTracks())
    {
        return;
    }

    const auto updatedCount = m_tracker.setAllTrackEndFrames(m_currentFrame.index);
    if (updatedCount <= 0)
    {
        emit statusChanged(QStringLiteral("No node ends were later than frame %1.").arg(m_currentFrame.index));
        return;
    }

    refreshOverlays();
    emit statusChanged(
        QStringLiteral("Set all node ends to frame %1.").arg(m_currentFrame.index));
}

void PlayerController::trimSelectedTracksToAttachedSound()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return;
    }

    int trimmedCount = 0;
    int missingAudioCount = 0;
    int failedDurationCount = 0;

    for (const auto& trackId : m_selectedTrackIds)
    {
        const auto trackIt = std::find_if(
            m_tracker.tracks().begin(),
            m_tracker.tracks().end(),
            [&trackId](const TrackPoint& track)
            {
                return track.id == trackId;
            });
        if (trackIt == m_tracker.tracks().end())
        {
            continue;
        }

        if (!trackIt->attachedAudio.has_value())
        {
            ++missingAudioCount;
            continue;
        }

        const auto endFrame = trimmedEndFrameForTrack(*trackIt);
        if (!endFrame.has_value())
        {
            ++failedDurationCount;
            continue;
        }

        if (m_tracker.setTrackEndFrame(trackId, *endFrame))
        {
            ++trimmedCount;
        }
    }

    if (trimmedCount > 0)
    {
        refreshOverlays();
    }

    if (trimmedCount > 0 && missingAudioCount == 0 && failedDurationCount == 0)
    {
        emit statusChanged(
            trimmedCount == 1
                ? QStringLiteral("Trimmed selected node to its attached sound length.")
                : QStringLiteral("Trimmed %1 selected nodes to their attached sound lengths.").arg(trimmedCount));
        return;
    }

    if (trimmedCount <= 0)
    {
        if (missingAudioCount > 0 && failedDurationCount == 0)
        {
            emit statusChanged(
                m_selectedTrackIds.size() == 1
                    ? QStringLiteral("The selected node has no attached sound.")
                    : QStringLiteral("None of the selected nodes had attached sound."));
            return;
        }

        if (failedDurationCount > 0 && missingAudioCount == 0)
        {
            emit statusChanged(
                QStringLiteral("Could not read the attached sound length for the selected node(s)."));
            return;
        }

        emit statusChanged(
            QStringLiteral("No selected nodes could be trimmed to attached sound length."));
        return;
    }

    emit statusChanged(
        QStringLiteral("Trimmed %1 node(s). %2 had no sound and %3 could not be measured.")
            .arg(trimmedCount)
            .arg(missingAudioCount)
            .arg(failedDurationCount));
}

void PlayerController::toggleSelectedTrackAutoPan()
{
    if (!hasVideoLoaded() || m_selectedTrackIds.empty())
    {
        return;
    }

    const auto enableAutoPan = !selectedTracksAutoPanEnabled();
    int updatedCount = 0;

    for (const auto& trackId : m_selectedTrackIds)
    {
        if (m_tracker.setTrackAutoPanEnabled(trackId, enableAutoPan))
        {
            ++updatedCount;
        }
    }

    if (updatedCount <= 0)
    {
        return;
    }

    refreshOverlays();

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }

    emit statusChanged(
        enableAutoPan
            ? QStringLiteral("Auto Pan enabled for %1 selected node(s).").arg(updatedCount)
            : QStringLiteral("Auto Pan disabled for %1 selected node(s).").arg(updatedCount));
}

void PlayerController::toggleEmbeddedVideoAudioMuted()
{
    if (!hasEmbeddedVideoAudio())
    {
        return;
    }

    m_embeddedVideoAudioMuted = !m_embeddedVideoAudioMuted;
    emit videoAudioStateChanged();

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }
    else if (m_embeddedVideoAudioMuted)
    {
        m_audioEngine->stopTrack(m_embeddedVideoAudioTrackId);
    }

    emit statusChanged(
        m_embeddedVideoAudioMuted
            ? QStringLiteral("Video audio muted.")
            : QStringLiteral("Video audio unmuted."));
}

void PlayerController::setFastPlaybackEnabled(const bool enabled)
{
    if (m_renderService.fastPlaybackEnabled() == enabled)
    {
        return;
    }

    m_renderService.setFastPlaybackEnabled(enabled);
    applyPresentationScaleForPlaybackState(m_transport.isPlaying());
    emit videoAudioStateChanged();
    emitCurrentFrame();
    emit statusChanged(
        enabled
            ? QStringLiteral("Fast playback enabled.")
            : QStringLiteral("Fast playback disabled."));
}

void PlayerController::setMotionTrackingEnabled(const bool enabled)
{
    if (m_motionTrackingEnabled == enabled)
    {
        return;
    }

    m_motionTrackingEnabled = enabled;
    emit motionTrackingChanged(m_motionTrackingEnabled);
    emit statusChanged(
        m_motionTrackingEnabled
            ? QStringLiteral("Motion tracking enabled.")
            : QStringLiteral("Motion tracking disabled. New nodes will stay manual."));
}

void PlayerController::setInsertionFollowsPlayback(const bool enabled)
{
    if (m_transport.insertionFollowsPlayback() == enabled)
    {
        return;
    }

    m_transport.setInsertionFollowsPlayback(enabled);
    emit statusChanged(
        enabled
            ? QStringLiteral("Insertion follows playback enabled.")
            : QStringLiteral("Insertion follows playback disabled."));
}

bool PlayerController::hasVideoLoaded() const
{
    return m_videoPlayback.hasVideoLoaded() && m_currentFrame.isValid();
}

bool PlayerController::isPlaying() const
{
    return m_transport.isPlaying();
}

bool PlayerController::isInsertionFollowsPlayback() const
{
    return m_transport.insertionFollowsPlayback();
}

bool PlayerController::isMotionTrackingEnabled() const
{
    return m_motionTrackingEnabled;
}

bool PlayerController::hasSelection() const
{
    return !m_selectedTrackIds.empty();
}

bool PlayerController::hasTracks() const
{
    return !m_tracker.tracks().empty();
}

bool PlayerController::canPasteTracks() const
{
    return hasVideoLoaded() && !m_copiedTracks.empty();
}

bool PlayerController::canUndoTrackEdit() const
{
    return m_undoTrackerState.has_value();
}

bool PlayerController::canRedoTrackEdit() const
{
    return m_redoTrackerState.has_value();
}

bool PlayerController::isSelectedTrackClipPreviewPlaying() const
{
    return m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId);
}

int PlayerController::trackCount() const
{
    return static_cast<int>(m_tracker.tracks().size());
}

int PlayerController::currentFrameIndex() const
{
    return m_currentFrame.index;
}

int PlayerController::totalFrames() const
{
    return m_totalFrames;
}

double PlayerController::fps() const
{
    return m_fps;
}

QString PlayerController::loadedPath() const
{
    return m_loadedPath;
}

QUuid PlayerController::selectedTrackId() const
{
    return m_selectedTrackId;
}

QString PlayerController::decoderBackendName() const
{
    return m_videoPlayback.decoderBackendName();
}

bool PlayerController::videoHardwareAccelerated() const
{
    return m_videoPlayback.isHardwareDecoded();
}

QString PlayerController::renderBackendName() const
{
    return m_renderService.backendName();
}

bool PlayerController::renderHardwareAccelerated() const
{
    return m_renderService.isHardwareAccelerated();
}

RenderService* PlayerController::renderService()
{
    return &m_renderService;
}

const VideoFrame& PlayerController::currentVideoFrame() const
{
    return m_currentFrame;
}

bool PlayerController::hasEmbeddedVideoAudio() const
{
    return !m_embeddedVideoAudioPath.isEmpty();
}

QString PlayerController::embeddedVideoAudioDisplayName() const
{
    return m_embeddedVideoAudioDisplayName;
}

bool PlayerController::isEmbeddedVideoAudioMuted() const
{
    return m_embeddedVideoAudioMuted;
}

bool PlayerController::isFastPlaybackEnabled() const
{
    return m_renderService.fastPlaybackEnabled();
}

bool PlayerController::isFastPlaybackActive() const
{
    return m_renderService.fastPlaybackEnabled() && m_transport.isPlaying();
}

std::optional<int> PlayerController::loopStartFrame() const
{
    return m_loopStartFrame;
}

std::optional<int> PlayerController::loopEndFrame() const
{
    return m_loopEndFrame;
}

float PlayerController::masterMixGainDb() const
{
    return m_masterMixGainDb;
}

bool PlayerController::masterMixMuted() const
{
    return m_masterMixMuted;
}

float PlayerController::masterMixLevel() const
{
    return m_audioEngine->masterLevel();
}

QSize PlayerController::videoFrameSize() const
{
    return QSize{m_currentFrame.frameSize.width, m_currentFrame.frameSize.height};
}

QString PlayerController::trackLabel(const QUuid& trackId) const
{
    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });

    return trackIt != m_tracker.tracks().end() ? trackIt->label : QString{};
}

bool PlayerController::trackHasAttachedAudio(const QUuid& trackId) const
{
    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });

    return trackIt != m_tracker.tracks().end() && trackIt->attachedAudio.has_value();
}

bool PlayerController::selectedTrackLoopEnabled() const
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return false;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });

    return trackIt != m_tracker.tracks().end()
        && trackIt->attachedAudio.has_value()
        && trackIt->attachedAudio->loopEnabled;
}

bool PlayerController::trackAutoPanEnabled(const QUuid& trackId) const
{
    return m_tracker.isTrackAutoPanEnabled(trackId);
}

bool PlayerController::selectedTracksAutoPanEnabled() const
{
    return !m_selectedTrackIds.empty()
        && std::all_of(
            m_selectedTrackIds.begin(),
            m_selectedTrackIds.end(),
            [this](const QUuid& trackId)
            {
                return m_tracker.isTrackAutoPanEnabled(trackId);
            });
}

std::optional<ClipEditorState> PlayerController::selectedClipEditorState() const
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return std::nullopt;
    }

    const auto trackIt = std::find_if(
        m_tracker.tracks().begin(),
        m_tracker.tracks().end(),
        [this](const TrackPoint& track)
        {
            return track.id == m_selectedTrackId;
        });
    if (trackIt == m_tracker.tracks().end())
    {
        return std::nullopt;
    }

    ClipEditorState state;
    state.trackId = trackIt->id;
    state.label = trackIt->label;
    state.color = trackIt->color;
    state.nodeStartFrame = std::max(0, trackIt->startFrame);
    state.nodeEndFrame = trackIt->endFrame.has_value()
        ? std::max(state.nodeStartFrame, *trackIt->endFrame)
        : std::max(state.nodeStartFrame, m_totalFrames > 0 ? (m_totalFrames - 1) : state.nodeStartFrame);
    state.hasAttachedAudio = trackIt->attachedAudio.has_value();

    if (!trackIt->attachedAudio.has_value())
    {
        return state;
    }

    state.assetPath = trackIt->attachedAudio->assetPath;
    const auto sourceDurationMs = audioDurationMs(trackIt->attachedAudio->assetPath);
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return state;
    }

    state.sourceDurationMs = *sourceDurationMs;
    state.clipStartMs = audioClipStartMs(*trackIt->attachedAudio);
    state.clipEndMs = audioClipEndMs(*trackIt->attachedAudio, *sourceDurationMs);
    state.gainDb = trackIt->attachedAudio->gainDb;
    state.loopEnabled = trackIt->attachedAudio->loopEnabled;
    if (m_clipEditorPreviewSourceTrackId == trackIt->id
        && m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId))
    {
        state.level = m_audioEngine->trackLevel(m_clipEditorPreviewTrackId);
    }
    else
    {
        state.level = m_audioEngine->trackLevel(trackIt->id);
    }
    auto playheadMs = m_clipEditorPlayheadMsByTrack.value(trackIt->id, state.clipStartMs);
    if (const auto previewPlayheadMs = currentSelectedTrackClipPreviewPlayheadMs();
        previewPlayheadMs.has_value() && m_clipEditorPreviewSourceTrackId == trackIt->id)
    {
        playheadMs = *previewPlayheadMs;
    }
    else if (m_transport.isPlaying()
        && m_currentFrame.index >= state.nodeStartFrame
        && m_currentFrame.index <= state.nodeEndFrame)
    {
        const auto elapsedSeconds = frameTimestampSeconds(m_currentFrame.index) - frameTimestampSeconds(state.nodeStartFrame);
        const auto elapsedWithinNodeMs = std::max(0, static_cast<int>(std::lround(elapsedSeconds * 1000.0)));
        playheadMs = audioClipPlaybackOffsetMs(*trackIt->attachedAudio, *sourceDurationMs, elapsedWithinNodeMs);
    }
    else if (!m_clipEditorPlayheadMsByTrack.contains(trackIt->id)
        && m_currentFrame.index >= state.nodeStartFrame
        && m_currentFrame.index <= state.nodeEndFrame)
    {
        const auto elapsedSeconds = frameTimestampSeconds(m_currentFrame.index) - frameTimestampSeconds(state.nodeStartFrame);
        playheadMs = state.clipStartMs + std::max(0, static_cast<int>(std::lround(elapsedSeconds * 1000.0)));
    }
    state.playheadMs = std::clamp(playheadMs, 0, std::max(0, state.sourceDurationMs - 1));

    return state;
}

std::optional<int> PlayerController::currentSelectedTrackClipPreviewPlayheadMs() const
{
    if (m_clipEditorPreviewSourceTrackId.isNull()
        || !m_audioEngine->isTrackPlaying(m_clipEditorPreviewTrackId))
    {
        return std::nullopt;
    }

    const auto clipEndMs = std::max(m_clipEditorPreviewClipStartMs + 1, m_clipEditorPreviewClipEndMs);
    const auto elapsedMs = m_clipEditorPreviewElapsedTimer.isValid()
        ? static_cast<int>(m_clipEditorPreviewElapsedTimer.elapsed())
        : 0;
    return std::clamp(
        m_clipEditorPreviewStartMs + std::max(0, elapsedMs),
        m_clipEditorPreviewClipStartMs,
        clipEndMs - 1);
}

bool PlayerController::removeAudioFromPool(const QString& filePath)
{
    if (!m_audioPool.remove(filePath))
    {
        return false;
    }

    if (m_audioPoolPreviewAssetPath == filePath)
    {
        stopAudioPoolPreview();
    }

    stopSelectedTrackClipPreview();
    const auto detachedCount = m_tracker.detachTrackAudioByPath(filePath);
    m_audioEngine->stopAll();
    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }
    refreshOverlays();
    emit audioPoolChanged();
    emit statusChanged(
        detachedCount > 0
            ? QStringLiteral("Removed audio from pool and detached it from %1 node(s).").arg(detachedCount)
            : QStringLiteral("Removed audio from the pool."));
    return true;
}

bool PlayerController::removeAudioAndConnectedNodesFromPool(const QString& filePath)
{
    if (!m_audioPool.remove(filePath))
    {
        return false;
    }

    if (m_audioPoolPreviewAssetPath == filePath)
    {
        stopAudioPoolPreview();
    }

    stopSelectedTrackClipPreview();
    std::vector<QUuid> trackIdsToRemove;
    for (const auto& track : m_tracker.tracks())
    {
        if (track.attachedAudio.has_value() && track.attachedAudio->assetPath == filePath)
        {
            trackIdsToRemove.push_back(track.id);
        }
    }

    m_audioEngine->stopAll();
    const auto removedNodeCount = m_tracker.removeTracks(trackIdsToRemove);

    const auto selectedTrackRemoved = !m_selectedTrackIds.empty()
        && std::any_of(
            m_selectedTrackIds.begin(),
            m_selectedTrackIds.end(),
            [&trackIdsToRemove](const QUuid& selectedId)
            {
                return std::find(trackIdsToRemove.begin(), trackIdsToRemove.end(), selectedId) != trackIdsToRemove.end();
            });
    if (selectedTrackRemoved)
    {
        setSelectedTrackId({}, false);
    }

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }

    refreshOverlays();
    emit audioPoolChanged();
    emit trackAvailabilityChanged(hasTracks());
    emit statusChanged(
        removedNodeCount > 0
            ? QStringLiteral("Removed audio and deleted %1 connected node(s).").arg(removedNodeCount)
            : QStringLiteral("Removed audio from the pool."));
    return true;
}

std::vector<AudioPoolItem> PlayerController::audioPoolItems() const
{
    const auto previewAssetPath = m_audioEngine->isTrackPlaying(m_audioPoolPreviewTrackId)
        ? m_audioPoolPreviewAssetPath
        : QString{};
    return m_audioPool.items(m_tracker.tracks(), *m_audioEngine, previewAssetPath);
}

std::vector<MixLaneStrip> PlayerController::mixLaneStrips() const
{
    const auto spans = timelineTrackSpans();
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
                .gainDb = mixLaneGainDb(span.laneIndex),
                .meterLevel = 0.0F,
                .clipCount = 1,
                .muted = isMixLaneMuted(span.laneIndex),
                .soloed = isMixLaneSoloed(span.laneIndex)
            });
            continue;
        }

        ++stripIt->clipCount;
    }

    for (auto& strip : strips)
    {
        float laneLevel = 0.0F;
        for (const auto& track : m_tracker.tracks())
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
            laneLevel = std::max(laneLevel, m_audioEngine->trackLevel(track.id));
        }
        strip.meterLevel = laneLevel;
    }

    return strips;
}

std::vector<TimelineTrackSpan> PlayerController::timelineTrackSpans() const
{
    struct TimelineTrackCandidate
    {
        const TrackPoint* track = nullptr;
        int startFrame = 0;
        int endFrame = 0;
    };

    std::vector<TimelineTrackCandidate> candidates;
    candidates.reserve(m_tracker.tracks().size());

    for (const auto& track : m_tracker.tracks())
    {
        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = track.endFrame.has_value()
            ? std::max(startFrame, *track.endFrame)
            : std::max(startFrame, m_totalFrames > 0 ? (m_totalFrames - 1) : track.startFrame);

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
            .isSelected = isTrackSelected(candidate.track->id)
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

const std::vector<TrackOverlay>& PlayerController::currentOverlays() const
{
    return m_currentOverlays;
}

void PlayerController::advancePlayback()
{
    if (!hasVideoLoaded() || !m_transport.isPlaying())
    {
        return;
    }

    if (const auto loopRange = activeLoopRange(); loopRange.has_value()
        && m_currentFrame.index == loopRange->second)
    {
        if (!loadFrameAt(loopRange->first))
        {
            pause(false);
            return;
        }

        m_playbackStartTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
        m_playbackElapsedTimer.restart();
        m_perfPlaybackTickTimer.restart();
        syncAttachedAudioForCurrentFrame();
        return;
    }

    if (!m_playbackElapsedTimer.isValid())
    {
        m_playbackStartTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
        m_playbackElapsedTimer.start();
        stepForward();
        return;
    }

    const auto targetTimestampSeconds =
        m_playbackStartTimestampSeconds + (static_cast<double>(m_playbackElapsedTimer.elapsed()) / 1000.0);

    auto targetFrameIndex = m_videoPlayback.frameIndexForPresentationTime(targetTimestampSeconds, m_currentFrame.index);
    if (const auto loopRange = activeLoopRange(); loopRange.has_value()
        && m_currentFrame.index >= loopRange->first
        && m_currentFrame.index <= loopRange->second)
    {
        targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, loopRange->second);
    }
    else
    {
        targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, std::max(0, m_totalFrames - 1));
    }
    if (targetFrameIndex == m_currentFrame.index)
    {
        return;
    }

    const auto previousFrameIndex = m_currentFrame.index;
    int advancedFrames = 0;
    while (m_transport.isPlaying() && m_currentFrame.index < targetFrameIndex)
    {
        const auto previousStepFrameIndex = m_currentFrame.index;
        const auto shouldPresentFrame = previousStepFrameIndex >= (targetFrameIndex - 1);
        if (!advanceOneFrame(shouldPresentFrame, false))
        {
            break;
        }
        ++advancedFrames;
    }

    if (advancedFrames > 0 && m_transport.isPlaying())
    {
        if (m_currentFrame.index != targetFrameIndex)
        {
            refreshOverlays();
            emitCurrentFrame();
        }
        syncAttachedAudioForCurrentFrame();
    }

    logPlaybackHitchIfNeeded(targetFrameIndex, previousFrameIndex, advancedFrames);
}

void PlayerController::advanceSelectionFade()
{
    if (m_fadingDeselectedTrackId.isNull())
    {
        m_selectionFadeTimer.stop();
        return;
    }

    const auto nextOpacity = m_fadingDeselectedTrackOpacity - 0.18F;
    m_fadingDeselectedTrackOpacity = nextOpacity > 0.0F ? nextOpacity : 0.0F;
    if (m_fadingDeselectedTrackOpacity <= 0.0F)
    {
        m_fadingDeselectedTrackId = {};
        m_selectionFadeTimer.stop();
    }

    refreshOverlays();
}

bool PlayerController::loadFrameAt(const int frameIndex)
{
    if (!hasVideoLoaded() || !m_videoPlayback.seekFrame(frameIndex))
    {
        return false;
    }

    m_currentFrame = m_videoPlayback.currentFrame();
    updateCurrentGrayFrameIfNeeded();
    refreshOverlays();
    emitCurrentFrame();
    return true;
}

void PlayerController::logPlaybackHitchIfNeeded(
    const int targetFrameIndex,
    const int previousFrameIndex,
    const int advancedFrames)
{
    if (!m_transport.isPlaying())
    {
        return;
    }

    const auto stats = m_videoPlayback.runtimeStats();
    const auto tickDeltaMs = m_perfPlaybackTickTimer.isValid() ? m_perfPlaybackTickTimer.restart() : 0;
    const auto skippedFrames = std::max(0, targetFrameIndex - previousFrameIndex);
    const auto shouldLog =
        tickDeltaMs > 40
        || stats.lastStepWaitMs > 4
        || stats.lastStepUsedSynchronousFallback
        || skippedFrames > 1
        || stats.queueStarvationCount > m_lastLoggedQueueStarvationCount;

    if (!shouldLog)
    {
        return;
    }

    m_lastLoggedQueueStarvationCount = stats.queueStarvationCount;
    m_perfLogger.logEvent(
        QStringLiteral("playback_hitch"),
        QStringLiteral(
            "tickMs=%1 currentFrame=%2 targetFrame=%3 advanced=%4 queued=%5/%6 waitMs=%7 syncFallback=%8 starvationCount=%9")
            .arg(tickDeltaMs)
            .arg(m_currentFrame.index)
            .arg(targetFrameIndex)
            .arg(advancedFrames)
            .arg(stats.queuedFrames)
            .arg(stats.prefetchTargetFrames)
            .arg(stats.lastStepWaitMs)
            .arg(stats.lastStepUsedSynchronousFallback ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(stats.queueStarvationCount));
}

bool PlayerController::needsTrackingFrameProcessing() const
{
    return m_tracker.hasMotionTrackedTracks();
}

void PlayerController::updateCurrentGrayFrameIfNeeded()
{
    if (!needsTrackingFrameProcessing())
    {
        m_currentGrayFrame.release();
        return;
    }

    m_currentGrayFrame = m_analysisFrameProvider.grayscaleFrame(m_currentFrame);
}

double PlayerController::frameTimestampSeconds(const int frameIndex) const
{
    return m_videoPlayback.frameTimestampSeconds(frameIndex);
}

std::optional<int> PlayerController::audioDurationMs(const QString& filePath) const
{
    if (filePath.isEmpty())
    {
        return std::nullopt;
    }

    if (const auto cachedIt = m_audioDurationMsByPath.constFind(filePath); cachedIt != m_audioDurationMsByPath.cend())
    {
        return cachedIt.value();
    }

    const auto durationMs = m_audioEngine->durationMs(filePath);
    m_audioDurationMsByPath.insert(filePath, durationMs);
    return durationMs;
}

std::optional<int> PlayerController::trimmedEndFrameForTrack(const TrackPoint& track) const
{
    if (!track.attachedAudio.has_value() || !hasVideoLoaded())
    {
        return std::nullopt;
    }

    const auto durationMs = audioDurationMs(track.attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 0)
    {
        return std::nullopt;
    }

    const auto clipDurationMs = audioClipDurationMs(*track.attachedAudio, *durationMs);
    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto startFrame = std::clamp(track.startFrame, 0, maxFrameIndex);
    const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
    const auto coveredFrames = std::max(
        1,
        static_cast<int>(std::ceil((static_cast<double>(clipDurationMs) * safeFps) / 1000.0)));
    const auto endFrame = startFrame + coveredFrames - 1;
    return std::clamp(endFrame, startFrame, maxFrameIndex);
}

void PlayerController::saveUndoState()
{
    m_undoTrackerState = m_tracker.snapshotState();
    m_undoSelectedTrackIds = m_selectedTrackIds;
    m_redoTrackerState.reset();
    m_redoSelectedTrackIds.clear();
    emit editStateChanged();
}

void PlayerController::restoreTrackEditState(
    const MotionTrackerState& trackerState,
    const std::vector<QUuid>& selectedTrackIds)
{
    m_audioEngine->stopAll();
    m_tracker.restoreState(trackerState);

    m_selectedTrackIds.clear();
    for (const auto& trackId : selectedTrackIds)
    {
        if (m_tracker.hasTrack(trackId))
        {
            m_selectedTrackIds.push_back(trackId);
        }
    }
    m_selectedTrackId = m_selectedTrackIds.empty() ? QUuid{} : m_selectedTrackIds.front();
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();

    refreshOverlays();
    emit selectionChanged(!m_selectedTrackIds.empty());
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }
}

void PlayerController::refreshOverlays()
{
    m_currentOverlays = m_tracker.overlaysForFrame(
        m_currentFrame.index,
        m_selectedTrackIds,
        m_fadingDeselectedTrackId,
        m_fadingDeselectedTrackOpacity);
    emit overlaysChanged();
}

bool PlayerController::isTrackSelected(const QUuid& trackId) const
{
    return std::find(m_selectedTrackIds.begin(), m_selectedTrackIds.end(), trackId) != m_selectedTrackIds.end();
}

void PlayerController::setSelectedTrackId(const QUuid& trackId, const bool fadePreviousSelection)
{
    if (m_selectedTrackId != trackId)
    {
        stopSelectedTrackClipPreview();
    }

    if (m_selectedTrackId == trackId)
    {
        if (trackId.isNull() && m_selectedTrackIds.empty())
        {
            return;
        }
        if (!trackId.isNull() && m_selectedTrackIds.size() == 1 && m_selectedTrackIds.front() == trackId)
        {
            return;
        }
    }

    if (trackId.isNull() && m_selectedTrackIds.empty())
    {
        return;
    }

    if (fadePreviousSelection && !m_selectedTrackId.isNull())
    {
        m_fadingDeselectedTrackId = m_selectedTrackId;
        m_fadingDeselectedTrackOpacity = 1.0F;
        m_selectionFadeTimer.start();
    }
    else if (!fadePreviousSelection)
    {
        m_fadingDeselectedTrackId = {};
        m_fadingDeselectedTrackOpacity = 0.0F;
        m_selectionFadeTimer.stop();
    }

    m_selectedTrackId = trackId;
    m_selectedTrackIds.clear();
    if (!trackId.isNull())
    {
        m_selectedTrackIds.push_back(trackId);
    }
    refreshOverlays();
    emit selectionChanged(!m_selectedTrackIds.empty());
}

void PlayerController::emitCurrentFrame()
{
    emit frameReady(
        m_renderService.presentFrame(m_currentFrame, m_transport.isPlaying()),
        m_currentFrame.index,
        m_currentFrame.timestampSeconds);
}

bool PlayerController::applyPresentationScaleForPlaybackState(const bool playbackActive)
{
    const auto targetScale = (m_renderService.fastPlaybackEnabled() && playbackActive) ? 0.5 : 1.0;
    if (!m_videoPlayback.setPresentationScale(targetScale))
    {
        return false;
    }

    m_currentFrame = m_videoPlayback.currentFrame();
    updateCurrentGrayFrameIfNeeded();
    refreshOverlays();
    return true;
}

float PlayerController::mixLaneGainDb(const int laneIndex) const
{
    const auto gainIt = m_mixLaneGainDbByLane.find(laneIndex);
    return gainIt != m_mixLaneGainDbByLane.end() ? gainIt->second : 0.0F;
}

bool PlayerController::isMixLaneMuted(const int laneIndex) const
{
    const auto mutedIt = m_mixLaneMutedByLane.find(laneIndex);
    return mutedIt != m_mixLaneMutedByLane.end() && mutedIt->second;
}

bool PlayerController::isMixLaneSoloed(const int laneIndex) const
{
    const auto soloIt = m_mixLaneSoloByLane.find(laneIndex);
    return soloIt != m_mixLaneSoloByLane.end() && soloIt->second;
}

bool PlayerController::anyMixLaneSoloed() const
{
    return std::any_of(
        m_mixLaneSoloByLane.begin(),
        m_mixLaneSoloByLane.end(),
        [](const auto& entry)
        {
            return entry.second;
        });
}

std::optional<std::pair<int, int>> PlayerController::activeLoopRange() const
{
    if (!m_loopStartFrame.has_value() || !m_loopEndFrame.has_value())
    {
        return std::nullopt;
    }

    const auto startFrame = std::min(*m_loopStartFrame, *m_loopEndFrame);
    const auto endFrame = std::max(*m_loopStartFrame, *m_loopEndFrame);
    if (endFrame <= startFrame)
    {
        return std::nullopt;
    }

    return std::pair<int, int>{startFrame, endFrame};
}

void PlayerController::applyLiveMixStateToCurrentPlayback()
{
    if (!hasVideoLoaded() || !m_transport.isPlaying())
    {
        return;
    }

    m_audioEngine->setMasterGain(m_masterMixMuted ? kSilentMixGainDb : m_masterMixGainDb);

    QHash<QUuid, int> laneByTrackId;
    const auto spans = timelineTrackSpans();
    laneByTrackId.reserve(static_cast<int>(spans.size()));
    for (const auto& span : spans)
    {
        laneByTrackId.insert(span.id, span.laneIndex);
    }

    const auto anySoloed = anyMixLaneSoloed();
    if (hasEmbeddedVideoAudio() && !m_embeddedVideoAudioMuted)
    {
        m_audioEngine->setTrackGain(m_embeddedVideoAudioTrackId, anySoloed ? kSilentMixGainDb : 0.0F);
    }

    for (const auto& track : m_tracker.tracks())
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }

        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = track.endFrame.has_value()
            ? std::max(startFrame, *track.endFrame)
            : std::max(startFrame, m_totalFrames > 0 ? (m_totalFrames - 1) : startFrame);
        if (m_currentFrame.index < startFrame || m_currentFrame.index > endFrame)
        {
            continue;
        }

        const auto laneIndex = laneByTrackId.value(track.id, 0);
        const auto laneAudible = !isMixLaneMuted(laneIndex) && (!anySoloed || isMixLaneSoloed(laneIndex));
        const auto trackGainDb = laneAudible
            ? (track.attachedAudio->gainDb + mixLaneGainDb(laneIndex))
            : kSilentMixGainDb;
        m_audioEngine->setTrackGain(track.id, trackGainDb);
    }
}

void PlayerController::syncAttachedAudioForCurrentFrame()
{
    if (!hasVideoLoaded())
    {
        m_audioEngine->stopAll();
        return;
    }

    const auto currentTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
    const auto currentOffsetMs =
        std::max(0, static_cast<int>(std::lround(currentTimestampSeconds * 1000.0)));
    m_audioEngine->setMasterGain(m_masterMixMuted ? kSilentMixGainDb : m_masterMixGainDb);

    QHash<QUuid, int> laneByTrackId;
    const auto spans = timelineTrackSpans();
    laneByTrackId.reserve(static_cast<int>(spans.size()));
    for (const auto& span : spans)
    {
        laneByTrackId.insert(span.id, span.laneIndex);
    }
    const auto anySoloed = anyMixLaneSoloed();

    if (hasEmbeddedVideoAudio() && !m_embeddedVideoAudioMuted)
    {
        m_audioEngine->playTrack(m_embeddedVideoAudioTrackId, m_embeddedVideoAudioPath, currentOffsetMs);
        m_audioEngine->setTrackGain(m_embeddedVideoAudioTrackId, anySoloed ? kSilentMixGainDb : 0.0F);
        m_audioEngine->setTrackPan(m_embeddedVideoAudioTrackId, 0.0F);
    }
    else
    {
        m_audioEngine->stopTrack(m_embeddedVideoAudioTrackId);
    }

    for (const auto& track : m_tracker.tracks())
    {
        if (!track.attachedAudio.has_value())
        {
            m_audioEngine->stopTrack(track.id);
            continue;
        }

        const auto startFrame = std::max(0, track.startFrame);
        const auto endFrame = track.endFrame.has_value()
            ? std::max(startFrame, *track.endFrame)
            : std::max(startFrame, m_totalFrames > 0 ? (m_totalFrames - 1) : startFrame);
        if (m_currentFrame.index < startFrame || m_currentFrame.index > endFrame)
        {
            m_audioEngine->stopTrack(track.id);
            continue;
        }

        const auto startTimestampSeconds = frameTimestampSeconds(startFrame);
        const auto elapsedWithinNodeMs = std::max(
            0,
            static_cast<int>(std::lround((currentTimestampSeconds - startTimestampSeconds) * 1000.0)));
        const auto sourceDurationMs = audioDurationMs(track.attachedAudio->assetPath).value_or(0);
        if (sourceDurationMs <= 0)
        {
            m_audioEngine->stopTrack(track.id);
            continue;
        }

        const auto clipStartMs = audioClipStartMs(*track.attachedAudio);
        const auto clipEndMs = audioClipEndMs(*track.attachedAudio, sourceDurationMs);
        const auto clipDurationMs = std::max(1, clipEndMs - clipStartMs);
        if (!track.attachedAudio->loopEnabled && elapsedWithinNodeMs >= clipDurationMs)
        {
            m_audioEngine->stopTrack(track.id);
            continue;
        }

        const auto offsetMs = audioClipPlaybackOffsetMs(
            *track.attachedAudio,
            sourceDurationMs,
            elapsedWithinNodeMs);
        AudioEngine::TrackPlaybackOptions playbackOptions;
        playbackOptions.offsetMs = offsetMs;
        playbackOptions.clipStartMs = clipStartMs;
        playbackOptions.clipEndMs = clipEndMs;
        playbackOptions.loopEnabled = track.attachedAudio->loopEnabled;
        m_audioEngine->playTrack(track.id, track.attachedAudio->assetPath, playbackOptions);
        const auto laneIndex = laneByTrackId.value(track.id, 0);
        const auto laneAudible = !isMixLaneMuted(laneIndex) && (!anySoloed || isMixLaneSoloed(laneIndex));
        const auto trackGainDb = laneAudible
            ? (track.attachedAudio->gainDb + mixLaneGainDb(laneIndex))
            : kSilentMixGainDb;
        m_audioEngine->setTrackGain(track.id, trackGainDb);

        float pan = 0.0F;
        if (track.autoPanEnabled && m_currentFrame.cpuBgr.cols > 1)
        {
            const auto samplePoint = track.interpolatedSampleAt(m_currentFrame.index).value_or(QPointF{});
            const auto normalizedPan = ((samplePoint.x() / static_cast<double>(m_currentFrame.cpuBgr.cols - 1)) * 2.0) - 1.0;
            pan = std::clamp(static_cast<float>(normalizedPan), -1.0F, 1.0F);
        }
        m_audioEngine->setTrackPan(track.id, pan);
    }
}
