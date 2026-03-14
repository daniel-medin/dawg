#include "app/PlayerController.h"

#include <algorithm>
#include <cmath>

#include <QFileInfo>
#include <QElapsedTimer>

#include "core/audio/VideoAudioExtractor.h"

namespace
{
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
    m_selectionFadeTimer.setInterval(30);
    connect(
        &m_selectionFadeTimer,
        &QTimer::timeout,
        this,
        &PlayerController::advanceSelectionFade);
}

bool PlayerController::openVideo(const QString& filePath)
{
    pause(false);
    m_audioEngine->stopTrack(m_embeddedVideoAudioTrackId);

    if (!m_videoPlayback.open(filePath))
    {
        emit statusChanged(QStringLiteral("Failed to open video: %1").arg(filePath));
        return false;
    }

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
    m_loadedPath = filePath;
    m_embeddedVideoAudioPath.clear();
    m_embeddedVideoAudioDisplayName.clear();
    m_embeddedVideoAudioMuted = true;
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
    setSelectedTrackId(track.id);
    refreshOverlays();

    emit statusChanged(
        QStringLiteral("Added %1 at frame %2 (%3)")
            .arg(track.label)
            .arg(m_currentFrame.index)
            .arg(track.motionTracked ? QStringLiteral("tracked") : QStringLiteral("manual")));
    emit trackAvailabilityChanged(true);
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

    refreshOverlays();
    emit audioPoolChanged();
    emit statusChanged(QStringLiteral("Attached %1 to the selected node.").arg(QFileInfo(filePath).fileName()));
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

bool PlayerController::removeAudioFromPool(const QString& filePath)
{
    if (!m_audioPool.remove(filePath))
    {
        return false;
    }

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
    return m_audioPool.items(m_tracker.tracks(), *m_audioEngine);
}

std::vector<TimelineTrackSpan> PlayerController::timelineTrackSpans() const
{
    std::vector<TimelineTrackSpan> trackSpans;
    trackSpans.reserve(m_tracker.tracks().size());

    for (const auto& track : m_tracker.tracks())
    {
        trackSpans.push_back(TimelineTrackSpan{
            .id = track.id,
            .label = track.label,
            .color = track.color,
            .startFrame = track.startFrame > 0 ? track.startFrame : 0,
            .endFrame = track.endFrame.has_value()
                ? (*track.endFrame > track.startFrame ? *track.endFrame : track.startFrame)
                : ((m_totalFrames > 0 ? m_totalFrames - 1 : track.startFrame) > track.startFrame
                    ? (m_totalFrames > 0 ? m_totalFrames - 1 : track.startFrame)
                    : track.startFrame),
            .isSelected = isTrackSelected(track.id)
        });
    }

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

    targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, std::max(0, m_totalFrames - 1));
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

std::optional<int> PlayerController::trimmedEndFrameForTrack(const TrackPoint& track) const
{
    if (!track.attachedAudio.has_value() || !hasVideoLoaded())
    {
        return std::nullopt;
    }

    const auto durationMs = m_audioEngine->durationMs(track.attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 0)
    {
        return std::nullopt;
    }

    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto startFrame = std::clamp(track.startFrame, 0, maxFrameIndex);
    const auto endExclusiveSeconds = frameTimestampSeconds(startFrame) + (*durationMs / 1000.0);
    int endFrame = startFrame;

    const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
    const auto coveredFrames = std::max(1, static_cast<int>(std::lround((*durationMs * safeFps) / 1000.0)));
    endFrame = std::clamp(
        m_videoPlayback.frameIndexForPresentationTime(endExclusiveSeconds, startFrame),
        startFrame,
        startFrame + coveredFrames);

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

    if (hasEmbeddedVideoAudio() && !m_embeddedVideoAudioMuted)
    {
        m_audioEngine->playTrack(m_embeddedVideoAudioTrackId, m_embeddedVideoAudioPath, currentOffsetMs);
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
        const auto offsetMs = std::max(
            0,
            static_cast<int>(std::lround((currentTimestampSeconds - startTimestampSeconds) * 1000.0)));
        m_audioEngine->playTrack(track.id, track.attachedAudio->assetPath, offsetMs);

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
