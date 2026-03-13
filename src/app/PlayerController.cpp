#include "app/PlayerController.h"

#include <algorithm>
#include <cmath>

#include <QFileInfo>
#include <QImage>

#include <opencv2/imgproc.hpp>

#include "core/video/OpenCvVideoDecoder.h"

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

    auto decoder = std::make_unique<OpenCvVideoDecoder>();
    if (!decoder->open(filePath.toStdString()))
    {
        emit statusChanged(QStringLiteral("Failed to open video: %1").arg(filePath));
        return false;
    }

    const auto firstFrame = decoder->readFrame();
    if (!firstFrame.has_value() || !firstFrame->isValid())
    {
        emit statusChanged(QStringLiteral("The file opened, but no frames were decoded."));
        return false;
    }

    m_tracker.reset();
    m_currentOverlays.clear();
    m_selectedTrackIds.clear();
    m_selectedTrackId = {};
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    m_selectionFadeTimer.stop();
    m_audioPoolPaths.clear();
    m_loadedPath = filePath;
    m_totalFrames = decoder->frameCount();
    m_fps = decoder->fps();
    m_frameTimestampsSeconds = buildFrameTimestampCache(filePath);
    m_currentFrame = *firstFrame;
    m_decoder = std::move(decoder);

    cv::cvtColor(m_currentFrame.bgr, m_currentGrayFrame, cv::COLOR_BGR2GRAY);
    const auto safeFps = m_fps > 1.0 ? m_fps : 1.0;
    const auto playbackIntervalMs = static_cast<int>(1000.0 / safeFps);
    m_transport.setPlaybackIntervalMs(playbackIntervalMs);

    refreshOverlays();
    emitCurrentFrame();
    emit videoLoaded(m_loadedPath, m_totalFrames, m_fps);
    emit selectionChanged(false);
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
    emit statusChanged(QStringLiteral("Loaded %1").arg(filePath));

    return true;
}

bool PlayerController::importAudioToPool(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    if (std::find(m_audioPoolPaths.begin(), m_audioPoolPaths.end(), filePath) == m_audioPoolPaths.end())
    {
        m_audioPoolPaths.push_back(filePath);
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
    syncAttachedAudioForCurrentFrame();
}

void PlayerController::pause(const bool restorePlaybackAnchor)
{
    m_audioEngine->stopAll();
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

    pause(false);

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto clampedFrameIndex = frameIndex < 0 ? 0 : (frameIndex > maxFrameIndex ? maxFrameIndex : frameIndex);
    if (clampedFrameIndex == m_currentFrame.index)
    {
        return;
    }

    if (!loadFrameAt(clampedFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to seek to frame %1.").arg(clampedFrameIndex));
    }
}

void PlayerController::stepForward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    const auto previousGrayFrame = m_currentGrayFrame.clone();
    const auto nextFrame = m_decoder->readFrame();
    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        pause();
        emit statusChanged(QStringLiteral("Reached the end of the clip."));
        return;
    }

    cv::Mat nextGrayFrame;
    cv::cvtColor(nextFrame->bgr, nextGrayFrame, cv::COLOR_BGR2GRAY);
    m_tracker.trackForward(previousGrayFrame, nextGrayFrame, nextFrame->index);

    m_currentFrame = *nextFrame;
    m_currentGrayFrame = nextGrayFrame;

    refreshOverlays();
    emitCurrentFrame();
    if (m_transport.isPlaying())
    {
        syncAttachedAudioForCurrentFrame();
    }
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
        static_cast<double>(m_currentFrame.bgr.cols) * 0.5,
        static_cast<double>(m_currentFrame.bgr.rows) * 0.5
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

void PlayerController::clearSelection()
{
    setSelectedTrackId({});
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

void PlayerController::deleteSelectedTrack()
{
    if (m_selectedTrackIds.empty())
    {
        return;
    }

    const auto removedCount = m_selectedTrackIds.size() == 1
        ? (m_tracker.removeTrack(m_selectedTrackIds.front()) ? 1 : 0)
        : m_tracker.removeTracks(m_selectedTrackIds);
    if (removedCount <= 0)
    {
        setSelectedTrackId({}, false);
        emit statusChanged(QStringLiteral("The selected node selection no longer exists."));
        return;
    }

    setSelectedTrackId({}, false);
    refreshOverlays();
    emit trackAvailabilityChanged(hasTracks());
    emit audioPoolChanged();
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

    m_tracker.reset();
    m_selectedTrackIds.clear();
    setSelectedTrackId({}, false);
    refreshOverlays();
    emit trackAvailabilityChanged(false);
    emit audioPoolChanged();
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
    return m_decoder != nullptr && m_currentFrame.isValid();
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

bool PlayerController::removeAudioFromPool(const QString& filePath)
{
    const auto removeIt = std::remove(m_audioPoolPaths.begin(), m_audioPoolPaths.end(), filePath);
    if (removeIt == m_audioPoolPaths.end())
    {
        return false;
    }

    m_audioPoolPaths.erase(removeIt, m_audioPoolPaths.end());
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
    const auto removeIt = std::remove(m_audioPoolPaths.begin(), m_audioPoolPaths.end(), filePath);
    if (removeIt == m_audioPoolPaths.end())
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

    m_audioPoolPaths.erase(removeIt, m_audioPoolPaths.end());
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
    std::vector<AudioPoolItem> items;
    items.reserve(m_audioPoolPaths.size());

    for (const auto& assetPath : m_audioPoolPaths)
    {
        int connectedNodeCount = 0;
        bool isPlaying = false;
        QString firstConnectedLabel;

        for (const auto& track : m_tracker.tracks())
        {
            if (!track.attachedAudio.has_value() || track.attachedAudio->assetPath != assetPath)
            {
                continue;
            }

            ++connectedNodeCount;
            if (firstConnectedLabel.isEmpty())
            {
                firstConnectedLabel = track.label;
            }
            if (m_audioEngine->isTrackPlaying(track.id))
            {
                isPlaying = true;
            }
        }

        QString connectionSummary = QStringLiteral("Not connected");
        if (isPlaying)
        {
            connectionSummary = connectedNodeCount == 1
                ? QStringLiteral("Playing on %1").arg(firstConnectedLabel)
                : QStringLiteral("Playing on %1 nodes").arg(connectedNodeCount);
        }
        else if (connectedNodeCount == 1)
        {
            connectionSummary = QStringLiteral("Connected to %1").arg(firstConnectedLabel);
        }
        else if (connectedNodeCount > 1)
        {
            connectionSummary = QStringLiteral("Connected to %1 nodes").arg(connectedNodeCount);
        }

        items.push_back(AudioPoolItem{
            .assetPath = assetPath,
            .displayName = QFileInfo(assetPath).fileName(),
            .connectedNodeCount = connectedNodeCount,
            .isPlaying = isPlaying,
            .connectionSummary = connectionSummary
        });
    }

    return items;
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

    int targetFrameIndex = m_currentFrame.index;
    if (!m_frameTimestampsSeconds.empty())
    {
        const auto endIt = std::upper_bound(
            m_frameTimestampsSeconds.begin(),
            m_frameTimestampsSeconds.end(),
            targetTimestampSeconds);
        targetFrameIndex = static_cast<int>(std::distance(m_frameTimestampsSeconds.begin(), endIt)) - 1;
    }
    else
    {
        const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
        targetFrameIndex = static_cast<int>(std::floor(targetTimestampSeconds * safeFps));
    }

    targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, std::max(0, m_totalFrames - 1));
    if (targetFrameIndex == m_currentFrame.index)
    {
        return;
    }

    while (m_transport.isPlaying() && m_currentFrame.index < targetFrameIndex)
    {
        const auto previousFrameIndex = m_currentFrame.index;
        stepForward();
        if (m_currentFrame.index == previousFrameIndex)
        {
            break;
        }
    }
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
    if (!hasVideoLoaded() || !m_decoder->seekFrame(frameIndex))
    {
        return false;
    }

    const auto frame = m_decoder->readFrame();
    if (!frame.has_value() || !frame->isValid())
    {
        return false;
    }

    m_currentFrame = *frame;
    cv::cvtColor(m_currentFrame.bgr, m_currentGrayFrame, cv::COLOR_BGR2GRAY);
    refreshOverlays();
    emitCurrentFrame();
    return true;
}

std::vector<double> PlayerController::buildFrameTimestampCache(const QString& filePath) const
{
    OpenCvVideoDecoder decoder;
    if (!decoder.open(filePath.toStdString()))
    {
        return {};
    }

    std::vector<double> timestamps;
    const auto expectedFrameCount = decoder.frameCount();
    if (expectedFrameCount > 0)
    {
        timestamps.reserve(expectedFrameCount);
    }

    const auto fallbackFps = decoder.fps() > 0.0 ? decoder.fps() : 30.0;
    double previousTimestamp = 0.0;

    while (true)
    {
        const auto frame = decoder.readFrame();
        if (!frame.has_value() || !frame->isValid())
        {
            break;
        }

        const auto fallbackTimestamp = static_cast<double>(frame->index) / fallbackFps;
        auto timestamp = frame->timestampSeconds >= 0.0 ? frame->timestampSeconds : fallbackTimestamp;
        if (!timestamps.empty() && timestamp < previousTimestamp)
        {
            timestamp = previousTimestamp;
        }

        if (frame->index >= static_cast<int>(timestamps.size()))
        {
            timestamps.resize(frame->index + 1, fallbackTimestamp);
        }

        timestamps[frame->index] = timestamp;
        previousTimestamp = timestamp;
    }

    if (timestamps.empty() && expectedFrameCount > 0)
    {
        timestamps.resize(expectedFrameCount, 0.0);
    }

    for (int index = 0; index < static_cast<int>(timestamps.size()); ++index)
    {
        const auto fallbackTimestamp = static_cast<double>(index) / fallbackFps;
        if (index > 0 && timestamps[static_cast<std::size_t>(index)] < timestamps[static_cast<std::size_t>(index - 1)])
        {
            timestamps[static_cast<std::size_t>(index)] = timestamps[static_cast<std::size_t>(index - 1)];
        }
        else if (timestamps[static_cast<std::size_t>(index)] <= 0.0 && index > 0)
        {
            timestamps[static_cast<std::size_t>(index)] = std::max(
                timestamps[static_cast<std::size_t>(index - 1)],
                fallbackTimestamp);
        }
        else if (timestamps[static_cast<std::size_t>(index)] < 0.0)
        {
            timestamps[static_cast<std::size_t>(index)] = fallbackTimestamp;
        }
    }

    return timestamps;
}

double PlayerController::frameTimestampSeconds(const int frameIndex) const
{
    if (frameIndex >= 0 && frameIndex < static_cast<int>(m_frameTimestampsSeconds.size()))
    {
        return m_frameTimestampsSeconds[static_cast<std::size_t>(frameIndex)];
    }

    const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
    return frameIndex >= 0 ? (static_cast<double>(frameIndex) / safeFps) : 0.0;
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

    if (!m_frameTimestampsSeconds.empty() && startFrame < static_cast<int>(m_frameTimestampsSeconds.size()))
    {
        const auto searchBegin = m_frameTimestampsSeconds.begin() + startFrame;
        const auto searchEnd = m_frameTimestampsSeconds.begin() + std::min(
            static_cast<int>(m_frameTimestampsSeconds.size()),
            maxFrameIndex + 1);
        const auto endIt = std::lower_bound(searchBegin, searchEnd, endExclusiveSeconds);
        if (endIt == searchBegin)
        {
            endFrame = startFrame;
        }
        else
        {
            endFrame = static_cast<int>(std::distance(m_frameTimestampsSeconds.begin(), endIt) - 1);
        }
    }
    else
    {
        const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
        const auto coveredFrames = std::max(1, static_cast<int>(std::lround((*durationMs * safeFps) / 1000.0)));
        endFrame = startFrame + coveredFrames - 1;
    }

    return std::clamp(endFrame, startFrame, maxFrameIndex);
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
    emit frameReady(toImage(m_currentFrame.bgr), m_currentFrame.index, m_currentFrame.timestampSeconds);
}

void PlayerController::syncAttachedAudioForCurrentFrame()
{
    if (!hasVideoLoaded())
    {
        m_audioEngine->stopAll();
        return;
    }

    const auto currentTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);

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
    }
}

QImage PlayerController::toImage(const cv::Mat& bgrFrame) const
{
    cv::Mat rgbFrame;
    cv::cvtColor(bgrFrame, rgbFrame, cv::COLOR_BGR2RGB);

    return QImage(
               rgbFrame.data,
               rgbFrame.cols,
               rgbFrame.rows,
               static_cast<int>(rgbFrame.step),
               QImage::Format_RGB888)
        .copy();
}
