#include "app/PlayerController.h"

#include <algorithm>
#include <cmath>

#include "app/AudioPlaybackCoordinator.h"
#include "app/ClipEditorSession.h"
#include "app/MixStateStore.h"
#include "app/NodeController.h"
#include "app/ProjectSessionAdapter.h"
#include "app/SelectionController.h"
#include "app/TimelineLayoutService.h"
#include "app/TrackEditService.h"
#include "app/VideoPlaybackCoordinator.h"
#include <QHash>
#include <QDir>
#include <QFileInfo>

#include "core/audio/VideoAudioExtractor.h"

namespace
{
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedAbsolutePath(const QString& path)
{
    if (path.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool pathsMatch(const QString& left, const QString& right)
{
    return QString::compare(normalizedAbsolutePath(left), normalizedAbsolutePath(right), pathCaseSensitivity()) == 0;
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

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
}

void updateSmoothedMs(double& target, const double sampleMs)
{
    constexpr double kBlend = 0.2;
    target = target <= 0.0 ? sampleMs : ((target * (1.0 - kBlend)) + (sampleMs * kBlend));
}
}

PlayerController::PlayerController(QObject* parent)
    : QObject(parent)
    , m_transport(this)
    , m_audioEngine(AudioEngine::create(this))
{
    m_audioPlaybackCoordinator = std::make_unique<AudioPlaybackCoordinator>(*m_audioEngine);
    m_videoPlaybackCoordinator = std::make_unique<VideoPlaybackCoordinator>(m_tracker, m_transport, m_perfLogger);
    m_nodeController = std::make_unique<NodeController>(*this);
    m_selectionController = std::make_unique<SelectionController>();
    m_trackEditService = std::make_unique<TrackEditService>(m_tracker, m_audioPool);
    m_mixStateStore = std::make_unique<MixStateStore>();
    m_clipEditorSession = std::make_unique<ClipEditorSession>(
        m_tracker,
        *m_audioEngine,
        *m_audioPlaybackCoordinator,
        m_clipEditorPreviewStopTimer);
    connect(&m_transport, &TransportController::playbackAdvanceRequested, this, &PlayerController::advancePlayback);
    connect(&m_transport, &TransportController::playbackStateChanged, this, &PlayerController::playbackStateChanged);
    connect(
        &m_transport,
        &TransportController::insertionFollowsPlaybackChanged,
        this,
        &PlayerController::insertionFollowsPlaybackChanged);
    connect(m_audioEngine.get(), &AudioEngine::statusChanged, this, &PlayerController::statusChanged);
    if (!m_audioEngine->isReady())
    {
        const auto initializationError = m_audioEngine->initializationError();
        emit statusChanged(
            initializationError.isEmpty()
                ? QStringLiteral("Audio backend is unavailable.")
                : initializationError);
    }
    m_selectionFadeTimer.setInterval(30);
    connect(
        &m_selectionFadeTimer,
        &QTimer::timeout,
        this,
        &PlayerController::advanceSelectionFade);
    m_clipEditorPreviewStopTimer.setSingleShot(true);
    connect(&m_clipEditorPreviewStopTimer, &QTimer::timeout, this, &PlayerController::handleClipEditorPreviewTimeout);
    m_mixStateStore->applyMasterGain(*m_audioEngine);
}

PlayerController::~PlayerController() = default;

void PlayerController::clearProjectStateAfterMediaStop(const bool resetVideoPlaybackState)
{
    m_tracker.reset();
    m_currentOverlays.clear();
    m_selectionController->reset();
    m_selectionFadeTimer.stop();
    m_audioPool.clear();
    m_trackEditService->reset();
    m_undoTrackerState.reset();
    m_undoSelectedTrackIds.clear();
    m_redoTrackerState.reset();
    m_redoSelectedTrackIds.clear();
    m_loopRanges.clear();
    m_mixStateStore->reset();
    m_audioDurationMsByPath.clear();
    m_audioChannelCountByPath.clear();
    m_playbackDebugStats = {};
    m_clipEditorSession->reset();
    m_audioPlaybackCoordinator->reset();
    m_mixStateStore->applyMasterGain(*m_audioEngine);
    m_embeddedVideoAudioMuted = true;
    if (resetVideoPlaybackState)
    {
        m_videoPlaybackCoordinator->resetState();
    }
}

void PlayerController::resetProjectState()
{
    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();
    pause(false);
    m_audioEngine->stopAll();
    m_videoPlaybackCoordinator->setProxyPresentationEnabled(false);
    m_videoPlaybackCoordinator->close();
    clearProjectStateAfterMediaStop(true);
    m_projectVideoPath.clear();
    m_proxyVideoPath.clear();
    m_videoPlaybackCoordinator->renderService().setFastPlaybackEnabled(false);
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
    emit mixSoloModeChanged(isMixSoloXorMode());
    emit motionTrackingChanged(false);
}

bool PlayerController::openVideo(const QString& filePath)
{
    m_projectVideoPath = filePath;
    m_proxyVideoPath.clear();
    return openPlaybackVideo(filePath);
}

bool PlayerController::refreshPlaybackSource(QString* errorMessage)
{
    if (m_projectVideoPath.isEmpty())
    {
        return true;
    }

    const auto playbackPath = preferredPlaybackPath();
    if (playbackPath.isEmpty() || playbackPath == m_videoPlaybackCoordinator->loadedPath())
    {
        return true;
    }

    return restoreProjectState(snapshotProjectState(), errorMessage);
}

bool PlayerController::openPlaybackVideo(const QString& filePath)
{
    stopAudioPoolPreview();
    stopSelectedTrackClipPreview();
    pause(false);
    m_audioEngine->stopTrack(m_embeddedVideoAudioTrackId);
    m_videoPlaybackCoordinator->setProxyPresentationEnabled(pathsMatch(filePath, m_proxyVideoPath));
    const auto openResult = m_videoPlaybackCoordinator->openVideo(filePath);
    if (!openResult.success)
    {
        emit statusChanged(openResult.errorMessage);
        return false;
    }

    clearProjectStateAfterMediaStop(false);
    refreshOverlays();
    emitCurrentFrame();
    emit videoLoaded(m_videoPlaybackCoordinator->loadedPath(), m_videoPlaybackCoordinator->totalFrames(), m_videoPlaybackCoordinator->fps());
    emit videoAudioStateChanged();
    emit loopRangeChanged();
    emit selectionChanged(false);
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
    emit editStateChanged();
    emit mixSoloModeChanged(isMixSoloXorMode());
    emit statusChanged(
        QStringLiteral("Loaded %1 via %2 decode and %3 render.")
            .arg(m_projectVideoPath.isEmpty() ? filePath : m_projectVideoPath)
            .arg(m_videoPlaybackCoordinator->decoderBackendName())
            .arg(m_videoPlaybackCoordinator->renderBackendName()));

    return true;
}

dawg::project::ControllerState PlayerController::snapshotProjectState() const
{
    auto state = ProjectSessionAdapter::snapshot(ProjectSessionAdapter::SnapshotInput{
        .videoPath = m_projectVideoPath,
        .audioPool = m_audioPool,
        .tracker = m_tracker,
        .selectedTrackIds = m_selectionController->selectedTrackIds(),
        .currentFrameIndex = m_videoPlaybackCoordinator->currentFrame().index,
        .motionTrackingEnabled = m_motionTrackingEnabled,
        .insertionFollowsPlayback = m_transport.insertionFollowsPlayback(),
        .fastPlaybackEnabled = m_videoPlaybackCoordinator->renderService().fastPlaybackEnabled(),
        .embeddedVideoAudioMuted = m_embeddedVideoAudioMuted,
        .loopRanges = m_loopRanges,
        .masterMixGainDb = m_mixStateStore->masterGainDb(),
        .masterMixMuted = m_mixStateStore->masterMuted(),
        .mixSoloXorMode = m_mixStateStore->soloMode() == MixStateStore::SoloMode::Xor,
        .mixLaneGainDbByLane = m_mixStateStore->gainByLane(),
        .mixLaneMutedByLane = m_mixStateStore->mutedByLane(),
        .mixLaneSoloByLane = m_mixStateStore->soloByLane(),
        .clipEditorPlayheads = m_clipEditorSession->playheads()
    });
    state.videoPath = m_projectVideoPath;
    state.proxyVideoPath = m_proxyVideoPath;
    return state;
}

bool PlayerController::restoreProjectState(const dawg::project::ControllerState& state, QString* errorMessage)
{
    if (!ProjectSessionAdapter::validate(state, errorMessage))
    {
        return false;
    }

    if (state.videoPath.isEmpty())
    {
        resetProjectState();
    }
    else
    {
        m_projectVideoPath = state.videoPath;
        m_proxyVideoPath = state.proxyVideoPath;
        if (!openPlaybackVideo(resolvedPlaybackPath(m_projectVideoPath, m_proxyVideoPath)))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to open project video: %1").arg(state.videoPath);
            }
            return false;
        }
    }

    const auto payload = ProjectSessionAdapter::buildRestorePayload(
        state,
        [](const float gainDb)
        {
            return MixStateStore::clampGainDb(gainDb);
        });
    m_audioPool.setAssetPaths(payload.audioPoolAssetPaths);
    m_tracker.restoreState(payload.trackerState);
    m_loopRanges = payload.loopRanges;
    m_mixStateStore->restore(
        payload.masterMixGainDb,
        payload.masterMixMuted,
        payload.mixLaneGainDbByLane,
        payload.mixLaneMutedByLane,
        payload.mixLaneSoloByLane,
        payload.mixSoloXorMode ? MixStateStore::SoloMode::Xor : MixStateStore::SoloMode::Latch);
    m_clipEditorSession->setPlayheads(payload.clipEditorPlayheads);

    m_motionTrackingEnabled = payload.motionTrackingEnabled;
    m_transport.setInsertionFollowsPlayback(payload.insertionFollowsPlayback);
    m_videoPlaybackCoordinator->renderService().setFastPlaybackEnabled(payload.fastPlaybackEnabled);
    m_embeddedVideoAudioMuted = payload.embeddedVideoAudioMuted;
    m_mixStateStore->applyMasterGain(*m_audioEngine);

    const auto validSelection = ProjectSessionAdapter::filterExistingSelection(
        payload.selectedTrackIds,
        [this](const QUuid& trackId)
        {
            return m_tracker.hasTrack(trackId);
        });
    static_cast<void>(m_selectionController->setSelectedTrackIds(validSelection));
    m_selectionFadeTimer.stop();

    refreshOverlays();
    if (hasVideoLoaded())
    {
        const auto maxFrameIndex = std::max(0, m_videoPlaybackCoordinator->totalFrames() - 1);
        const auto clampedFrameIndex = std::clamp(payload.currentFrameIndex, 0, maxFrameIndex);
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
    emit selectionChanged(m_selectionController->hasSelection());
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();
    emit editStateChanged();
    emit mixSoloModeChanged(isMixSoloXorMode());
    emit motionTrackingChanged(m_motionTrackingEnabled);
    return true;
}

bool PlayerController::importAudioToPool(const QString& filePath)
{
    const auto result = m_trackEditService->importAudioToPool(filePath);
    if (!result.accepted)
    {
        return false;
    }

    if (result.poolChanged)
    {
        emit audioPoolChanged();
    }

    return true;
}

bool PlayerController::setSelectedTrackClipPlayheadMs(const int playheadMs)
{
    return m_clipEditorSession->setSelectedTrackClipPlayheadMs(
        hasVideoLoaded(),
        m_selectionController->selectedTrackId(),
        playheadMs,
        [this](const QString& filePath)
        {
            return audioDurationMs(filePath);
        });
}

bool PlayerController::setSelectedTrackAudioGainDb(const float gainDb)
{
    if (!m_clipEditorSession->setSelectedTrackAudioGainDb(
            hasVideoLoaded(),
            m_selectionController->selectedTrackId(),
            gainDb,
            m_transport.isPlaying(),
            [this]()
            {
                applyLiveMixStateToCurrentPlayback();
            }))
    {
        return false;
    }

    emit editStateChanged();
    return true;
}

bool PlayerController::setSelectedTrackLoopEnabled(const bool enabled)
{
    if (!m_clipEditorSession->setSelectedTrackLoopEnabled(
            hasVideoLoaded(),
            m_selectionController->selectedTrackId(),
            enabled,
            m_transport.isPlaying(),
            [this]()
            {
                syncAttachedAudioForCurrentFrame();
            }))
    {
        return false;
    }

    emit editStateChanged();
    return true;
}

bool PlayerController::startAudioPoolPreview(const QString& filePath)
{
    bool stateChanged = false;
    if (!m_audioPlaybackCoordinator->startAudioPoolPreview(filePath, &stateChanged))
    {
        return false;
    }

    if (stateChanged)
    {
        emit audioPoolPlaybackStateChanged();
    }
    return true;
}

void PlayerController::stopAudioPoolPreview()
{
    bool stateChanged = false;
    m_audioPlaybackCoordinator->stopAudioPoolPreview(&stateChanged);
    if (stateChanged)
    {
        emit audioPoolPlaybackStateChanged();
    }
}

bool PlayerController::startSelectedTrackClipPreview()
{
    return m_clipEditorSession->startSelectedTrackClipPreview(
        hasVideoLoaded(),
        m_selectionController->selectedTrackId(),
        m_transport.isPlaying(),
        [this](const bool restorePlaybackAnchor)
        {
            pause(restorePlaybackAnchor);
        },
        [this]()
        {
            stopAudioPoolPreview();
        },
        [this](const QString& filePath)
        {
            return audioDurationMs(filePath);
        });
}

void PlayerController::handleClipEditorPreviewTimeout()
{
    m_clipEditorSession->handlePreviewTimeout(
        [this](const QString& filePath)
        {
            return audioDurationMs(filePath);
        });
}

void PlayerController::stopSelectedTrackClipPreview()
{
    m_clipEditorSession->stopSelectedTrackClipPreview();
}

bool PlayerController::setSelectedTrackClipRangeMs(const int clipStartMs, const int clipEndMs)
{
    if (!m_clipEditorSession->setSelectedTrackClipRangeMs(
            hasVideoLoaded(),
            m_selectionController->selectedTrackId(),
            clipStartMs,
            clipEndMs,
            m_transport.isPlaying(),
            [this]()
            {
                syncAttachedAudioForCurrentFrame();
            },
            [this]()
            {
                refreshOverlays();
            },
            [this](const QString& filePath)
            {
                return audioDurationMs(filePath);
            }))
    {
        return false;
    }

    emit editStateChanged();
    return true;
}

bool PlayerController::addLoopRange(const int startFrame, const int endFrame)
{
    if (!hasVideoLoaded())
    {
        return false;
    }

    const auto maxFrameIndex = std::max(0, m_videoPlaybackCoordinator->totalFrames() - 1);
    auto normalizedRange = TimelineLoopRange{
        .startFrame = std::clamp(std::min(startFrame, endFrame), 0, maxFrameIndex),
        .endFrame = std::clamp(std::max(startFrame, endFrame), 0, maxFrameIndex)
    };
    if (normalizedRange.endFrame < normalizedRange.startFrame
        || loopRangeOverlaps(normalizedRange))
    {
        return false;
    }

    m_loopRanges.push_back(normalizedRange);
    std::sort(
        m_loopRanges.begin(),
        m_loopRanges.end(),
        [](const TimelineLoopRange& left, const TimelineLoopRange& right)
        {
            return left.startFrame < right.startFrame;
        });
    emit loopRangeChanged();
    return true;
}

void PlayerController::setLoopStartFrame(const int loopIndex, const int frameIndex)
{
    if (!hasVideoLoaded() || loopIndex < 0 || loopIndex >= static_cast<int>(m_loopRanges.size()))
    {
        return;
    }

    const auto maxFrameIndex = std::max(0, m_videoPlaybackCoordinator->totalFrames() - 1);
    const auto previousEnd = loopIndex > 0 ? m_loopRanges[loopIndex - 1].endFrame : -1;
    auto updatedRange = m_loopRanges[loopIndex];
    const auto maxStartFrame = std::max(0, std::min(updatedRange.endFrame, maxFrameIndex));
    updatedRange.startFrame = std::clamp(
        frameIndex,
        previousEnd,
        maxStartFrame);
    if (updatedRange == m_loopRanges[loopIndex])
    {
        return;
    }

    m_loopRanges[loopIndex] = updatedRange;
    emit loopRangeChanged();
}

void PlayerController::setLoopEndFrame(const int loopIndex, const int frameIndex)
{
    if (!hasVideoLoaded() || loopIndex < 0 || loopIndex >= static_cast<int>(m_loopRanges.size()))
    {
        return;
    }

    const auto maxFrameIndex = std::max(0, m_videoPlaybackCoordinator->totalFrames() - 1);
    const auto nextStart = loopIndex + 1 < static_cast<int>(m_loopRanges.size())
        ? m_loopRanges[loopIndex + 1].startFrame
        : maxFrameIndex + 1;
    auto updatedRange = m_loopRanges[loopIndex];
    const auto minEndFrame = std::min(maxFrameIndex, std::max(updatedRange.startFrame, 0));
    updatedRange.endFrame = std::clamp(
        frameIndex,
        minEndFrame,
        std::min(nextStart, maxFrameIndex));
    if (updatedRange == m_loopRanges[loopIndex])
    {
        return;
    }

    m_loopRanges[loopIndex] = updatedRange;
    emit loopRangeChanged();
}

void PlayerController::removeLoopRange(const int loopIndex)
{
    if (loopIndex < 0 || loopIndex >= static_cast<int>(m_loopRanges.size()))
    {
        return;
    }

    m_loopRanges.erase(m_loopRanges.begin() + loopIndex);
    emit loopRangeChanged();
}

void PlayerController::clearLoopRanges()
{
    if (m_loopRanges.empty())
    {
        return;
    }

    m_loopRanges.clear();
    emit loopRangeChanged();
}

void PlayerController::setMasterMixGainDb(const float gainDb)
{
    if (!m_mixStateStore->setMasterGainDb(gainDb))
    {
        return;
    }

    m_mixStateStore->applyMasterGain(*m_audioEngine);
}

void PlayerController::setMasterMixMuted(const bool muted)
{
    if (!m_mixStateStore->setMasterMuted(muted))
    {
        return;
    }

    m_mixStateStore->applyMasterGain(*m_audioEngine);
}

void PlayerController::setMixLaneGainDb(const int laneIndex, const float gainDb)
{
    if (!m_mixStateStore->setLaneGainDb(laneIndex, gainDb))
    {
        return;
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

void PlayerController::setMixLaneMuted(const int laneIndex, const bool muted)
{
    if (!m_mixStateStore->setLaneMuted(laneIndex, muted))
    {
        return;
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

void PlayerController::setMixLaneSoloed(const int laneIndex, const bool soloed)
{
    if (!m_mixStateStore->setLaneSoloed(laneIndex, soloed))
    {
        return;
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }
}

void PlayerController::setMixSoloXorMode(const bool enabled)
{
    if (!m_mixStateStore->setSoloMode(enabled ? MixStateStore::SoloMode::Xor : MixStateStore::SoloMode::Latch))
    {
        return;
    }

    if (m_transport.isPlaying())
    {
        applyLiveMixStateToCurrentPlayback();
    }

    emit mixSoloModeChanged(enabled);
}

std::optional<float> PlayerController::adjustMixLaneGainForTrack(const QUuid& trackId, const float deltaDb)
{
    if (trackId.isNull() || std::abs(deltaDb) < 0.001F)
    {
        return std::nullopt;
    }

    const auto laneIndex = mixLaneIndexForTrack(trackId);
    if (!laneIndex.has_value())
    {
        return std::nullopt;
    }

    const auto nextGainDb = MixStateStore::clampGainDb(m_mixStateStore->laneGainDb(*laneIndex) + deltaDb);
    setMixLaneGainDb(*laneIndex, nextGainDb);
    return nextGainDb;
}

std::optional<float> PlayerController::mixLaneGainForTrack(const QUuid& trackId) const
{
    const auto laneIndex = mixLaneIndexForTrack(trackId);
    if (!laneIndex.has_value())
    {
        return std::nullopt;
    }

    return m_mixStateStore->laneGainDb(*laneIndex);
}

bool PlayerController::setMixLaneGainForTrack(const QUuid& trackId, const float gainDb)
{
    const auto laneIndex = mixLaneIndexForTrack(trackId);
    if (!laneIndex.has_value())
    {
        return false;
    }

    setMixLaneGainDb(*laneIndex, gainDb);
    return true;
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
    m_playbackDebugStats = {};
    static_cast<void>(m_videoPlaybackCoordinator->applyPresentationScaleForPlaybackState(
        true,
        [this]()
        {
            refreshOverlays();
        }));
    emitCurrentFrame();
    m_transport.startPlayback(m_videoPlaybackCoordinator->currentFrame().index);
    m_videoPlaybackCoordinator->restartPlaybackTiming();
    const auto stats = m_videoPlaybackCoordinator->runtimeStats();
    m_perfLogger.logEvent(
        QStringLiteral("play"),
        QStringLiteral("frame=%1 time=%2s queue=%3/%4")
            .arg(m_videoPlaybackCoordinator->currentFrame().index)
            .arg(m_videoPlaybackCoordinator->currentFrame().timestampSeconds, 0, 'f', 3)
            .arg(stats.queuedFrames)
            .arg(stats.prefetchTargetFrames));
    syncAttachedAudioForCurrentFrame();
}

void PlayerController::pause(const bool restorePlaybackAnchor)
{
    m_clipEditorPreviewStopTimer.stop();
    m_audioEngine->stopAll();
    m_perfLogger.logEvent(
        QStringLiteral("stop"),
        QStringLiteral("frame=%1 time=%2s")
            .arg(m_videoPlaybackCoordinator->currentFrame().index)
            .arg(m_videoPlaybackCoordinator->currentFrame().timestampSeconds, 0, 'f', 3));
    const auto restoreFrame = m_transport.stopPlayback(m_videoPlaybackCoordinator->currentFrame().index, restorePlaybackAnchor);
    if (restoreFrame < 0)
    {
        return;
    }

    static_cast<void>(m_videoPlaybackCoordinator->applyPresentationScaleForPlaybackState(
        false,
        [this]()
        {
            refreshOverlays();
        }));
    loadFrameAt(restoreFrame);
}

void PlayerController::seekToFrame(const int frameIndex)
{
    const auto outcome = m_videoPlaybackCoordinator->seekToFrame(
        frameIndex,
        m_transport.isPlaying(),
        [this]()
        {
            refreshOverlays();
            emitCurrentFrame();
        },
        [this]()
        {
            syncAttachedAudioForCurrentFrame();
        });
    if (outcome.status == VideoPlaybackCoordinator::SeekOutcome::Status::Failed)
    {
        emit statusChanged(QStringLiteral("Failed to seek to frame %1.").arg(outcome.targetFrameIndex));
        m_perfLogger.logEvent(
            QStringLiteral("seek_failed"),
            QStringLiteral("targetFrame=%1").arg(outcome.targetFrameIndex));
    }
}

void PlayerController::stepForward()
{
    static_cast<void>(m_videoPlaybackCoordinator->stepForward(
        m_transport.isPlaying(),
        VideoPlaybackCoordinator::FrameCallbacks{
            .onFrameChanged = [this]()
            {
                refreshOverlays();
                emitCurrentFrame();
            },
            .onSyncAudio = [this]()
            {
                syncAttachedAudioForCurrentFrame();
            },
            .onPausePlayback = [this](const bool restorePlaybackAnchor)
            {
                pause(restorePlaybackAnchor);
            },
            .onStatusChanged = [this](const QString& message)
            {
                emit statusChanged(message);
            }
        }));
}

bool PlayerController::advanceOneFrame(const bool presentFrame, const bool syncAudio)
{
    return m_videoPlaybackCoordinator->stepForward(
        syncAudio,
        VideoPlaybackCoordinator::FrameCallbacks{
            .onFrameChanged = [this, presentFrame]()
            {
                refreshOverlays();
                if (presentFrame)
                {
                    emitCurrentFrame();
                }
            },
            .onSyncAudio = [this]()
            {
                syncAttachedAudioForCurrentFrame();
            },
            .onPausePlayback = [this](const bool restorePlaybackAnchor)
            {
                pause(restorePlaybackAnchor);
            },
            .onStatusChanged = [this](const QString& message)
            {
                emit statusChanged(message);
            }
        });
}

QString PlayerController::resolvedPlaybackPath(
    const QString& projectVideoPath,
    const QString& proxyVideoPath) const
{
    if (projectVideoPath.isEmpty())
    {
        return {};
    }

    if (m_useProxyVideo
        && !proxyVideoPath.isEmpty()
        && QFileInfo::exists(proxyVideoPath))
    {
        return proxyVideoPath;
    }

    return projectVideoPath;
}

void PlayerController::stepBackward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause(false);

    const auto outcome = m_videoPlaybackCoordinator->seekRelativeFrames(
        -1,
        [this]()
        {
            refreshOverlays();
            emitCurrentFrame();
        });
    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Boundary)
    {
        emit statusChanged(QStringLiteral("Already at the first frame."));
        return;
    }

    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Failed)
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

    const auto outcome = m_videoPlaybackCoordinator->seekRelativeFrames(
        -5,
        [this]()
        {
            refreshOverlays();
            emitCurrentFrame();
        });
    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Boundary)
    {
        emit statusChanged(QStringLiteral("Already at the first frame."));
        return;
    }

    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Failed)
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

    const auto outcome = m_videoPlaybackCoordinator->seekRelativeFrames(
        5,
        [this]()
        {
            refreshOverlays();
            emitCurrentFrame();
        });
    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Boundary)
    {
        emit statusChanged(QStringLiteral("Already at the last frame."));
        return;
    }

    if (outcome.status == VideoPlaybackCoordinator::RelativeSeekOutcome::Status::Failed)
    {
        emit statusChanged(QStringLiteral("Failed to step fast forward."));
    }
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
    if (m_videoPlaybackCoordinator->renderService().fastPlaybackEnabled() == enabled)
    {
        return;
    }

    m_videoPlaybackCoordinator->renderService().setFastPlaybackEnabled(enabled);
    static_cast<void>(m_videoPlaybackCoordinator->applyPresentationScaleForPlaybackState(
        m_transport.isPlaying(),
        [this]()
        {
            refreshOverlays();
        }));
    emit videoAudioStateChanged();
    emitCurrentFrame();
    emit statusChanged(
        enabled
            ? QStringLiteral("Low-Res Playback enabled.")
            : QStringLiteral("Low-Res Playback disabled."));
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

void PlayerController::setUseProxyVideo(const bool enabled)
{
    m_useProxyVideo = enabled;
}

void PlayerController::setProxyVideoPath(const QString& filePath)
{
    m_proxyVideoPath = filePath;
}

void PlayerController::setPreferredD3D11Device(void* device)
{
    m_videoPlaybackCoordinator->setPreferredD3D11Device(device);
}

void PlayerController::setNativeVideoPresentationEnabled(const bool enabled)
{
    m_videoPlaybackCoordinator->setNativePresentationEnabled(enabled);
    if (hasVideoLoaded())
    {
        emitCurrentFrame();
    }
}

bool PlayerController::hasVideoLoaded() const
{
    return m_videoPlaybackCoordinator->hasVideoLoaded();
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
    return m_selectionController->hasSelection();
}

bool PlayerController::hasTracks() const
{
    return !m_tracker.tracks().empty();
}

bool PlayerController::hasEmptyTracks() const
{
    return std::any_of(
        m_tracker.tracks().cbegin(),
        m_tracker.tracks().cend(),
        [](const TrackPoint& track)
        {
            return !track.attachedAudio.has_value();
        });
}

bool PlayerController::canPasteTracks() const
{
    return hasVideoLoaded() && m_trackEditService->hasCopiedTracks();
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
    return m_audioPlaybackCoordinator->isClipPreviewPlaying();
}

int PlayerController::trackCount() const
{
    return static_cast<int>(m_tracker.tracks().size());
}

int PlayerController::currentFrameIndex() const
{
    return m_videoPlaybackCoordinator->currentFrame().index;
}

int PlayerController::totalFrames() const
{
    return m_videoPlaybackCoordinator->totalFrames();
}

double PlayerController::fps() const
{
    return m_videoPlaybackCoordinator->fps();
}

QString PlayerController::loadedPath() const
{
    return m_videoPlaybackCoordinator->loadedPath();
}

QString PlayerController::projectVideoPath() const
{
    return m_projectVideoPath;
}

QString PlayerController::proxyVideoPath() const
{
    return m_proxyVideoPath;
}

QString PlayerController::preferredPlaybackPath() const
{
    return resolvedPlaybackPath(m_projectVideoPath, m_proxyVideoPath);
}

QUuid PlayerController::selectedTrackId() const
{
    return m_selectionController->selectedTrackId();
}

QString PlayerController::decoderBackendName() const
{
    return m_videoPlaybackCoordinator->decoderBackendName();
}

bool PlayerController::videoHardwareAccelerated() const
{
    return m_videoPlaybackCoordinator->videoHardwareAccelerated();
}

QString PlayerController::renderBackendName() const
{
    return m_videoPlaybackCoordinator->renderBackendName();
}

bool PlayerController::renderHardwareAccelerated() const
{
    return m_videoPlaybackCoordinator->renderHardwareAccelerated();
}

RenderService* PlayerController::renderService()
{
    return &m_videoPlaybackCoordinator->renderService();
}

const VideoFrame& PlayerController::currentVideoFrame() const
{
    return m_videoPlaybackCoordinator->currentFrame();
}

bool PlayerController::hasEmbeddedVideoAudio() const
{
    return m_videoPlaybackCoordinator->hasEmbeddedVideoAudio();
}

QString PlayerController::embeddedVideoAudioDisplayName() const
{
    return m_videoPlaybackCoordinator->embeddedVideoAudioDisplayName();
}

bool PlayerController::isEmbeddedVideoAudioMuted() const
{
    return m_embeddedVideoAudioMuted;
}

bool PlayerController::isFastPlaybackEnabled() const
{
    return m_videoPlaybackCoordinator->renderService().fastPlaybackEnabled();
}

bool PlayerController::isFastPlaybackActive() const
{
    return m_videoPlaybackCoordinator->renderService().fastPlaybackEnabled() && m_transport.isPlaying();
}

bool PlayerController::useProxyVideo() const
{
    return m_useProxyVideo;
}

std::vector<TimelineLoopRange> PlayerController::loopRanges() const
{
    return m_loopRanges;
}

float PlayerController::masterMixGainDb() const
{
    return m_mixStateStore->masterGainDb();
}

bool PlayerController::masterMixMuted() const
{
    return m_mixStateStore->masterMuted();
}

bool PlayerController::isMixSoloXorMode() const
{
    return m_mixStateStore->soloMode() == MixStateStore::SoloMode::Xor;
}

float PlayerController::masterMixLevel() const
{
    return m_audioEngine->masterLevel();
}

AudioEngine::StereoLevels PlayerController::masterMixStereoLevels() const
{
    return m_audioEngine->masterStereoLevels();
}

QSize PlayerController::videoFrameSize() const
{
    return QSize{
        m_videoPlaybackCoordinator->currentFrame().frameSize.width,
        m_videoPlaybackCoordinator->currentFrame().frameSize.height};
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
    return m_clipEditorSession->selectedTrackLoopEnabled(
        hasVideoLoaded(),
        m_selectionController->selectedTrackId());
}

bool PlayerController::trackAutoPanEnabled(const QUuid& trackId) const
{
    return m_tracker.isTrackAutoPanEnabled(trackId);
}

bool PlayerController::selectedTracksAutoPanEnabled() const
{
    return m_selectionController->hasSelection()
        && std::all_of(
            m_selectionController->selectedTrackIds().begin(),
            m_selectionController->selectedTrackIds().end(),
            [this](const QUuid& trackId)
            {
                return m_tracker.isTrackAutoPanEnabled(trackId);
            });
}

std::optional<ClipEditorState> PlayerController::selectedClipEditorState() const
{
    return m_clipEditorSession->selectedClipEditorState(
        hasVideoLoaded(),
        m_selectionController->selectedTrackId(),
        m_videoPlaybackCoordinator->currentFrame().index,
        m_videoPlaybackCoordinator->totalFrames(),
        m_transport.isPlaying(),
        [this](const QString& filePath)
        {
            return audioDurationMs(filePath);
        },
        [this](const int frameIndex)
        {
            return frameTimestampSeconds(frameIndex);
        });
}

bool PlayerController::removeAudioFromPool(const QString& filePath)
{
    if (!m_audioPool.remove(filePath))
    {
        return false;
    }

    if (m_audioPlaybackCoordinator->audioPoolPreviewAssetPath() == filePath)
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

    if (m_audioPlaybackCoordinator->audioPoolPreviewAssetPath() == filePath)
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

    const auto selectedTrackRemoved = m_selectionController->hasSelection()
        && std::any_of(
            m_selectionController->selectedTrackIds().begin(),
            m_selectionController->selectedTrackIds().end(),
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
    const auto previewAssetPath = m_audioPlaybackCoordinator->isAudioPoolPreviewPlaying()
        ? m_audioPlaybackCoordinator->audioPoolPreviewAssetPath()
        : QString{};
    return m_audioPool.items(m_tracker.tracks(), *m_audioEngine, previewAssetPath);
}

std::vector<MixLaneStrip> PlayerController::mixLaneStrips() const
{
    const auto spans = timelineTrackSpans();
    auto strips = TimelineLayoutService::mixLaneStrips(
        spans,
        m_tracker.tracks(),
        *m_audioEngine,
        [this](const QString& filePath)
        {
            return audioChannelCount(filePath);
        },
        m_mixStateStore->gainByLane(),
        m_mixStateStore->mutedByLane(),
        m_mixStateStore->soloByLane());

    if (!m_audioPlaybackCoordinator->isClipPreviewPlaying())
    {
        return strips;
    }

    const auto sourceTrackId = m_audioPlaybackCoordinator->clipPreviewSourceTrackId();
    if (sourceTrackId.isNull())
    {
        return strips;
    }

    const auto spanIt = std::find_if(
        spans.begin(),
        spans.end(),
        [&sourceTrackId](const TimelineTrackSpan& span)
        {
            return span.id == sourceTrackId;
        });
    if (spanIt == spans.end())
    {
        return strips;
    }

    const auto previewStereoLevels = m_audioEngine->trackStereoLevels(m_audioPlaybackCoordinator->clipPreviewTrackId());
    for (auto& strip : strips)
    {
        if (strip.laneIndex == spanIt->laneIndex)
        {
            strip.meterLeftLevel = std::max(strip.meterLeftLevel, previewStereoLevels.left);
            strip.meterRightLevel = std::max(strip.meterRightLevel, previewStereoLevels.right);
            strip.meterLevel = std::max(strip.meterLevel, std::max(previewStereoLevels.left, previewStereoLevels.right));
            break;
        }
    }

    return strips;
}

std::vector<MixLaneMeterState> PlayerController::mixLaneMeterStates(const std::vector<TimelineTrackSpan>& spans) const
{
    auto meterStates = TimelineLayoutService::mixLaneMeterStates(spans, m_tracker.tracks(), *m_audioEngine);
    if (!m_audioPlaybackCoordinator->isClipPreviewPlaying())
    {
        return meterStates;
    }

    const auto sourceTrackId = m_audioPlaybackCoordinator->clipPreviewSourceTrackId();
    if (sourceTrackId.isNull())
    {
        return meterStates;
    }

    const auto spanIt = std::find_if(
        spans.begin(),
        spans.end(),
        [&sourceTrackId](const TimelineTrackSpan& span)
        {
            return span.id == sourceTrackId;
        });
    if (spanIt == spans.end())
    {
        return meterStates;
    }

    const auto previewStereoLevels = m_audioEngine->trackStereoLevels(m_audioPlaybackCoordinator->clipPreviewTrackId());
    for (auto& state : meterStates)
    {
        if (state.laneIndex != spanIt->laneIndex)
        {
            continue;
        }

        state.meterLeftLevel = std::max(state.meterLeftLevel, previewStereoLevels.left);
        state.meterRightLevel = std::max(state.meterRightLevel, previewStereoLevels.right);
        state.meterLevel = std::max(state.meterLevel, std::max(previewStereoLevels.left, previewStereoLevels.right));
        break;
    }

    return meterStates;
}

std::vector<TimelineTrackSpan> PlayerController::timelineTrackSpans() const
{
    return TimelineLayoutService::timelineTrackSpans(
        m_tracker.tracks(),
        m_videoPlaybackCoordinator->totalFrames(),
        m_selectionController->selectedTrackIds());
}

const std::vector<TrackOverlay>& PlayerController::currentOverlays() const
{
    return m_currentOverlays;
}

PlaybackDebugStats PlayerController::playbackDebugStats() const
{
    return m_playbackDebugStats;
}

void PlayerController::advancePlayback()
{
    QElapsedTimer advanceTimer;
    advanceTimer.start();
    VideoPlaybackCoordinator::PlaybackCallbacks callbacks;
    callbacks.onFrameChanged = [this]()
    {
        QElapsedTimer frameCallbackTimer;
        frameCallbackTimer.start();
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        refreshOverlays();
        updateSmoothedMs(m_playbackDebugStats.overlayRefreshMs, elapsedMs(overlayTimer));
        emitCurrentFrame();
        updateSmoothedMs(m_playbackDebugStats.frameCallbackMs, elapsedMs(frameCallbackTimer));
    };
    callbacks.onSyncAudio = [this]()
    {
        QElapsedTimer audioTimer;
        audioTimer.start();
        syncAttachedAudioForCurrentFrame();
        updateSmoothedMs(m_playbackDebugStats.syncAudioMs, elapsedMs(audioTimer));
    };
    callbacks.onPausePlayback = [this](const bool restorePlaybackAnchor)
    {
        pause(restorePlaybackAnchor);
    };
    callbacks.onStatusChanged = [this](const QString& message)
    {
        emit statusChanged(message);
    };
    callbacks.activeLoopRange = [this]()
    {
        return activeLoopRange();
    };
    m_videoPlaybackCoordinator->advancePlayback(callbacks);
    updateSmoothedMs(m_playbackDebugStats.advancePlaybackMs, elapsedMs(advanceTimer));
    m_playbackDebugStats.runtimeStats = m_videoPlaybackCoordinator->runtimeStats();
}

void PlayerController::advanceSelectionFade()
{
    if (m_selectionController->fadingDeselectedTrackId().isNull())
    {
        m_selectionFadeTimer.stop();
        return;
    }

    if (!m_selectionController->advanceFade())
    {
        m_selectionFadeTimer.stop();
    }

    refreshOverlays();
}

bool PlayerController::loadFrameAt(const int frameIndex)
{
    const auto loaded = m_videoPlaybackCoordinator->loadFrameAt(
        frameIndex,
        [this]()
        {
            refreshOverlays();
            emitCurrentFrame();
        });
    return loaded;
}

double PlayerController::frameTimestampSeconds(const int frameIndex) const
{
    return m_videoPlaybackCoordinator->frameTimestampSeconds(frameIndex);
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

std::optional<int> PlayerController::audioChannelCount(const QString& filePath) const
{
    if (filePath.isEmpty())
    {
        return std::nullopt;
    }

    if (const auto cachedIt = m_audioChannelCountByPath.constFind(filePath);
        cachedIt != m_audioChannelCountByPath.cend())
    {
        return cachedIt.value();
    }

    const auto channelCount = m_audioEngine->channelCount(filePath);
    m_audioChannelCountByPath.insert(filePath, channelCount);
    return channelCount;
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
    const auto maxFrameIndex = std::max(0, m_videoPlaybackCoordinator->totalFrames() - 1);
    const auto startFrame = std::clamp(track.startFrame, 0, maxFrameIndex);
    const auto safeFps = m_videoPlaybackCoordinator->fps() > 0.0 ? m_videoPlaybackCoordinator->fps() : 30.0;
    const auto coveredFrames = std::max(
        1,
        static_cast<int>(std::ceil((static_cast<double>(clipDurationMs) * safeFps) / 1000.0)));
    const auto endFrame = startFrame + coveredFrames - 1;
    return std::clamp(endFrame, startFrame, maxFrameIndex);
}

void PlayerController::saveUndoState()
{
    m_undoTrackerState = m_tracker.snapshotState();
    m_undoSelectedTrackIds = m_selectionController->selectedTrackIds();
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

    std::vector<QUuid> validSelection;
    validSelection.reserve(selectedTrackIds.size());
    for (const auto& trackId : selectedTrackIds)
    {
        if (m_tracker.hasTrack(trackId))
        {
            validSelection.push_back(trackId);
        }
    }
    static_cast<void>(m_selectionController->setSelectedTrackIds(validSelection));
    m_selectionFadeTimer.stop();

    refreshOverlays();
    emit selectionChanged(m_selectionController->hasSelection());
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();

    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }
}

void PlayerController::refreshOverlays()
{
    QElapsedTimer timer;
    timer.start();
    m_currentOverlays = m_tracker.overlaysForFrame(
        m_videoPlaybackCoordinator->currentFrame().index,
        m_selectionController->selectedTrackIds(),
        m_selectionController->fadingDeselectedTrackId(),
        m_selectionController->fadingDeselectedTrackOpacity());
    updateSmoothedMs(m_playbackDebugStats.overlayBuildMs, elapsedMs(timer));
    m_playbackDebugStats.overlayCount = static_cast<int>(m_currentOverlays.size());
    m_playbackDebugStats.overlayLabelCount = static_cast<int>(std::count_if(
        m_currentOverlays.begin(),
        m_currentOverlays.end(),
        [](const TrackOverlay& overlay)
        {
            return overlay.showLabel;
        }));
    emit overlaysChanged();
}

bool PlayerController::isTrackSelected(const QUuid& trackId) const
{
    return m_selectionController->isTrackSelected(trackId);
}

std::optional<int> PlayerController::mixLaneIndexForTrack(const QUuid& trackId) const
{
    if (trackId.isNull())
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

    return spanIt->laneIndex;
}

void PlayerController::setSelectedTrackId(const QUuid& trackId, const bool fadePreviousSelection)
{
    if (m_selectionController->selectedTrackId() != trackId)
    {
        stopSelectedTrackClipPreview();
    }

    if (!m_selectionController->setSelectedTrackId(trackId, fadePreviousSelection))
    {
        return;
    }

    if (m_selectionController->fadingDeselectedTrackOpacity() > 0.0F)
    {
        m_selectionFadeTimer.start();
    }
    else
    {
        m_selectionFadeTimer.stop();
    }

    refreshOverlays();
    emit selectionChanged(m_selectionController->hasSelection());
}

void PlayerController::emitCurrentFrame()
{
    QElapsedTimer presentTimer;
    presentTimer.start();
    const auto presentedFrame = m_videoPlaybackCoordinator->presentCurrentFrame(m_transport.isPlaying());
    updateSmoothedMs(m_playbackDebugStats.presentFrameMs, elapsedMs(presentTimer));
    QElapsedTimer dispatchTimer;
    dispatchTimer.start();
    emit frameReady(
        presentedFrame.image,
        presentedFrame.frameIndex,
        presentedFrame.timestampSeconds);
    updateSmoothedMs(m_playbackDebugStats.frameReadyDispatchMs, elapsedMs(dispatchTimer));
    m_playbackDebugStats.runtimeStats = m_videoPlaybackCoordinator->runtimeStats();
}

std::optional<std::pair<int, int>> PlayerController::activeLoopRange() const
{
    const auto currentFrameIndex = m_videoPlaybackCoordinator->currentFrame().index;
    for (const auto& loopRange : m_loopRanges)
    {
        if (currentFrameIndex >= loopRange.startFrame && currentFrameIndex <= loopRange.endFrame)
        {
            return std::pair<int, int>{loopRange.startFrame, loopRange.endFrame};
        }
    }
    return std::nullopt;
}

bool PlayerController::loopRangeOverlaps(const TimelineLoopRange& candidate, const int ignoreIndex) const
{
    for (int index = 0; index < static_cast<int>(m_loopRanges.size()); ++index)
    {
        if (index == ignoreIndex)
        {
            continue;
        }

        const auto& existingRange = m_loopRanges[index];
        if (candidate.startFrame < existingRange.endFrame
            && candidate.endFrame > existingRange.startFrame)
        {
            return true;
        }
    }

    return false;
}

void PlayerController::applyLiveMixStateToCurrentPlayback()
{
    if (!hasVideoLoaded() || !m_transport.isPlaying())
    {
        return;
    }

    const auto spans = timelineTrackSpans();
    m_audioPlaybackCoordinator->applyLiveMixStateToCurrentPlayback(
        AudioPlaybackCoordinator::PlaybackSyncRequest{
            .hasVideoLoaded = hasVideoLoaded(),
            .currentFrameIndex = m_videoPlaybackCoordinator->currentFrame().index,
            .totalFrames = m_videoPlaybackCoordinator->totalFrames(),
            .currentFrameWidth = m_videoPlaybackCoordinator->currentFrame().frameSize.width,
            .masterGainDb = m_mixStateStore->masterGainDb(),
            .masterMuted = m_mixStateStore->masterMuted(),
            .embeddedVideoAudioTrackId = m_embeddedVideoAudioTrackId,
            .embeddedVideoAudioPath = m_videoPlaybackCoordinator->embeddedVideoAudioPath(),
            .embeddedVideoAudioMuted = m_embeddedVideoAudioMuted
        },
        spans,
        m_tracker.tracks(),
        m_mixStateStore->gainByLane(),
        m_mixStateStore->mutedByLane(),
        m_mixStateStore->soloByLane());
}

void PlayerController::syncAttachedAudioForCurrentFrame()
{
    const auto spans = timelineTrackSpans();
    m_audioPlaybackCoordinator->syncAttachedAudioForCurrentFrame(
        AudioPlaybackCoordinator::PlaybackSyncRequest{
            .hasVideoLoaded = hasVideoLoaded(),
            .currentFrameIndex = m_videoPlaybackCoordinator->currentFrame().index,
            .totalFrames = m_videoPlaybackCoordinator->totalFrames(),
            .currentFrameWidth = m_videoPlaybackCoordinator->currentFrame().frameSize.width,
            .masterGainDb = m_mixStateStore->masterGainDb(),
            .masterMuted = m_mixStateStore->masterMuted(),
            .embeddedVideoAudioTrackId = m_embeddedVideoAudioTrackId,
            .embeddedVideoAudioPath = m_videoPlaybackCoordinator->embeddedVideoAudioPath(),
            .embeddedVideoAudioMuted = m_embeddedVideoAudioMuted
        },
        spans,
        m_tracker.tracks(),
        m_mixStateStore->gainByLane(),
        m_mixStateStore->mutedByLane(),
        m_mixStateStore->soloByLane(),
        [this](const QString& filePath)
        {
            return audioDurationMs(filePath);
        },
        [this](const int frameIndex)
        {
            return frameTimestampSeconds(frameIndex);
        });
}
