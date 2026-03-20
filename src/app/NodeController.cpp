#include "app/NodeController.h"

#include <algorithm>
#include <limits>

#include <QFileInfo>

#include "app/PlayerController.h"

NodeController::NodeController(PlayerController& controller)
    : m_controller(controller)
{
}

void NodeController::seedTrack(const QPointF& imagePoint)
{
    if (!m_controller.hasVideoLoaded())
    {
        return;
    }

    const auto previousUndoTrackerState = m_controller.m_undoTrackerState;
    const auto previousUndoSelectedTrackIds = m_controller.m_undoSelectedTrackIds;
    const auto previousRedoTrackerState = m_controller.m_redoTrackerState;
    const auto previousRedoSelectedTrackIds = m_controller.m_redoSelectedTrackIds;
    m_controller.saveUndoState();

    const auto result = m_controller.m_trackEditService->seedTrack(
        m_controller.m_videoPlaybackCoordinator->currentFrame().index,
        imagePoint,
        m_controller.m_motionTrackingEnabled,
        m_controller.m_videoPlaybackCoordinator->totalFrames(),
        m_controller.m_videoPlaybackCoordinator->fps());
    if (!result.created)
    {
        m_controller.m_undoTrackerState = previousUndoTrackerState;
        m_controller.m_undoSelectedTrackIds = previousUndoSelectedTrackIds;
        m_controller.m_redoTrackerState = previousRedoTrackerState;
        m_controller.m_redoSelectedTrackIds = previousRedoSelectedTrackIds;
        return;
    }

    m_controller.setSelectedTrackId(result.trackId);
    m_controller.refreshOverlays();

    emit m_controller.statusChanged(
        QStringLiteral("Added %1 at frame %2 (%3)")
            .arg(result.label)
            .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index)
            .arg(result.motionTracked ? QStringLiteral("tracked") : QStringLiteral("manual")));
    emit m_controller.trackAvailabilityChanged(true);
    emit m_controller.editStateChanged();
}

bool NodeController::createTrackWithAudioAtCurrentFrame(const QString& filePath)
{
    if (!m_controller.hasVideoLoaded())
    {
        emit m_controller.statusChanged(QStringLiteral("Open a video before adding audio nodes."));
        return false;
    }

    const auto imageCenter = QPointF{
        static_cast<double>(m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.width) * 0.5,
        static_cast<double>(m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.height) * 0.5
    };
    return createTrackWithAudioAtCurrentFrame(filePath, imageCenter);
}

bool NodeController::createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint)
{
    if (!m_controller.hasVideoLoaded())
    {
        emit m_controller.statusChanged(QStringLiteral("Open a video before adding audio nodes."));
        return false;
    }

    if (filePath.isEmpty())
    {
        return false;
    }

    const auto previousUndoTrackerState = m_controller.m_undoTrackerState;
    const auto previousUndoSelectedTrackIds = m_controller.m_undoSelectedTrackIds;
    const auto previousRedoTrackerState = m_controller.m_redoTrackerState;
    const auto previousRedoSelectedTrackIds = m_controller.m_redoSelectedTrackIds;
    m_controller.saveUndoState();

    const auto result = m_controller.m_trackEditService->createTrackWithAudio(
        m_controller.m_videoPlaybackCoordinator->currentFrame().index,
        imagePoint,
        m_controller.m_motionTrackingEnabled,
        m_controller.m_videoPlaybackCoordinator->totalFrames(),
        m_controller.m_videoPlaybackCoordinator->fps(),
        filePath,
          [this](const TrackPoint& track)
          {
              return m_controller.trimmedEndFrameForTrack(track);
          });
    if (!result.success)
    {
        m_controller.m_undoTrackerState = previousUndoTrackerState;
        m_controller.m_undoSelectedTrackIds = previousUndoSelectedTrackIds;
        m_controller.m_redoTrackerState = previousRedoTrackerState;
        m_controller.m_redoSelectedTrackIds = previousRedoSelectedTrackIds;
        emit m_controller.statusChanged(QStringLiteral("Failed to attach sound to the new node."));
        return false;
    }

    m_controller.setSelectedTrackId(result.trackId);
    m_controller.refreshOverlays();
    if (result.poolChanged)
    {
        emit m_controller.audioPoolChanged();
    }
    emit m_controller.trackAvailabilityChanged(true);
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        QStringLiteral("Added %1 at frame %2.")
            .arg(QFileInfo(filePath).fileName())
            .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
    return true;
}

bool NodeController::importSoundForSelectedTrack(const QString& filePath)
{
    if (!m_controller.hasVideoLoaded() || m_controller.m_selectionController->selectedTrackId().isNull())
    {
        emit m_controller.statusChanged(QStringLiteral("Select a node before importing sound."));
        return false;
    }

    const auto result = m_controller.m_trackEditService->attachAudioToTrack(
        m_controller.m_selectionController->selectedTrackId(),
        filePath,
        [this](const TrackPoint& track)
        {
            return m_controller.trimmedEndFrameForTrack(track);
        });
    if (!result.success)
    {
        emit m_controller.statusChanged(QStringLiteral("Failed to attach sound to the selected node."));
        return false;
    }

    m_controller.refreshOverlays();
    if (result.poolChanged)
    {
        emit m_controller.audioPoolChanged();
    }
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(QStringLiteral("Attached %1 to the selected node.").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool NodeController::selectTrackAndJumpToStart(const QUuid& trackId)
{
    const auto trackIt = std::find_if(
        m_controller.m_tracker.tracks().begin(),
        m_controller.m_tracker.tracks().end(),
        [&trackId](const TrackPoint& track)
        {
            return track.id == trackId;
        });
    if (trackIt == m_controller.m_tracker.tracks().end())
    {
        clearSelection();
        return false;
    }

    m_controller.setSelectedTrackId(trackId);
    m_controller.seekToFrame(std::max(0, trackIt->startFrame));
    return true;
}

void NodeController::selectAllVisibleTracks()
{
    if (!m_controller.hasVideoLoaded())
    {
        return;
    }

    std::vector<QUuid> visibleTrackIds;
    visibleTrackIds.reserve(m_controller.m_currentOverlays.size());

    for (const auto& overlay : m_controller.m_currentOverlays)
    {
        visibleTrackIds.push_back(overlay.id);
    }

    if (visibleTrackIds.empty())
    {
        clearSelection();
        emit m_controller.statusChanged(QStringLiteral("No nodes are visible on the current frame."));
        return;
    }

    static_cast<void>(m_controller.m_selectionController->setSelectedTrackIds(visibleTrackIds));
    m_controller.m_selectionFadeTimer.stop();
    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(true);
    emit m_controller.statusChanged(QStringLiteral("Selected %1 node(s) on the current frame.").arg(visibleTrackIds.size()));
}

void NodeController::selectTracks(const QList<QUuid>& trackIds)
{
    if (!m_controller.hasVideoLoaded())
    {
        return;
    }

    std::vector<QUuid> validTrackIds;
    validTrackIds.reserve(trackIds.size());

    for (const auto& trackId : trackIds)
    {
        if (trackId.isNull()
            || !m_controller.m_tracker.hasTrack(trackId)
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

    static_cast<void>(m_controller.m_selectionController->setSelectedTrackIds(validTrackIds));
    m_controller.m_selectionFadeTimer.stop();
    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(true);
    emit m_controller.statusChanged(QStringLiteral("Selected %1 node(s).").arg(validTrackIds.size()));
}

void NodeController::selectTrack(const QUuid& trackId)
{
    if (!m_controller.m_tracker.hasTrack(trackId))
    {
        clearSelection();
        return;
    }

    m_controller.setSelectedTrackId(trackId);
}

void NodeController::selectNextVisibleTrack()
{
    if (!m_controller.hasVideoLoaded() || m_controller.m_currentOverlays.empty())
    {
        clearSelection();
        emit m_controller.statusChanged(QStringLiteral("No nodes are visible on the current frame."));
        return;
    }

    std::vector<QUuid> visibleTrackIds;
    visibleTrackIds.reserve(m_controller.m_currentOverlays.size());
    for (const auto& overlay : m_controller.m_currentOverlays)
    {
        visibleTrackIds.push_back(overlay.id);
    }

    if (!m_controller.m_selectionController->selectNextVisibleTrack(visibleTrackIds))
    {
        return;
    }

    if (m_controller.m_selectionController->fadingDeselectedTrackOpacity() > 0.0F)
    {
        m_controller.m_selectionFadeTimer.start();
    }
    else
    {
        m_controller.m_selectionFadeTimer.stop();
    }

    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
}

void NodeController::selectNextTimelineTrack()
{
    if (!m_controller.hasVideoLoaded() || m_controller.m_tracker.tracks().empty())
    {
        clearSelection();
        emit m_controller.statusChanged(QStringLiteral("No nodes are available."));
        return;
    }

    std::vector<const TrackPoint*> orderedTracks;
    orderedTracks.reserve(m_controller.m_tracker.tracks().size());
    for (const auto& track : m_controller.m_tracker.tracks())
    {
        orderedTracks.push_back(&track);
    }

    std::stable_sort(
        orderedTracks.begin(),
        orderedTracks.end(),
        [](const TrackPoint* left, const TrackPoint* right)
        {
            if (!left || !right)
            {
                return left != nullptr;
            }

            if (left->startFrame != right->startFrame)
            {
                return left->startFrame < right->startFrame;
            }

            const auto leftEndFrame = left->endFrame.value_or(std::numeric_limits<int>::max());
            const auto rightEndFrame = right->endFrame.value_or(std::numeric_limits<int>::max());
            if (leftEndFrame != rightEndFrame)
            {
                return leftEndFrame < rightEndFrame;
            }

            if (left->label != right->label)
            {
                return left->label < right->label;
            }

            return left->id < right->id;
        });

    int nextIndex = 0;
    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    if (!selectedTrackId.isNull())
    {
        const auto currentIt = std::find_if(
            orderedTracks.cbegin(),
            orderedTracks.cend(),
            [&selectedTrackId](const TrackPoint* track)
            {
                return track && track->id == selectedTrackId;
            });
        if (currentIt != orderedTracks.cend())
        {
            nextIndex = (static_cast<int>(std::distance(orderedTracks.cbegin(), currentIt)) + 1)
                % static_cast<int>(orderedTracks.size());
        }
    }
    else
    {
        const auto currentFrame = m_controller.currentFrameIndex();
        const auto firstAtOrAfterCurrentFrame = std::find_if(
            orderedTracks.cbegin(),
            orderedTracks.cend(),
            [currentFrame](const TrackPoint* track)
            {
                return track && track->startFrame >= currentFrame;
            });
        if (firstAtOrAfterCurrentFrame != orderedTracks.cend())
        {
            nextIndex = static_cast<int>(std::distance(orderedTracks.cbegin(), firstAtOrAfterCurrentFrame));
        }
    }

    const auto* nextTrack = orderedTracks[static_cast<std::size_t>(nextIndex)];
    if (!nextTrack)
    {
        return;
    }

    if (!m_controller.m_selectionController->setSelectedTrackId(nextTrack->id))
    {
        return;
    }

    if (m_controller.m_selectionController->fadingDeselectedTrackOpacity() > 0.0F)
    {
        m_controller.m_selectionFadeTimer.start();
    }
    else
    {
        m_controller.m_selectionFadeTimer.stop();
    }

    m_controller.seekToFrame(std::max(0, nextTrack->startFrame));
    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
}

void NodeController::clearSelection()
{
    m_controller.setSelectedTrackId({});
}

bool NodeController::copySelectedTracks()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return false;
    }

    if (!m_controller.m_trackEditService->copyTracks(m_controller.m_selectionController->selectedTrackIds()))
    {
        return false;
    }

    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        m_controller.m_selectionController->selectedTrackIds().size() == 1
            ? QStringLiteral("Copied selected node.")
            : QStringLiteral("Copied %1 selected nodes.").arg(m_controller.m_selectionController->selectedTrackIds().size()));
    return true;
}

bool NodeController::pasteCopiedTracksAtCurrentFrame()
{
    if (!m_controller.hasVideoLoaded()
        || !m_controller.m_trackEditService->hasCopiedTracks()
        || m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.width <= 0
        || m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.height <= 0)
    {
        return false;
    }

    m_controller.saveUndoState();
    const auto result = m_controller.m_trackEditService->pasteCopiedTracks(
        m_controller.m_videoPlaybackCoordinator->currentFrame().index,
        QSize{
            m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.width,
            m_controller.m_videoPlaybackCoordinator->currentFrame().frameSize.height},
        m_controller.m_videoPlaybackCoordinator->totalFrames());
    if (result.pastedTrackIds.empty())
    {
        m_controller.m_undoTrackerState.reset();
        m_controller.m_undoSelectedTrackIds.clear();
        emit m_controller.editStateChanged();
        return false;
    }

    static_cast<void>(m_controller.m_selectionController->setSelectedTrackIds(result.pastedTrackIds));
    m_controller.m_selectionFadeTimer.stop();
    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(true);
    emit m_controller.trackAvailabilityChanged(true);
    emit m_controller.audioPoolChanged();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        result.pastedTrackIds.size() == 1
            ? QStringLiteral("Pasted node at the center of the frame.")
            : QStringLiteral("Pasted %1 nodes at the center of the frame.").arg(result.pastedTrackIds.size()));
    return true;
}

bool NodeController::cutSelectedTracks()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return false;
    }

    if (!m_controller.m_trackEditService->copyTracks(m_controller.m_selectionController->selectedTrackIds()))
    {
        return false;
    }

    m_controller.saveUndoState();
    const auto removedCount = m_controller.m_trackEditService->removeTracks(m_controller.m_selectionController->selectedTrackIds());
    if (removedCount <= 0)
    {
        m_controller.m_undoTrackerState.reset();
        m_controller.m_undoSelectedTrackIds.clear();
        emit m_controller.editStateChanged();
        return false;
    }

    m_controller.setSelectedTrackId({}, false);
    m_controller.refreshOverlays();
    emit m_controller.trackAvailabilityChanged(m_controller.hasTracks());
    emit m_controller.audioPoolChanged();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        removedCount == 1
            ? QStringLiteral("Cut selected node.")
            : QStringLiteral("Cut %1 selected nodes.").arg(removedCount));
    return true;
}

bool NodeController::undoLastTrackEdit()
{
    if (!m_controller.m_undoTrackerState.has_value())
    {
        return false;
    }

    m_controller.m_redoTrackerState = m_controller.m_tracker.snapshotState();
    m_controller.m_redoSelectedTrackIds = m_controller.m_selectionController->selectedTrackIds();
    m_controller.restoreTrackEditState(*m_controller.m_undoTrackerState, m_controller.m_undoSelectedTrackIds);
    m_controller.m_undoTrackerState.reset();
    m_controller.m_undoSelectedTrackIds.clear();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(QStringLiteral("Undid last node edit."));
    return true;
}

bool NodeController::redoLastTrackEdit()
{
    if (!m_controller.m_redoTrackerState.has_value())
    {
        return false;
    }

    m_controller.m_undoTrackerState = m_controller.m_tracker.snapshotState();
    m_controller.m_undoSelectedTrackIds = m_controller.m_selectionController->selectedTrackIds();
    m_controller.restoreTrackEditState(*m_controller.m_redoTrackerState, m_controller.m_redoSelectedTrackIds);
    m_controller.m_redoTrackerState.reset();
    m_controller.m_redoSelectedTrackIds.clear();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(QStringLiteral("Redid last node edit."));
    return true;
}

bool NodeController::renameTrack(const QUuid& trackId, const QString& label)
{
    if (!m_controller.hasVideoLoaded() || trackId.isNull())
    {
        return false;
    }

    const auto trimmedLabel = label.trimmed();
    if (trimmedLabel.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Node name cannot be empty."));
        return false;
    }

    if (!m_controller.m_tracker.setTrackLabel(trackId, trimmedLabel))
    {
        emit m_controller.statusChanged(QStringLiteral("Failed to rename the selected node."));
        return false;
    }

    m_controller.refreshOverlays();
    emit m_controller.statusChanged(QStringLiteral("Renamed node to %1.").arg(trimmedLabel));
    return true;
}

void NodeController::setTrackStartFrame(const QUuid& trackId, const int frameIndex)
{
    if (!m_controller.hasVideoLoaded() || trackId.isNull())
    {
        return;
    }

    if (!m_controller.m_trackEditService->setTrackStartFrame(trackId, frameIndex, m_controller.m_videoPlaybackCoordinator->totalFrames()))
    {
        return;
    }

    m_controller.refreshOverlays();
}

void NodeController::setTrackEndFrame(const QUuid& trackId, const int frameIndex)
{
    if (!m_controller.hasVideoLoaded() || trackId.isNull())
    {
        return;
    }

    if (!m_controller.m_trackEditService->setTrackEndFrame(trackId, frameIndex, m_controller.m_videoPlaybackCoordinator->totalFrames()))
    {
        return;
    }

    m_controller.refreshOverlays();
}

void NodeController::moveTrackFrameSpan(const QUuid& trackId, const int deltaFrames)
{
    if (!m_controller.hasVideoLoaded() || trackId.isNull() || deltaFrames == 0)
    {
        return;
    }

    const auto maxFrameIndex = m_controller.m_videoPlaybackCoordinator->totalFrames() > 0
        ? (m_controller.m_videoPlaybackCoordinator->totalFrames() - 1)
        : 0;
    if (!m_controller.m_tracker.moveTrackFrameSpan(trackId, deltaFrames, maxFrameIndex))
    {
        return;
    }

    m_controller.refreshOverlays();
}

void NodeController::moveSelectedTrack(const QPointF& imagePoint)
{
    if (!m_controller.hasVideoLoaded() || m_controller.m_selectionController->selectedTrackId().isNull())
    {
        return;
    }

    if (m_controller.m_tracker.updateTrackSample(
            m_controller.m_selectionController->selectedTrackId(),
            m_controller.m_videoPlaybackCoordinator->currentFrame().index,
            imagePoint))
    {
        m_controller.refreshOverlays();
    }
}

void NodeController::nudgeSelectedTracks(const QPointF& delta)
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection() || delta.isNull())
    {
        return;
    }

    auto movedAny = false;
    for (const auto& trackId : m_controller.m_selectionController->selectedTrackIds())
    {
        const auto trackIt = std::find_if(
            m_controller.m_tracker.tracks().begin(),
            m_controller.m_tracker.tracks().end(),
            [&trackId](const TrackPoint& track)
            {
                return track.id == trackId;
            });
        if (trackIt == m_controller.m_tracker.tracks().end())
        {
            continue;
        }

        auto basePoint = trackIt->interpolatedSampleAt(m_controller.m_videoPlaybackCoordinator->currentFrame().index);
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

        if (m_controller.m_tracker.updateTrackSample(
                trackId,
                m_controller.m_videoPlaybackCoordinator->currentFrame().index,
                *basePoint + delta))
        {
            movedAny = true;
        }
    }

    if (movedAny)
    {
        m_controller.refreshOverlays();
    }
}

void NodeController::deleteSelectedTrack()
{
    if (!m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    m_controller.saveUndoState();
    const auto removedCount = m_controller.m_trackEditService->removeTracks(m_controller.m_selectionController->selectedTrackIds());
    if (removedCount <= 0)
    {
        m_controller.m_undoTrackerState.reset();
        m_controller.m_undoSelectedTrackIds.clear();
        emit m_controller.editStateChanged();
        m_controller.setSelectedTrackId({}, false);
        emit m_controller.statusChanged(QStringLiteral("The selected node selection no longer exists."));
        return;
    }

    m_controller.setSelectedTrackId({}, false);
    m_controller.refreshOverlays();
    emit m_controller.trackAvailabilityChanged(m_controller.hasTracks());
    emit m_controller.audioPoolChanged();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        removedCount == 1
            ? QStringLiteral("Deleted selected node.")
            : QStringLiteral("Deleted %1 selected nodes.").arg(removedCount));
}

int NodeController::emptyTrackCount() const
{
    return static_cast<int>(std::count_if(
        m_controller.m_tracker.tracks().cbegin(),
        m_controller.m_tracker.tracks().cend(),
        [](const TrackPoint& track)
        {
            return !track.attachedAudio.has_value();
        }));
}

void NodeController::deleteAllEmptyTracks()
{
    std::vector<QUuid> emptyTrackIds;
    emptyTrackIds.reserve(m_controller.m_tracker.tracks().size());

    for (const auto& track : m_controller.m_tracker.tracks())
    {
        if (!track.attachedAudio.has_value())
        {
            emptyTrackIds.push_back(track.id);
        }
    }

    if (emptyTrackIds.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("There are no empty grey nodes to delete."));
        return;
    }

    m_controller.saveUndoState();
    const auto removedCount = m_controller.m_trackEditService->removeTracks(emptyTrackIds);
    if (removedCount <= 0)
    {
        m_controller.m_undoTrackerState.reset();
        m_controller.m_undoSelectedTrackIds.clear();
        emit m_controller.editStateChanged();
        emit m_controller.statusChanged(QStringLiteral("Failed to delete the empty grey nodes."));
        return;
    }

    std::vector<QUuid> remainingSelection;
    remainingSelection.reserve(m_controller.m_selectionController->selectedTrackIds().size());
    for (const auto& trackId : m_controller.m_selectionController->selectedTrackIds())
    {
        if (m_controller.m_tracker.hasTrack(trackId))
        {
            remainingSelection.push_back(trackId);
        }
    }

    static_cast<void>(m_controller.m_selectionController->setSelectedTrackIds(remainingSelection));
    m_controller.m_selectionFadeTimer.stop();
    m_controller.refreshOverlays();
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.trackAvailabilityChanged(m_controller.hasTracks());
    emit m_controller.audioPoolChanged();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(
        removedCount == 1
            ? QStringLiteral("Deleted 1 empty grey node.")
            : QStringLiteral("Deleted %1 empty grey nodes.").arg(removedCount));
}

void NodeController::clearAllTracks()
{
    if (!m_controller.hasTracks())
    {
        return;
    }

    m_controller.saveUndoState();
    m_controller.m_trackEditService->clearAllTracks();
    m_controller.setSelectedTrackId({}, false);
    m_controller.refreshOverlays();
    emit m_controller.trackAvailabilityChanged(false);
    emit m_controller.audioPoolChanged();
    emit m_controller.editStateChanged();
    emit m_controller.statusChanged(QStringLiteral("Cleared all nodes."));
}

void NodeController::setSelectedTrackStartToCurrentFrame()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    const auto updatedCount = m_controller.m_trackEditService->setTrackStartFrames(
        m_controller.m_selectionController->selectedTrackIds(),
        m_controller.m_videoPlaybackCoordinator->currentFrame().index);

    if (updatedCount > 0)
    {
        m_controller.refreshOverlays();
        emit m_controller.statusChanged(
            updatedCount == 1
                ? QStringLiteral("Set selected node start to frame %1.").arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index)
                : QStringLiteral("Set %1 selected node starts to frame %2.")
                      .arg(updatedCount)
                      .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
        return;
    }

    if (m_controller.m_selectionController->selectedTrackIds().size() > 1)
    {
        emit m_controller.statusChanged(
            QStringLiteral("No selected node starts were earlier than frame %1.")
                .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
    }
}

void NodeController::setSelectedTrackEndToCurrentFrame()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    const auto updatedCount = m_controller.m_trackEditService->setTrackEndFrames(
        m_controller.m_selectionController->selectedTrackIds(),
        m_controller.m_videoPlaybackCoordinator->currentFrame().index);

    if (updatedCount > 0)
    {
        m_controller.refreshOverlays();
        emit m_controller.statusChanged(
            updatedCount == 1
                ? QStringLiteral("Set selected node end to frame %1.").arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index)
                : QStringLiteral("Set %1 selected node ends to frame %2.")
                      .arg(updatedCount)
                      .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
        return;
    }

    if (m_controller.m_selectionController->selectedTrackIds().size() > 1)
    {
        emit m_controller.statusChanged(
            QStringLiteral("No selected node ends were later than frame %1.")
                .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
    }
}

void NodeController::toggleSelectedTrackLabels()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    const auto allLabelsVisible = std::all_of(
        m_controller.m_selectionController->selectedTrackIds().begin(),
        m_controller.m_selectionController->selectedTrackIds().end(),
        [this](const QUuid& trackId)
        {
            return m_controller.m_tracker.isTrackLabelVisible(trackId);
        });
    const auto newVisibleState = !allLabelsVisible;
    const auto updatedCount = m_controller.m_tracker.setTrackLabelsVisible(
        m_controller.m_selectionController->selectedTrackIds(),
        newVisibleState);

    if (updatedCount <= 0)
    {
        return;
    }

    m_controller.refreshOverlays();
    emit m_controller.statusChanged(
        newVisibleState
            ? QStringLiteral("Showing labels for %1 selected node(s).").arg(m_controller.m_selectionController->selectedTrackIds().size())
            : QStringLiteral("Hiding labels for %1 selected node(s).").arg(m_controller.m_selectionController->selectedTrackIds().size()));
}

void NodeController::setAllTracksStartToCurrentFrame()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.hasTracks())
    {
        return;
    }

    const auto updatedCount = m_controller.m_trackEditService->setAllTrackStartFrames(
        m_controller.m_videoPlaybackCoordinator->currentFrame().index);
    if (updatedCount <= 0)
    {
        emit m_controller.statusChanged(
            QStringLiteral("No node starts were earlier than frame %1.")
                .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
        return;
    }

    m_controller.refreshOverlays();
    emit m_controller.statusChanged(
        QStringLiteral("Set all node starts to frame %1.").arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
}

void NodeController::setAllTracksEndToCurrentFrame()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.hasTracks())
    {
        return;
    }

    const auto updatedCount = m_controller.m_trackEditService->setAllTrackEndFrames(
        m_controller.m_videoPlaybackCoordinator->currentFrame().index);
    if (updatedCount <= 0)
    {
        emit m_controller.statusChanged(
            QStringLiteral("No node ends were later than frame %1.")
                .arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
        return;
    }

    m_controller.refreshOverlays();
    emit m_controller.statusChanged(
        QStringLiteral("Set all node ends to frame %1.").arg(m_controller.m_videoPlaybackCoordinator->currentFrame().index));
}

void NodeController::trimSelectedTracksToAttachedSound()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    const auto result = m_controller.m_trackEditService->trimTracksToAttachedSound(
        m_controller.m_selectionController->selectedTrackIds(),
        [this](const TrackPoint& track)
        {
            return m_controller.trimmedEndFrameForTrack(track);
        });

    if (result.trimmedCount > 0)
    {
        m_controller.refreshOverlays();
    }

    if (result.trimmedCount > 0 && result.missingAudioCount == 0 && result.failedDurationCount == 0)
    {
        emit m_controller.statusChanged(
            result.trimmedCount == 1
                ? QStringLiteral("Trimmed selected node to its attached sound length.")
                : QStringLiteral("Trimmed %1 selected nodes to their attached sound lengths.").arg(result.trimmedCount));
        return;
    }

    if (result.trimmedCount <= 0)
    {
        if (result.missingAudioCount > 0 && result.failedDurationCount == 0)
        {
            emit m_controller.statusChanged(
                m_controller.m_selectionController->selectedTrackIds().size() == 1
                    ? QStringLiteral("The selected node has no attached sound.")
                    : QStringLiteral("None of the selected nodes had attached sound."));
            return;
        }

        if (result.failedDurationCount > 0 && result.missingAudioCount == 0)
        {
            emit m_controller.statusChanged(
                QStringLiteral("Could not read the attached sound length for the selected node(s)."));
            return;
        }

        emit m_controller.statusChanged(
            QStringLiteral("No selected nodes could be trimmed to attached sound length."));
        return;
    }

    emit m_controller.statusChanged(
        QStringLiteral("Trimmed %1 node(s). %2 had no sound and %3 could not be measured.")
            .arg(result.trimmedCount)
            .arg(result.missingAudioCount)
            .arg(result.failedDurationCount));
}

void NodeController::toggleSelectedTrackAutoPan()
{
    if (!m_controller.hasVideoLoaded() || !m_controller.m_selectionController->hasSelection())
    {
        return;
    }

    const auto enableAutoPan = !m_controller.selectedTracksAutoPanEnabled();
    int updatedCount = 0;

    for (const auto& trackId : m_controller.m_selectionController->selectedTrackIds())
    {
        if (m_controller.m_tracker.setTrackAutoPanEnabled(trackId, enableAutoPan))
        {
            ++updatedCount;
        }
    }

    if (updatedCount <= 0)
    {
        return;
    }

    m_controller.refreshOverlays();

    if (m_controller.m_transport.isPlaying())
    {
        m_controller.syncAttachedAudioForCurrentFrame();
    }

    emit m_controller.statusChanged(
        enableAutoPan
            ? QStringLiteral("Auto Pan enabled for %1 selected node(s).").arg(updatedCount)
            : QStringLiteral("Auto Pan disabled for %1 selected node(s).").arg(updatedCount));
}

void PlayerController::seedTrack(const QPointF& imagePoint)
{
    m_nodeController->seedTrack(imagePoint);
}

bool PlayerController::createTrackWithAudioAtCurrentFrame(const QString& filePath)
{
    return m_nodeController->createTrackWithAudioAtCurrentFrame(filePath);
}

bool PlayerController::createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint)
{
    return m_nodeController->createTrackWithAudioAtCurrentFrame(filePath, imagePoint);
}

bool PlayerController::importSoundForSelectedTrack(const QString& filePath)
{
    return m_nodeController->importSoundForSelectedTrack(filePath);
}

bool PlayerController::selectTrackAndJumpToStart(const QUuid& trackId)
{
    return m_nodeController->selectTrackAndJumpToStart(trackId);
}

void PlayerController::selectAllVisibleTracks()
{
    m_nodeController->selectAllVisibleTracks();
}

void PlayerController::selectTracks(const QList<QUuid>& trackIds)
{
    m_nodeController->selectTracks(trackIds);
}

void PlayerController::selectTrack(const QUuid& trackId)
{
    m_nodeController->selectTrack(trackId);
}

void PlayerController::selectNextVisibleTrack()
{
    m_nodeController->selectNextVisibleTrack();
}

void PlayerController::selectNextTimelineTrack()
{
    m_nodeController->selectNextTimelineTrack();
}

void PlayerController::clearSelection()
{
    m_nodeController->clearSelection();
}

bool PlayerController::copySelectedTracks()
{
    return m_nodeController->copySelectedTracks();
}

bool PlayerController::pasteCopiedTracksAtCurrentFrame()
{
    return m_nodeController->pasteCopiedTracksAtCurrentFrame();
}

bool PlayerController::cutSelectedTracks()
{
    return m_nodeController->cutSelectedTracks();
}

bool PlayerController::undoLastTrackEdit()
{
    return m_nodeController->undoLastTrackEdit();
}

bool PlayerController::redoLastTrackEdit()
{
    return m_nodeController->redoLastTrackEdit();
}

bool PlayerController::renameTrack(const QUuid& trackId, const QString& label)
{
    return m_nodeController->renameTrack(trackId, label);
}

void PlayerController::setTrackStartFrame(const QUuid& trackId, const int frameIndex)
{
    m_nodeController->setTrackStartFrame(trackId, frameIndex);
}

void PlayerController::setTrackEndFrame(const QUuid& trackId, const int frameIndex)
{
    m_nodeController->setTrackEndFrame(trackId, frameIndex);
}

void PlayerController::moveTrackFrameSpan(const QUuid& trackId, const int deltaFrames)
{
    m_nodeController->moveTrackFrameSpan(trackId, deltaFrames);
}

void PlayerController::moveSelectedTrack(const QPointF& imagePoint)
{
    m_nodeController->moveSelectedTrack(imagePoint);
}

void PlayerController::nudgeSelectedTracks(const QPointF& delta)
{
    m_nodeController->nudgeSelectedTracks(delta);
}

void PlayerController::deleteSelectedTrack()
{
    m_nodeController->deleteSelectedTrack();
}

int PlayerController::emptyTrackCount() const
{
    return m_nodeController->emptyTrackCount();
}

void PlayerController::deleteAllEmptyTracks()
{
    m_nodeController->deleteAllEmptyTracks();
}

void PlayerController::clearAllTracks()
{
    m_nodeController->clearAllTracks();
}

void PlayerController::setSelectedTrackStartToCurrentFrame()
{
    m_nodeController->setSelectedTrackStartToCurrentFrame();
}

void PlayerController::setSelectedTrackEndToCurrentFrame()
{
    m_nodeController->setSelectedTrackEndToCurrentFrame();
}

void PlayerController::toggleSelectedTrackLabels()
{
    m_nodeController->toggleSelectedTrackLabels();
}

void PlayerController::setAllTracksStartToCurrentFrame()
{
    m_nodeController->setAllTracksStartToCurrentFrame();
}

void PlayerController::setAllTracksEndToCurrentFrame()
{
    m_nodeController->setAllTracksEndToCurrentFrame();
}

void PlayerController::trimSelectedTracksToAttachedSound()
{
    m_nodeController->trimSelectedTracksToAttachedSound();
}

void PlayerController::toggleSelectedTrackAutoPan()
{
    m_nodeController->toggleSelectedTrackAutoPan();
}
