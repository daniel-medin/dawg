#include "app/NodeController.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "app/NodeEditorDocumentUtils.h"
#include "app/PlayerController.h"

namespace
{

constexpr int kMinimumClipDurationMs = 1;

int nodeTimelineDurationMs(const dawg::node::Document& document)
{
    if (document.node.timelineFps <= 0.0)
    {
        return 1;
    }

    return std::max(
        1,
        static_cast<int>(std::lround(
            (static_cast<double>(document.node.timelineFrameCount) * 1000.0)
            / document.node.timelineFps)));
}

Qt::CaseSensitivity nodePathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString uniqueTargetFilePath(const QString& directoryPath, const QString& fileName)
{
    const QFileInfo desiredInfo(QDir(directoryPath).filePath(fileName));
    const auto completeBaseName = desiredInfo.completeBaseName();
    const auto suffix = desiredInfo.completeSuffix();
    QString candidatePath = desiredInfo.filePath();
    int duplicateIndex = 2;
    while (QFileInfo::exists(candidatePath))
    {
        const auto candidateName = suffix.isEmpty()
            ? QStringLiteral("%1 %2").arg(completeBaseName).arg(duplicateIndex)
            : QStringLiteral("%1 %2.%3").arg(completeBaseName).arg(duplicateIndex).arg(suffix);
        candidatePath = QDir(directoryPath).filePath(candidateName);
        ++duplicateIndex;
    }

    return QDir::cleanPath(candidatePath);
}

bool materializeNodeClipAudio(
    dawg::node::AudioClipData& clip,
    const QString& audioDirectoryPath,
    QString* errorMessage)
{
    if (!clip.attachedAudio.has_value())
    {
        return true;
    }

    if (!QDir().mkpath(audioDirectoryPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create the project audio folder.");
        }
        return false;
    }

    if (!clip.embeddedAudioData.isEmpty())
    {
        const auto embeddedAudioFileName = clip.embeddedAudioFileName.isEmpty()
            ? QStringLiteral("node-audio.wav")
            : clip.embeddedAudioFileName;
        const auto targetAudioPath = uniqueTargetFilePath(audioDirectoryPath, embeddedAudioFileName);
        QFile audioFile(targetAudioPath);
        if (!audioFile.open(QIODevice::WriteOnly)
            || audioFile.write(clip.embeddedAudioData) != clip.embeddedAudioData.size())
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to materialize embedded node audio.");
            }
            return false;
        }
        audioFile.close();
        clip.attachedAudio->assetPath = QDir::cleanPath(targetAudioPath);
        return true;
    }

    const auto assetPath = clip.attachedAudio->assetPath;
    if (assetPath.isEmpty() || !QFileInfo::exists(assetPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("The node contains an audio clip that could not be found.");
        }
        return false;
    }

    const auto targetAudioPath = uniqueTargetFilePath(audioDirectoryPath, assetPath);
    if (!QFile::copy(assetPath, targetAudioPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to gather node audio into the project.");
        }
        return false;
    }

    clip.attachedAudio->assetPath = QDir::cleanPath(targetAudioPath);
    return true;
}

struct NodeClipRange
{
    int laneStartMs = 0;
    int laneEndMs = 0;
    int sourceStartMs = 0;
    int sourceEndMs = 0;
};

struct ExtractedNodeSelectionSegment
{
    dawg::node::AudioClipData clip;
    int laneIndexOffset = 0;
};

std::optional<NodeClipRange> resolvedNodeClipRange(
    const dawg::node::AudioClipData& clip,
    const dawg::nodeeditor::AudioDurationFn& durationForPath)
{
    if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
    {
        return std::nullopt;
    }

    const auto sourceDurationMs = durationForPath
        ? durationForPath(clip.attachedAudio->assetPath)
        : std::optional<int>{};
    if (!sourceDurationMs.has_value() || *sourceDurationMs <= 0)
    {
        return std::nullopt;
    }

    const auto sourceStartMs = std::clamp(
        clip.attachedAudio->clipStartMs,
        0,
        std::max(0, *sourceDurationMs - kMinimumClipDurationMs));
    const auto sourceEndMs = std::clamp(
        clip.attachedAudio->clipEndMs.value_or(*sourceDurationMs),
        sourceStartMs + kMinimumClipDurationMs,
        *sourceDurationMs);
    const auto laneStartMs = std::max(0, clip.laneOffsetMs);
    return NodeClipRange{
        .laneStartMs = laneStartMs,
        .laneEndMs = laneStartMs + std::max(kMinimumClipDurationMs, sourceEndMs - sourceStartMs),
        .sourceStartMs = sourceStartMs,
        .sourceEndMs = sourceEndMs};
}

void sortLaneAudioClips(dawg::node::LaneData& lane)
{
    std::stable_sort(
        lane.audioClips.begin(),
        lane.audioClips.end(),
        [](const dawg::node::AudioClipData& lhs, const dawg::node::AudioClipData& rhs)
        {
            if (lhs.laneOffsetMs != rhs.laneOffsetMs)
            {
                return lhs.laneOffsetMs < rhs.laneOffsetMs;
            }
            return lhs.id < rhs.id;
        });
}

void resolveLaneClipOverlaps(
    dawg::node::LaneData& lane,
    const QString& coveringClipId,
    const dawg::nodeeditor::AudioDurationFn& durationForPath)
{
    const auto coveringClipIt = std::find_if(
        lane.audioClips.cbegin(),
        lane.audioClips.cend(),
        [&coveringClipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == coveringClipId;
        });
    if (coveringClipIt == lane.audioClips.cend())
    {
        return;
    }

    const auto coveringRange = resolvedNodeClipRange(*coveringClipIt, durationForPath);
    if (!coveringRange.has_value())
    {
        sortLaneAudioClips(lane);
        return;
    }

    std::vector<dawg::node::AudioClipData> updatedClips;
    updatedClips.reserve(lane.audioClips.size() + 2);
    for (const auto& clip : lane.audioClips)
    {
        if (clip.id == coveringClipId)
        {
            updatedClips.push_back(clip);
            continue;
        }

        const auto clipRange = resolvedNodeClipRange(clip, durationForPath);
        if (!clipRange.has_value()
            || coveringRange->laneStartMs >= clipRange->laneEndMs
            || coveringRange->laneEndMs <= clipRange->laneStartMs)
        {
            updatedClips.push_back(clip);
            continue;
        }

        const auto overlapStartMs = std::max(coveringRange->laneStartMs, clipRange->laneStartMs);
        const auto overlapEndMs = std::min(coveringRange->laneEndMs, clipRange->laneEndMs);
        const auto keepLeft = overlapStartMs > clipRange->laneStartMs;
        const auto keepRight = overlapEndMs < clipRange->laneEndMs;

        if (keepLeft)
        {
            auto leftClip = clip;
            if (leftClip.attachedAudio.has_value())
            {
                leftClip.attachedAudio->clipEndMs = clipRange->sourceStartMs + (overlapStartMs - clipRange->laneStartMs);
                leftClip.attachedAudio->loopEnabled = false;
            }
            updatedClips.push_back(std::move(leftClip));
        }

        if (keepRight)
        {
            auto rightClip = clip;
            if (keepLeft)
            {
                rightClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            rightClip.laneOffsetMs = overlapEndMs;
            if (rightClip.attachedAudio.has_value())
            {
                rightClip.attachedAudio->clipStartMs = clipRange->sourceStartMs + (overlapEndMs - clipRange->laneStartMs);
                rightClip.attachedAudio->clipEndMs = clipRange->sourceEndMs;
                rightClip.attachedAudio->loopEnabled = false;
            }
            updatedClips.push_back(std::move(rightClip));
        }
    }

    lane.audioClips = std::move(updatedClips);
    sortLaneAudioClips(lane);
}

bool extractNodeSelectionFromLane(
    dawg::node::LaneData& lane,
    const int selectionStartMs,
    const int selectionEndMs,
    const int laneIndexOffset,
    const dawg::nodeeditor::AudioDurationFn& durationForPath,
    std::vector<ExtractedNodeSelectionSegment>* extractedSegments)
{
    if (selectionEndMs <= selectionStartMs)
    {
        return false;
    }

    bool laneChanged = false;
    std::vector<dawg::node::AudioClipData> updatedClips;
    updatedClips.reserve(lane.audioClips.size() + 2);
    for (const auto& clip : lane.audioClips)
    {
        const auto clipRange = resolvedNodeClipRange(clip, durationForPath);
        if (!clipRange.has_value()
            || selectionStartMs >= clipRange->laneEndMs
            || selectionEndMs <= clipRange->laneStartMs)
        {
            updatedClips.push_back(clip);
            continue;
        }

        laneChanged = true;
        const auto overlapStartMs = std::max(selectionStartMs, clipRange->laneStartMs);
        const auto overlapEndMs = std::min(selectionEndMs, clipRange->laneEndMs);
        if (extractedSegments && overlapEndMs > overlapStartMs)
        {
            auto extractedClip = clip;
            if (extractedClip.attachedAudio.has_value())
            {
                extractedClip.attachedAudio->clipStartMs =
                    clipRange->sourceStartMs + (overlapStartMs - clipRange->laneStartMs);
                extractedClip.attachedAudio->clipEndMs =
                    clipRange->sourceStartMs + (overlapEndMs - clipRange->laneStartMs);
                extractedClip.attachedAudio->loopEnabled = false;
            }
            extractedClip.laneOffsetMs = std::max(0, overlapStartMs - selectionStartMs);
            extractedSegments->push_back(
                ExtractedNodeSelectionSegment{std::move(extractedClip), laneIndexOffset});
        }

        const auto keepLeft = overlapStartMs > clipRange->laneStartMs;
        const auto keepRight = overlapEndMs < clipRange->laneEndMs;
        if (keepLeft)
        {
            auto leftClip = clip;
            if (leftClip.attachedAudio.has_value())
            {
                leftClip.attachedAudio->clipEndMs =
                    clipRange->sourceStartMs + (overlapStartMs - clipRange->laneStartMs);
                leftClip.attachedAudio->loopEnabled = false;
            }
            updatedClips.push_back(std::move(leftClip));
        }

        if (keepRight)
        {
            auto rightClip = clip;
            if (keepLeft)
            {
                rightClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            rightClip.laneOffsetMs = overlapEndMs;
            if (rightClip.attachedAudio.has_value())
            {
                rightClip.attachedAudio->clipStartMs =
                    clipRange->sourceStartMs + (overlapEndMs - clipRange->laneStartMs);
                rightClip.attachedAudio->clipEndMs = clipRange->sourceEndMs;
                rightClip.attachedAudio->loopEnabled = false;
            }
            updatedClips.push_back(std::move(rightClip));
        }
    }

    if (laneChanged)
    {
        lane.audioClips = std::move(updatedClips);
        sortLaneAudioClips(lane);
    }
    return laneChanged;
}

QString defaultNodeLaneLabel(const int laneNumber)
{
    return QStringLiteral("Track %1").arg(std::max(1, laneNumber));
}

}

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
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.editStateChanged();
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
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.editStateChanged();
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
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.editStateChanged();
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
        emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
        emit m_controller.editStateChanged();
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
        emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
        emit m_controller.editStateChanged();
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
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.editStateChanged();
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
    emit m_controller.selectionChanged(m_controller.m_selectionController->hasSelection());
    emit m_controller.editStateChanged();
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

bool NodeController::canPasteNodeClip() const
{
    return m_nodeEditorClipClipboard.has_value()
        || (m_nodeEditorSelectionClipboard.has_value()
            && !m_nodeEditorSelectionClipboard->segments.empty());
}

bool NodeController::copySelectedNodeClip(const QString& laneId, const QString& clipId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (laneId.isEmpty() || clipId.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Select an audio clip before copying."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before copying clips.")
                : errorMessage);
        return false;
    }

    const auto laneIt = std::find_if(
        nodeDocument.node.lanes.cbegin(),
        nodeDocument.node.lanes.cend(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.cend())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    const auto clipIt = std::find_if(
        laneIt->audioClips.cbegin(),
        laneIt->audioClips.cend(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == laneIt->audioClips.cend())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }

    m_nodeEditorClipClipboard = *clipIt;
    m_nodeEditorSelectionClipboard.reset();
    emit m_controller.statusChanged(QStringLiteral("Copied audio clip."));
    return true;
}

bool NodeController::copyNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }

    const auto normalizedStartMs = std::min(startMs, endMs);
    const auto normalizedEndMs = std::max(startMs, endMs);
    if (normalizedEndMs <= normalizedStartMs)
    {
        emit m_controller.statusChanged(QStringLiteral("Drag a time selection before copying it."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before copying a selection.")
                : errorMessage);
        return false;
    }
    if (nodeDocument.node.lanes.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("This node has no audio to copy."));
        return false;
    }

    const auto firstLaneIndex = std::clamp(
        std::min(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    const auto lastLaneIndex = std::clamp(
        std::max(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    std::vector<ExtractedNodeSelectionSegment> extractedSegments;
    const auto durationForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileDurationMs(filePath);
    };
    for (int laneIndex = firstLaneIndex; laneIndex <= lastLaneIndex; ++laneIndex)
    {
        dawg::node::LaneData laneCopy = nodeDocument.node.lanes[static_cast<std::size_t>(laneIndex)];
        static_cast<void>(extractNodeSelectionFromLane(
            laneCopy,
            normalizedStartMs,
            normalizedEndMs,
            laneIndex - firstLaneIndex,
            durationForPath,
            &extractedSegments));
    }
    if (extractedSegments.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("No audio clips were under the selection."));
        return false;
    }

    NodeSelectionClipboard clipboard;
    clipboard.durationMs = std::max(kMinimumClipDurationMs, normalizedEndMs - normalizedStartMs);
    clipboard.segments.reserve(extractedSegments.size());
    for (auto& segment : extractedSegments)
    {
        clipboard.segments.push_back(NodeSelectionClipboardSegment{std::move(segment.clip), segment.laneIndexOffset});
    }
    m_nodeEditorSelectionClipboard = std::move(clipboard);
    m_nodeEditorClipClipboard.reset();
    emit m_controller.statusChanged(
        QStringLiteral("Copied %1 clip segment(s).").arg(m_nodeEditorSelectionClipboard->segments.size()));
    return true;
}

bool NodeController::cutSelectedNodeClip(
    const QString& laneId,
    const QString& clipId,
    QString* selectedLaneId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (laneId.isEmpty() || clipId.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Select an audio clip before cutting."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before cutting clips.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    auto clipIt = std::find_if(
        laneIt->audioClips.begin(),
        laneIt->audioClips.end(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == laneIt->audioClips.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }

    m_nodeEditorClipClipboard = *clipIt;
    m_nodeEditorSelectionClipboard.reset();
    laneIt->audioClips.erase(clipIt);
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to cut node audio clip.")))
    {
        return false;
    }

    if (selectedLaneId)
    {
        *selectedLaneId = laneId;
    }
    emit m_controller.statusChanged(QStringLiteral("Cut audio clip."));
    return true;
}

bool NodeController::cutNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs,
    QString* selectedLaneId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }

    const auto normalizedStartMs = std::min(startMs, endMs);
    const auto normalizedEndMs = std::max(startMs, endMs);
    if (normalizedEndMs <= normalizedStartMs)
    {
        emit m_controller.statusChanged(QStringLiteral("Drag a time selection before cutting it."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before cutting a selection.")
                : errorMessage);
        return false;
    }
    if (nodeDocument.node.lanes.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("This node has no audio to cut."));
        return false;
    }

    const auto firstLaneIndex = std::clamp(
        std::min(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    const auto lastLaneIndex = std::clamp(
        std::max(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    const auto durationForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileDurationMs(filePath);
    };

    std::vector<ExtractedNodeSelectionSegment> extractedSegments;
    bool documentChanged = false;
    for (int laneIndex = firstLaneIndex; laneIndex <= lastLaneIndex; ++laneIndex)
    {
        documentChanged = extractNodeSelectionFromLane(
            nodeDocument.node.lanes[static_cast<std::size_t>(laneIndex)],
            normalizedStartMs,
            normalizedEndMs,
            laneIndex - firstLaneIndex,
            durationForPath,
            &extractedSegments)
            || documentChanged;
    }
    if (!documentChanged || extractedSegments.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("No audio clips were under the selection."));
        return false;
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to cut the selected time range.")))
    {
        return false;
    }

    NodeSelectionClipboard clipboard;
    clipboard.durationMs = std::max(kMinimumClipDurationMs, normalizedEndMs - normalizedStartMs);
    clipboard.segments.reserve(extractedSegments.size());
    for (auto& segment : extractedSegments)
    {
        clipboard.segments.push_back(NodeSelectionClipboardSegment{std::move(segment.clip), segment.laneIndexOffset});
    }
    m_nodeEditorSelectionClipboard = std::move(clipboard);
    m_nodeEditorClipClipboard.reset();
    if (selectedLaneId)
    {
        *selectedLaneId = nodeDocument.node.lanes[static_cast<std::size_t>(firstLaneIndex)].id;
    }
    emit m_controller.statusChanged(
        QStringLiteral("Cut %1 clip segment(s).").arg(m_nodeEditorSelectionClipboard->segments.size()));
    return true;
}

bool NodeController::pasteSelectedNodeClip(
    const QString& targetLaneId,
    const int playheadMs,
    QString* pastedLaneId,
    QString* pastedClipId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (!m_nodeEditorClipClipboard.has_value()
        && (!m_nodeEditorSelectionClipboard.has_value()
            || m_nodeEditorSelectionClipboard->segments.empty()))
    {
        emit m_controller.statusChanged(QStringLiteral("Copy or cut audio before pasting."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before pasting clips.")
                : errorMessage);
        return false;
    }

    if (nodeDocument.node.lanes.empty())
    {
        nodeDocument.node.lanes.push_back(dawg::node::LaneData{.label = QStringLiteral("Track 1")});
    }

    auto resolvedLaneId = targetLaneId;
    auto laneIt = resolvedLaneId.isEmpty()
        ? nodeDocument.node.lanes.end()
        : std::find_if(
            nodeDocument.node.lanes.begin(),
            nodeDocument.node.lanes.end(),
            [&resolvedLaneId](const dawg::node::LaneData& lane)
            {
                return lane.id == resolvedLaneId;
            });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        laneIt = nodeDocument.node.lanes.begin();
        resolvedLaneId = laneIt->id;
    }

    QString nextSelectedLaneId;
    QString nextSelectedClipId;
    const auto durationForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileDurationMs(filePath);
    };
    if (m_nodeEditorSelectionClipboard.has_value() && !m_nodeEditorSelectionClipboard->segments.empty())
    {
        const auto targetLaneIndex =
            static_cast<int>(std::distance(nodeDocument.node.lanes.begin(), laneIt));
        const auto maxLaneOffset = std::max_element(
            m_nodeEditorSelectionClipboard->segments.cbegin(),
            m_nodeEditorSelectionClipboard->segments.cend(),
            [](const NodeSelectionClipboardSegment& lhs, const NodeSelectionClipboardSegment& rhs)
            {
                return lhs.laneIndexOffset < rhs.laneIndexOffset;
            });
        const auto requiredLaneCount =
            targetLaneIndex + (maxLaneOffset != m_nodeEditorSelectionClipboard->segments.cend()
                ? maxLaneOffset->laneIndexOffset + 1
                : 1);
        while (static_cast<int>(nodeDocument.node.lanes.size()) < requiredLaneCount)
        {
            dawg::node::LaneData createdLane;
            createdLane.label = defaultNodeLaneLabel(static_cast<int>(nodeDocument.node.lanes.size()) + 1);
            nodeDocument.node.lanes.push_back(std::move(createdLane));
        }

        for (const auto& clipboardSegment : m_nodeEditorSelectionClipboard->segments)
        {
            const auto destinationLaneIndex = std::clamp(
                targetLaneIndex + clipboardSegment.laneIndexOffset,
                0,
                static_cast<int>(nodeDocument.node.lanes.size()) - 1);
            auto& destinationLane = nodeDocument.node.lanes[static_cast<std::size_t>(destinationLaneIndex)];
            auto pastedClip = clipboardSegment.clip;
            pastedClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            pastedClip.laneOffsetMs = dawg::nodeeditor::clampedNodeAudioClipOffsetMs(
                pastedClip,
                playheadMs + clipboardSegment.clip.laneOffsetMs,
                nodeTimelineDurationMs(nodeDocument),
                durationForPath);
            if (pastedClip.attachedAudio.has_value())
            {
                pastedClip.attachedAudio->loopEnabled = false;
            }

            const auto insertedClipId = pastedClip.id;
            destinationLane.audioClips.push_back(std::move(pastedClip));
            resolveLaneClipOverlaps(destinationLane, insertedClipId, durationForPath);
            if (nextSelectedLaneId.isEmpty())
            {
                nextSelectedLaneId = destinationLane.id;
                nextSelectedClipId = insertedClipId;
            }
        }
    }
    else
    {
        auto pastedClip = *m_nodeEditorClipClipboard;
        pastedClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pastedClip.laneOffsetMs = dawg::nodeeditor::clampedNodeAudioClipOffsetMs(
            pastedClip,
            playheadMs,
            nodeTimelineDurationMs(nodeDocument),
            durationForPath);
        if (pastedClip.attachedAudio.has_value())
        {
            pastedClip.attachedAudio->loopEnabled = false;
        }

        nextSelectedClipId = pastedClip.id;
        nextSelectedLaneId = resolvedLaneId;
        laneIt->audioClips.push_back(std::move(pastedClip));
        resolveLaneClipOverlaps(*laneIt, nextSelectedClipId, durationForPath);
    }
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to paste node audio clip.")))
    {
        return false;
    }

    if (pastedLaneId)
    {
        *pastedLaneId = nextSelectedLaneId;
    }
    if (pastedClipId)
    {
        *pastedClipId = nextSelectedClipId;
    }
    emit m_controller.statusChanged(
        (m_nodeEditorSelectionClipboard.has_value() && !m_nodeEditorSelectionClipboard->segments.empty())
            ? QStringLiteral("Pasted audio selection.")
            : QStringLiteral("Pasted audio clip."));
    return true;
}

bool NodeController::dropSelectedNodeClip(
    const QString& sourceLaneId,
    const QString& clipId,
    const QString& targetLaneId,
    const int laneOffsetMs,
    const bool copyClip,
    QString* droppedLaneId,
    QString* droppedClipId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (sourceLaneId.isEmpty() || targetLaneId.isEmpty() || clipId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before editing clips.")
                : errorMessage);
        return false;
    }

    auto sourceLaneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&sourceLaneId](const dawg::node::LaneData& lane)
        {
            return lane.id == sourceLaneId;
        });
    if (sourceLaneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    auto targetLaneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&targetLaneId](const dawg::node::LaneData& lane)
        {
            return lane.id == targetLaneId;
        });
    if (targetLaneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Target node lane no longer exists."));
        return false;
    }

    auto clipIt = std::find_if(
        sourceLaneIt->audioClips.begin(),
        sourceLaneIt->audioClips.end(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == sourceLaneIt->audioClips.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }

    auto droppedClip = *clipIt;
    if (copyClip)
    {
        droppedClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    droppedClip.laneOffsetMs = dawg::nodeeditor::clampedNodeAudioClipOffsetMs(
        droppedClip,
        laneOffsetMs,
        nodeTimelineDurationMs(nodeDocument),
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileDurationMs(filePath);
        });
    if (droppedClip.attachedAudio.has_value())
    {
        droppedClip.attachedAudio->loopEnabled = false;
    }

    const auto nextClipId = droppedClip.id;
    if (copyClip)
    {
        targetLaneIt->audioClips.push_back(std::move(droppedClip));
        resolveLaneClipOverlaps(
            *targetLaneIt,
            nextClipId,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller.audioFileDurationMs(filePath);
            });
    }
    else if (sourceLaneId == targetLaneId)
    {
        *clipIt = std::move(droppedClip);
        resolveLaneClipOverlaps(
            *targetLaneIt,
            nextClipId,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller.audioFileDurationMs(filePath);
            });
    }
    else
    {
        sourceLaneIt->audioClips.erase(clipIt);
        targetLaneIt->audioClips.push_back(std::move(droppedClip));
        resolveLaneClipOverlaps(
            *targetLaneIt,
            nextClipId,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller.audioFileDurationMs(filePath);
            });
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            copyClip
                ? QStringLiteral("Failed to copy node audio clip.")
                : QStringLiteral("Failed to move node audio clip.")))
    {
        return false;
    }

    if (droppedLaneId)
    {
        *droppedLaneId = targetLaneId;
    }
    if (droppedClipId)
    {
        *droppedClipId = nextClipId;
    }
    emit m_controller.statusChanged(copyClip ? QStringLiteral("Copied audio clip.") : QStringLiteral("Moved audio clip."));
    return true;
}

std::optional<int> NodeController::nodeLaneClipCount(const QString& laneId) const
{
    if (!m_controller.hasSelection() || laneId.isEmpty())
    {
        return std::nullopt;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        return std::nullopt;
    }

    const auto laneIt = std::find_if(
        nodeDocument.node.lanes.cbegin(),
        nodeDocument.node.lanes.cend(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.cend())
    {
        return std::nullopt;
    }

    return static_cast<int>(laneIt->audioClips.size());
}

bool NodeController::deleteSelectedNodeClipOrLane(
    const QString& laneId,
    const QString& laneHeaderId,
    const QString& clipId,
    const bool allowDeletePopulatedLane,
    QString* nextSelectedLaneId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (clipId.isEmpty() && laneHeaderId.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Select a lane name or audio clip before deleting."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before deleting clips.")
                : errorMessage);
        return false;
    }

    const auto targetLaneId = clipId.isEmpty() ? laneHeaderId : laneId;
    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&targetLaneId](const dawg::node::LaneData& lane)
        {
            return lane.id == targetLaneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    QString resolvedNextLaneId = laneIt->id;
    QString statusText;
    if (!clipId.isEmpty())
    {
        const auto originalClipCount = laneIt->audioClips.size();
        laneIt->audioClips.erase(
            std::remove_if(
                laneIt->audioClips.begin(),
                laneIt->audioClips.end(),
                [&clipId](const dawg::node::AudioClipData& clip)
                {
                    return clip.id == clipId;
                }),
            laneIt->audioClips.end());
        if (laneIt->audioClips.size() == originalClipCount)
        {
            emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
            return false;
        }
        statusText = QStringLiteral("Deleted audio clip.");
    }
    else
    {
        if (!allowDeletePopulatedLane && !laneIt->audioClips.empty())
        {
            emit m_controller.statusChanged(QStringLiteral("This lane contains audio clips."));
            return false;
        }

        const auto laneIndex = static_cast<std::size_t>(std::distance(nodeDocument.node.lanes.begin(), laneIt));
        laneIt = nodeDocument.node.lanes.erase(laneIt);
        if (!nodeDocument.node.lanes.empty())
        {
            const auto nextLaneIndex = std::min(laneIndex, nodeDocument.node.lanes.size() - 1);
            resolvedNextLaneId = nodeDocument.node.lanes[nextLaneIndex].id;
        }
        else
        {
            resolvedNextLaneId.clear();
        }
        statusText = QStringLiteral("Deleted lane.");
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to delete node editor selection.")))
    {
        return false;
    }

    if (nextSelectedLaneId)
    {
        *nextSelectedLaneId = resolvedNextLaneId;
    }
    emit m_controller.statusChanged(statusText);
    return true;
}

bool NodeController::deleteNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs,
    QString* nextSelectedLaneId)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }

    const auto normalizedStartMs = std::min(startMs, endMs);
    const auto normalizedEndMs = std::max(startMs, endMs);
    if (normalizedEndMs <= normalizedStartMs)
    {
        emit m_controller.statusChanged(QStringLiteral("Drag a time selection before deleting it."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before deleting a selection.")
                : errorMessage);
        return false;
    }
    if (nodeDocument.node.lanes.empty())
    {
        emit m_controller.statusChanged(QStringLiteral("This node has no audio to delete."));
        return false;
    }

    const auto firstLaneIndex = std::clamp(
        std::min(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    const auto lastLaneIndex = std::clamp(
        std::max(startLaneIndex, endLaneIndex),
        0,
        static_cast<int>(nodeDocument.node.lanes.size()) - 1);
    const auto durationForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileDurationMs(filePath);
    };

    bool documentChanged = false;
    for (int laneIndex = firstLaneIndex; laneIndex <= lastLaneIndex; ++laneIndex)
    {
        documentChanged = extractNodeSelectionFromLane(
            nodeDocument.node.lanes[static_cast<std::size_t>(laneIndex)],
            normalizedStartMs,
            normalizedEndMs,
            laneIndex - firstLaneIndex,
            durationForPath,
            nullptr)
            || documentChanged;
    }
    if (!documentChanged)
    {
        emit m_controller.statusChanged(QStringLiteral("No audio clips were under the selection."));
        return false;
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to delete the selected time range.")))
    {
        return false;
    }

    if (nextSelectedLaneId)
    {
        *nextSelectedLaneId = nodeDocument.node.lanes[static_cast<std::size_t>(firstLaneIndex)].id;
    }
    emit m_controller.statusChanged(QStringLiteral("Deleted selected time range."));
    return true;
}

bool NodeController::setNodeLaneMuted(const QString& laneId, const bool muted)
{
    if (!m_controller.hasSelection() || laneId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before muting lanes.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }
    if (laneIt->muted == muted)
    {
        return false;
    }

    laneIt->muted = muted;
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to update node lane mute.")))
    {
        return false;
    }

    emit m_controller.statusChanged(muted ? QStringLiteral("Node lane muted.") : QStringLiteral("Node lane unmuted."));
    return true;
}

bool NodeController::setNodeLaneSoloed(const QString& laneId, const bool soloed)
{
    if (!m_controller.hasSelection() || laneId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before soloing lanes.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    bool changed = false;
    if (m_controller.isMixSoloXorMode() && soloed)
    {
        for (auto& lane : nodeDocument.node.lanes)
        {
            const auto nextSoloed = lane.id == laneId;
            if (lane.soloed != nextSoloed)
            {
                lane.soloed = nextSoloed;
                changed = true;
            }
        }
    }
    else if (laneIt->soloed != soloed)
    {
        laneIt->soloed = soloed;
        changed = true;
    }
    if (!changed)
    {
        return false;
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to update node lane solo.")))
    {
        return false;
    }

    emit m_controller.statusChanged(soloed ? QStringLiteral("Node lane soloed.") : QStringLiteral("Node lane solo cleared."));
    return true;
}

bool NodeController::moveNodeClip(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs,
    const int nodeDurationMs)
{
    if (!m_controller.hasSelection() || laneId.isEmpty() || clipId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before moving clips.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    auto clipIt = std::find_if(
        laneIt->audioClips.begin(),
        laneIt->audioClips.end(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == laneIt->audioClips.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }

    const auto nextOffsetMs = dawg::nodeeditor::clampedNodeAudioClipOffsetMs(
        *clipIt,
        laneOffsetMs,
        std::max(1, nodeDurationMs),
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileDurationMs(filePath);
        });
    if (clipIt->laneOffsetMs == nextOffsetMs)
    {
        return false;
    }

    clipIt->laneOffsetMs = nextOffsetMs;
    resolveLaneClipOverlaps(
        *laneIt,
        clipId,
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileDurationMs(filePath);
        });
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to move node audio clip.")))
    {
        return false;
    }

    emit m_controller.statusChanged(QStringLiteral("Moved audio clip."));
    return true;
}

bool NodeController::splitNodeClipAtPlayhead(
    const QString& laneId,
    const QString& clipId,
    const int playheadMs,
    QString* selectedLaneId,
    QString* selectedClipId)
{
    if (!m_controller.hasSelection() || laneId.isEmpty() || clipId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before cutting clips.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    auto clipIt = std::find_if(
        laneIt->audioClips.begin(),
        laneIt->audioClips.end(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == laneIt->audioClips.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }
    if (!clipIt->attachedAudio.has_value() || clipIt->attachedAudio->assetPath.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip has no audio to cut."));
        return false;
    }

    const auto durationMs = m_controller.audioFileDurationMs(clipIt->attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 1)
    {
        emit m_controller.statusChanged(QStringLiteral("Failed to read the selected audio clip length."));
        return false;
    }

    const auto sourceDurationMs = *durationMs;
    const auto clipStartMs = std::clamp(
        clipIt->attachedAudio->clipStartMs,
        0,
        std::max(0, sourceDurationMs - kMinimumClipDurationMs));
    const auto clipEndMs = std::clamp(
        clipIt->attachedAudio->clipEndMs.value_or(sourceDurationMs),
        clipStartMs + kMinimumClipDurationMs,
        sourceDurationMs);
    const auto clipDurationMs = std::max(kMinimumClipDurationMs, clipEndMs - clipStartMs);
    const auto clipStartNodeMs = std::max(0, clipIt->laneOffsetMs);
    const auto clipEndNodeMs = clipStartNodeMs + clipDurationMs;
    if (playheadMs <= clipStartNodeMs || playheadMs >= clipEndNodeMs)
    {
        emit m_controller.statusChanged(QStringLiteral("Move the marker inside the audio clip before cutting it."));
        return false;
    }

    const auto splitOffsetMs = playheadMs - clipStartNodeMs;
    const auto leftClipEndMs = clipStartMs + splitOffsetMs;
    if (leftClipEndMs <= clipStartMs || leftClipEndMs >= clipEndMs)
    {
        emit m_controller.statusChanged(QStringLiteral("Move the marker inside the audio clip before cutting it."));
        return false;
    }

    const auto clipIndex = static_cast<std::size_t>(std::distance(laneIt->audioClips.begin(), clipIt));
    const auto originalClip = *clipIt;
    clipIt->attachedAudio->clipEndMs = leftClipEndMs;
    clipIt->attachedAudio->loopEnabled = false;

    auto rightClip = originalClip;
    rightClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rightClip.laneOffsetMs = playheadMs;
    if (rightClip.attachedAudio.has_value())
    {
        rightClip.attachedAudio->clipStartMs = leftClipEndMs;
        rightClip.attachedAudio->clipEndMs = clipEndMs;
        rightClip.attachedAudio->loopEnabled = false;
    }

    laneIt->audioClips.insert(laneIt->audioClips.begin() + static_cast<std::ptrdiff_t>(clipIndex + 1), rightClip);
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to cut node audio clip.")))
    {
        return false;
    }

    if (selectedLaneId)
    {
        *selectedLaneId = laneId;
    }
    if (selectedClipId)
    {
        *selectedClipId = clipId;
    }

    emit m_controller.statusChanged(QStringLiteral("Cut audio clip at marker."));
    return true;
}

bool NodeController::resolveNodeClipAtPlayhead(
    const QString& preferredLaneId,
    const int playheadMs,
    QString* resolvedLaneId,
    QString* resolvedClipId) const
{
    if (!m_controller.hasSelection())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        return false;
    }

    auto resolveFromLane = [this, playheadMs, resolvedLaneId, resolvedClipId](const dawg::node::LaneData& lane) -> bool
    {
        for (const auto& clip : lane.audioClips)
        {
            const auto clipRange = resolvedNodeClipRange(
                clip,
                [this](const QString& filePath) -> std::optional<int>
                {
                    return m_controller.audioFileDurationMs(filePath);
                });
            if (!clipRange.has_value())
            {
                continue;
            }

            if (playheadMs < clipRange->laneStartMs || playheadMs > clipRange->laneEndMs)
            {
                continue;
            }

            if (resolvedLaneId)
            {
                *resolvedLaneId = lane.id;
            }
            if (resolvedClipId)
            {
                *resolvedClipId = clip.id;
            }
            return true;
        }
        return false;
    };

    if (!preferredLaneId.isEmpty())
    {
        const auto preferredLaneIt = std::find_if(
            nodeDocument.node.lanes.cbegin(),
            nodeDocument.node.lanes.cend(),
            [&preferredLaneId](const dawg::node::LaneData& lane)
            {
                return lane.id == preferredLaneId;
            });
        if (preferredLaneIt != nodeDocument.node.lanes.cend() && resolveFromLane(*preferredLaneIt))
        {
            return true;
        }
    }

    for (const auto& lane : nodeDocument.node.lanes)
    {
        if (!preferredLaneId.isEmpty() && lane.id == preferredLaneId)
        {
            continue;
        }
        if (resolveFromLane(lane))
        {
            return true;
        }
    }

    return false;
}

bool NodeController::trimNodeClip(
    const QString& laneId,
    const QString& clipId,
    const int targetMs,
    const bool trimStart,
    const int nodeDurationMs)
{
    if (!m_controller.hasSelection() || laneId.isEmpty() || clipId.isEmpty())
    {
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before trimming clips.")
                : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument.node.lanes.begin(),
        nodeDocument.node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument.node.lanes.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected node lane no longer exists."));
        return false;
    }

    auto clipIt = std::find_if(
        laneIt->audioClips.begin(),
        laneIt->audioClips.end(),
        [&clipId](const dawg::node::AudioClipData& clip)
        {
            return clip.id == clipId;
        });
    if (clipIt == laneIt->audioClips.end())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip no longer exists."));
        return false;
    }
    if (!clipIt->attachedAudio.has_value() || clipIt->attachedAudio->assetPath.isEmpty())
    {
        emit m_controller.statusChanged(QStringLiteral("Selected audio clip has no audio to trim."));
        return false;
    }

    const auto durationMs = m_controller.audioFileDurationMs(clipIt->attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 1)
    {
        emit m_controller.statusChanged(QStringLiteral("Failed to read the selected audio clip length."));
        return false;
    }

    const auto sourceDurationMs = *durationMs;
    const auto clipStartMs = std::clamp(
        clipIt->attachedAudio->clipStartMs,
        0,
        std::max(0, sourceDurationMs - kMinimumClipDurationMs));
    const auto clipEndMs = std::clamp(
        clipIt->attachedAudio->clipEndMs.value_or(sourceDurationMs),
        clipStartMs + kMinimumClipDurationMs,
        sourceDurationMs);
    const auto clipDurationMs = clipEndMs - clipStartMs;
    const auto safeNodeDurationMs = std::max(1, nodeDurationMs);
    const auto clampedTargetMs = std::clamp(targetMs, 0, safeNodeDurationMs);

    auto nextLaneOffsetMs = std::max(0, clipIt->laneOffsetMs);
    auto nextClipStartMs = clipStartMs;
    auto nextClipEndMs = clipEndMs;

    if (trimStart)
    {
        const auto oldStartNodeMs = std::max(0, clipIt->laneOffsetMs);
        const auto desiredDeltaMs = clampedTargetMs - oldStartNodeMs;
        const auto minimumDeltaMs = std::max(-clipStartMs, -oldStartNodeMs);
        const auto maximumDeltaMs = clipDurationMs - kMinimumClipDurationMs;
        if (maximumDeltaMs < minimumDeltaMs)
        {
            emit m_controller.statusChanged(QStringLiteral("Selected audio clip is already too short to trim."));
            return false;
        }

        const auto deltaMs = std::clamp(desiredDeltaMs, minimumDeltaMs, maximumDeltaMs);
        nextLaneOffsetMs = oldStartNodeMs + deltaMs;
        nextClipStartMs = clipStartMs + deltaMs;
    }
    else
    {
        const auto oldEndNodeMs = std::max(0, clipIt->laneOffsetMs) + clipDurationMs;
        const auto desiredDeltaMs = clampedTargetMs - oldEndNodeMs;
        const auto minimumDeltaMs = -(clipDurationMs - kMinimumClipDurationMs);
        auto maximumDeltaMs = std::min(
            sourceDurationMs - clipEndMs,
            safeNodeDurationMs - oldEndNodeMs);
        maximumDeltaMs = std::max(minimumDeltaMs, maximumDeltaMs);

        const auto deltaMs = std::clamp(desiredDeltaMs, minimumDeltaMs, maximumDeltaMs);
        nextClipEndMs = clipEndMs + deltaMs;
    }

    if (nextLaneOffsetMs == clipIt->laneOffsetMs
        && nextClipStartMs == clipIt->attachedAudio->clipStartMs
        && clipIt->attachedAudio->clipEndMs.value_or(sourceDurationMs) == nextClipEndMs)
    {
        return false;
    }

    clipIt->laneOffsetMs = nextLaneOffsetMs;
    clipIt->attachedAudio->clipStartMs = nextClipStartMs;
    clipIt->attachedAudio->clipEndMs = nextClipEndMs;
    clipIt->attachedAudio->loopEnabled = false;
    resolveLaneClipOverlaps(
        *laneIt,
        clipId,
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileDurationMs(filePath);
        });
    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            QStringLiteral("Failed to trim node audio clip.")))
    {
        return false;
    }

    emit m_controller.statusChanged(
        trimStart ? QStringLiteral("Trimmed audio clip start.") : QStringLiteral("Trimmed audio clip end."));
    return true;
}

bool NodeController::addNodeLaneOrImportClip(
    const QString& nodesDirectoryPath,
    const QString& importedAudioFilePath,
    const QString& selectedLaneId,
    QString* resolvedLaneId,
    QString* resolvedLaneLabel)
{
    if (!m_controller.hasSelection())
    {
        return false;
    }
    if (nodesDirectoryPath.trimmed().isEmpty())
    {
        return false;
    }

    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    if (selectedTrackId.isNull())
    {
        emit m_controller.statusChanged(QStringLiteral("Select a node before editing its lanes."));
        return false;
    }

    QString nodeDocumentPath;
    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocumentPath, &nodeDocument, &errorMessage))
    {
        const auto nodeLabel = m_controller.trackLabel(selectedTrackId).trimmed().isEmpty()
            ? QStringLiteral("Node")
            : m_controller.trackLabel(selectedTrackId).trimmed();
        nodeDocumentPath = uniqueTargetFilePath(
            nodesDirectoryPath,
            dawg::node::nodeFileNameForName(nodeLabel));
        nodeDocument.name = nodeLabel;
        nodeDocument.node.label = nodeLabel;
        nodeDocument.node.autoPanEnabled = m_controller.selectedTracksAutoPanEnabled();
        nodeDocument.node.timelineFrameCount = m_controller.totalFrames();
        nodeDocument.node.timelineFps = m_controller.fps();
    }

    if (nodeDocument.node.label.trimmed().isEmpty())
    {
        nodeDocument.node.label = m_controller.trackLabel(selectedTrackId).trimmed();
    }
    if (nodeDocument.name.trimmed().isEmpty())
    {
        nodeDocument.name = nodeDocument.node.label.trimmed().isEmpty()
            ? QStringLiteral("Node")
            : nodeDocument.node.label.trimmed();
    }
    nodeDocument.node.timelineFrameCount = m_controller.totalFrames();
    nodeDocument.node.timelineFps = m_controller.fps();
    nodeDocument.node.autoPanEnabled = m_controller.selectedTracksAutoPanEnabled();

    auto laneIt = selectedLaneId.isEmpty()
        ? nodeDocument.node.lanes.end()
        : std::find_if(
            nodeDocument.node.lanes.begin(),
            nodeDocument.node.lanes.end(),
            [&selectedLaneId](const dawg::node::LaneData& lane)
            {
                return lane.id == selectedLaneId;
            });
    if (importedAudioFilePath.trimmed().isEmpty() || laneIt == nodeDocument.node.lanes.end())
    {
        dawg::node::LaneData lane;
        lane.label = QStringLiteral("Track %1").arg(static_cast<int>(nodeDocument.node.lanes.size()) + 1);
        nodeDocument.node.lanes.push_back(lane);
        laneIt = std::prev(nodeDocument.node.lanes.end());
    }

    if (!importedAudioFilePath.trimmed().isEmpty())
    {
        const QFileInfo importedAudioInfo(importedAudioFilePath);
        dawg::node::AudioClipData importedClip;
        importedClip.label = importedAudioInfo.completeBaseName().isEmpty()
            ? importedAudioInfo.fileName()
            : importedAudioInfo.completeBaseName();
        importedClip.attachedAudio = AudioAttachment{
            .assetPath = importedAudioFilePath,
            .gainDb = 0.0F,
            .clipStartMs = 0,
            .clipEndMs = std::nullopt,
            .loopEnabled = false
        };
        laneIt->audioClips.push_back(importedClip);
        resolveLaneClipOverlaps(
            *laneIt,
            importedClip.id,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller.audioFileDurationMs(filePath);
            });
    }

    if (!saveSelectedNodeDocument(
            nodeDocumentPath,
            nodeDocument,
            importedAudioFilePath.trimmed().isEmpty()
                ? QStringLiteral("Failed to create node lane.")
                : QStringLiteral("Failed to import node audio clip.")))
    {
        return false;
    }

    if (resolvedLaneId)
    {
        *resolvedLaneId = laneIt->id;
    }
    if (resolvedLaneLabel)
    {
        *resolvedLaneLabel = laneIt->label;
    }
    return true;
}

bool NodeController::saveSelectedNodeToFile(
    const QString& nodeFilePath,
    const bool bindToSelectedTrack,
    const QString& nodeLabelOverride,
    QString* errorMessage)
{
    if (!m_controller.hasSelection())
    {
        const auto message = QStringLiteral("Select a node before saving it.");
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    if (selectedTrackId.isNull())
    {
        const auto message = QStringLiteral("Select a node before saving it.");
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    const auto currentState = m_controller.selectedAudioClipPreviewState();
    const auto trackLabel = m_controller.trackLabel(selectedTrackId).trimmed();
    const auto nodeLabel = !nodeLabelOverride.trimmed().isEmpty()
        ? nodeLabelOverride.trimmed()
        : (trackLabel.isEmpty() ? QStringLiteral("Node") : trackLabel);
    dawg::node::Document nodeDocument;
    const auto boundNodeDocumentPath = m_controller.trackNodeDocumentPath(selectedTrackId);
    auto targetNodeFilePath = QDir::cleanPath(nodeFilePath);
    QString obsoleteNodeFilePath;
    if (!boundNodeDocumentPath.isEmpty() && QFileInfo::exists(boundNodeDocumentPath))
    {
        QString loadError;
        const auto loadedDocument = dawg::node::loadDocument(boundNodeDocumentPath, &loadError);
        if (!loadedDocument.has_value())
        {
            if (errorMessage)
            {
                *errorMessage = loadError;
            }
            emit m_controller.statusChanged(loadError);
            return false;
        }
        nodeDocument = *loadedDocument;
    }

    if (bindToSelectedTrack && !boundNodeDocumentPath.trimmed().isEmpty())
    {
        const auto cleanedBoundPath = QDir::cleanPath(boundNodeDocumentPath);
        if (QString::compare(targetNodeFilePath, cleanedBoundPath, nodePathCaseSensitivity()) == 0)
        {
            const QFileInfo boundInfo(cleanedBoundPath);
            const auto renamedNodeFilePath = QDir(boundInfo.absolutePath()).filePath(
                dawg::node::nodeFileNameForName(nodeLabel));
            const auto cleanedRenamedNodeFilePath = QDir::cleanPath(renamedNodeFilePath);
            if (QString::compare(cleanedRenamedNodeFilePath, cleanedBoundPath, nodePathCaseSensitivity()) != 0)
            {
                if (QFileInfo::exists(cleanedRenamedNodeFilePath))
                {
                    const auto message = QStringLiteral(
                        "A node file named \"%1\" already exists.\nUse Save Node As... to choose a different file.")
                        .arg(QFileInfo(cleanedRenamedNodeFilePath).fileName());
                    if (errorMessage)
                    {
                        *errorMessage = message;
                    }
                    emit m_controller.statusChanged(message);
                    return false;
                }
                targetNodeFilePath = cleanedRenamedNodeFilePath;
                obsoleteNodeFilePath = cleanedBoundPath;
            }
        }
    }

    nodeDocument.name = nodeLabel;
    nodeDocument.node.label = nodeLabel;
    nodeDocument.node.autoPanEnabled = m_controller.selectedTracksAutoPanEnabled();
    nodeDocument.node.timelineFrameCount = m_controller.totalFrames();
    nodeDocument.node.timelineFps = m_controller.fps();

    if (nodeDocument.node.lanes.empty())
    {
        nodeDocument.node.lanes.push_back(dawg::node::LaneData{.label = QStringLiteral("Lane 1")});
    }

    if (currentState.has_value())
    {
        auto runtimeClip = dawg::nodeeditor::nodeAudioClipFromClipState(*currentState, nodeLabel);
        auto* targetLane = &nodeDocument.node.lanes.front();
        dawg::node::AudioClipData* targetClip = nullptr;
        for (auto& lane : nodeDocument.node.lanes)
        {
            const auto targetClipIt = std::find_if(
                lane.audioClips.begin(),
                lane.audioClips.end(),
                [](const dawg::node::AudioClipData& clip)
                {
                    return clip.attachedAudio.has_value();
                });
            if (targetClipIt != lane.audioClips.end())
            {
                targetLane = &lane;
                targetClip = &(*targetClipIt);
                break;
            }
        }
        if (targetClip == nullptr)
        {
            targetLane->audioClips.push_back(runtimeClip);
        }
        else
        {
            *targetClip = runtimeClip;
        }
    }

    QString saveError;
    if (!dawg::node::saveDocument(targetNodeFilePath, nodeDocument, &saveError))
    {
        const auto message = saveError.isEmpty() ? QStringLiteral("Failed to save the selected node.") : saveError;
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    if (bindToSelectedTrack)
    {
        if (m_controller.trackLabel(selectedTrackId).trimmed() != nodeLabel)
        {
            static_cast<void>(m_controller.renameTrack(selectedTrackId, nodeLabel));
        }
        if (!obsoleteNodeFilePath.isEmpty()
            && QString::compare(obsoleteNodeFilePath, targetNodeFilePath, nodePathCaseSensitivity()) != 0
            && QFileInfo::exists(obsoleteNodeFilePath))
        {
            QFile::remove(obsoleteNodeFilePath);
        }
        static_cast<void>(m_controller.setTrackNodeDocument(
            selectedTrackId,
            targetNodeFilePath,
            nodeDocument.node.timelineFrameCount,
            nodeDocument.node.timelineFps));
    }

    if (errorMessage)
    {
        errorMessage->clear();
    }
    emit m_controller.statusChanged(QStringLiteral("Saved node to %1.").arg(QFileInfo(targetNodeFilePath).fileName()));
    emit m_controller.editStateChanged();
    return true;
}

bool NodeController::openNodeFileAsNewNode(
    const QString& nodeFilePath,
    const QString& projectRootPath,
    QString* errorMessage)
{
    if (!m_controller.hasVideoLoaded())
    {
        const auto message = QStringLiteral("Open a video before opening a saved node.");
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    QString loadError;
    const auto document = dawg::node::loadDocument(nodeFilePath, &loadError);
    if (!document.has_value())
    {
        const auto message = loadError.isEmpty() ? QStringLiteral("Failed to open the selected node.") : loadError;
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    const auto frameSize = m_controller.videoFrameSize();
    const auto imageCenter = QPointF{
        std::max(1, frameSize.width()) * 0.5,
        std::max(1, frameSize.height()) * 0.5
    };
    const auto nodesDirectoryPath = QDir(projectRootPath).filePath(QStringLiteral("nodes"));
    if (nodesDirectoryPath.isEmpty() || !QDir().mkpath(nodesDirectoryPath))
    {
        const auto message = QStringLiteral("Failed to create the project nodes folder.");
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    dawg::node::Document materializedDocument = *document;
    const auto preferredNodeName = !materializedDocument.node.label.trimmed().isEmpty()
        ? materializedDocument.node.label.trimmed()
        : QFileInfo(nodeFilePath).completeBaseName();
    const auto targetNodePath = uniqueTargetFilePath(
        nodesDirectoryPath,
        dawg::node::nodeFileNameForName(preferredNodeName.isEmpty() ? QStringLiteral("Node") : preferredNodeName));
    const auto audioDirectoryPath = QDir(projectRootPath).filePath(QStringLiteral("audio"));
    for (auto& lane : materializedDocument.node.lanes)
    {
        for (auto& clip : lane.audioClips)
        {
            if (!materializeNodeClipAudio(clip, audioDirectoryPath, &loadError))
            {
                if (errorMessage)
                {
                    *errorMessage = loadError;
                }
                emit m_controller.statusChanged(loadError);
                return false;
            }
        }
    }
    if (!dawg::node::saveDocument(targetNodePath, materializedDocument, &loadError))
    {
        const auto message = loadError.isEmpty() ? QStringLiteral("Failed to save the opened node.") : loadError;
        if (errorMessage)
        {
            *errorMessage = message;
        }
        emit m_controller.statusChanged(message);
        return false;
    }

    const dawg::node::AudioClipData* primaryClip = nullptr;
    for (const auto& lane : materializedDocument.node.lanes)
    {
        const auto clipIt = std::find_if(
            lane.audioClips.cbegin(),
            lane.audioClips.cend(),
            [](const dawg::node::AudioClipData& clip)
            {
                return clip.attachedAudio.has_value() && !clip.attachedAudio->assetPath.isEmpty();
            });
        if (clipIt != lane.audioClips.cend())
        {
            primaryClip = &(*clipIt);
            break;
        }
    }

    if (primaryClip != nullptr
        && primaryClip->attachedAudio.has_value()
        && !primaryClip->attachedAudio->assetPath.isEmpty())
    {
        if (!createTrackWithAudioAtCurrentFrame(primaryClip->attachedAudio->assetPath, imageCenter))
        {
            const auto message = QStringLiteral("Failed to create a node from the selected file.");
            if (errorMessage)
            {
                *errorMessage = message;
            }
            return false;
        }
        const auto importedState = m_controller.selectedAudioClipPreviewState();
        static_cast<void>(m_controller.setSelectedTrackClipRangeMs(
            primaryClip->attachedAudio->clipStartMs,
            primaryClip->attachedAudio->clipEndMs.value_or(
                importedState.has_value() ? importedState->clipEndMs : primaryClip->attachedAudio->clipStartMs)));
        static_cast<void>(m_controller.setSelectedTrackAudioGainDb(primaryClip->attachedAudio->gainDb));
    }
    else
    {
        seedTrack(imageCenter);
    }

    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    const auto preferredLabel = !materializedDocument.node.label.trimmed().isEmpty()
        ? materializedDocument.node.label.trimmed()
        : (primaryClip != nullptr ? primaryClip->label.trimmed() : QString{});
    if (!selectedTrackId.isNull() && !preferredLabel.isEmpty())
    {
        m_controller.renameTrack(selectedTrackId, preferredLabel);
    }
    if (!selectedTrackId.isNull())
    {
        static_cast<void>(m_controller.setTrackNodeDocument(
            selectedTrackId,
            targetNodePath,
            materializedDocument.node.timelineFrameCount > 0
                ? materializedDocument.node.timelineFrameCount
                : m_controller.totalFrames(),
            materializedDocument.node.timelineFps > 0.0
                ? materializedDocument.node.timelineFps
                : m_controller.fps()));
    }

    if (m_controller.selectedTracksAutoPanEnabled() != materializedDocument.node.autoPanEnabled)
    {
        toggleSelectedTrackAutoPan();
    }

    if (errorMessage)
    {
        errorMessage->clear();
    }
    emit m_controller.statusChanged(
        primaryClip != nullptr && materializedDocument.node.lanes.size() > 1
            ? QStringLiteral("Opened node %1 using the first lane audio clip for now.")
                .arg(QFileInfo(nodeFilePath).completeBaseName())
            : QStringLiteral("Opened node %1.").arg(QFileInfo(nodeFilePath).completeBaseName()));
    emit m_controller.editStateChanged();
    return true;
}

bool NodeController::loadSelectedNodeDocument(
    QString* nodeDocumentPath,
    dawg::node::Document* nodeDocument,
    QString* errorMessage) const
{
    if (!nodeDocumentPath || !nodeDocument)
    {
        return false;
    }

    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    const auto path = selectedTrackId.isNull()
        ? QString{}
        : m_controller.trackNodeDocumentPath(selectedTrackId).trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path))
    {
        if (errorMessage)
        {
            errorMessage->clear();
        }
        return false;
    }

    QString loadError;
    const auto loadedDocument = dawg::node::loadDocument(path, &loadError);
    if (!loadedDocument.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = loadError;
        }
        return false;
    }

    *nodeDocumentPath = path;
    *nodeDocument = *loadedDocument;
    if (errorMessage)
    {
        errorMessage->clear();
    }
    return true;
}

bool NodeController::saveSelectedNodeDocument(
    const QString& nodeDocumentPath,
    const dawg::node::Document& nodeDocument,
    const QString& failureStatus)
{
    QString errorMessage;
    if (!dawg::node::saveDocument(nodeDocumentPath, nodeDocument, &errorMessage))
    {
        emit m_controller.statusChanged(errorMessage.isEmpty() ? failureStatus : errorMessage);
        return false;
    }

    m_controller.invalidateNodePreviewClipCache(nodeDocumentPath);
    const auto selectedTrackId = m_controller.m_selectionController->selectedTrackId();
    static_cast<void>(m_controller.setTrackNodeDocument(
        selectedTrackId,
        QDir::cleanPath(nodeDocumentPath),
        nodeDocument.node.timelineFrameCount,
        nodeDocument.node.timelineFps));
    emit m_controller.editStateChanged();
    return true;
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

bool PlayerController::setTrackNodeDocument(
    const QUuid& trackId,
    const QString& nodeDocumentPath,
    const int timelineFrameCount,
    const double timelineFps)
{
    return m_tracker.setTrackNodeDocument(trackId, nodeDocumentPath, timelineFrameCount, timelineFps);
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

bool PlayerController::canPasteNodeClip() const
{
    return m_nodeController->canPasteNodeClip();
}

bool PlayerController::copySelectedNodeClip(const QString& laneId, const QString& clipId)
{
    return m_nodeController->copySelectedNodeClip(laneId, clipId);
}

bool PlayerController::copyNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs)
{
    return m_nodeController->copyNodeTimelineSelection(startLaneIndex, endLaneIndex, startMs, endMs);
}

bool PlayerController::cutSelectedNodeClip(
    const QString& laneId,
    const QString& clipId,
    QString* selectedLaneId)
{
    return m_nodeController->cutSelectedNodeClip(laneId, clipId, selectedLaneId);
}

bool PlayerController::cutNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs,
    QString* selectedLaneId)
{
    return m_nodeController->cutNodeTimelineSelection(
        startLaneIndex,
        endLaneIndex,
        startMs,
        endMs,
        selectedLaneId);
}

bool PlayerController::pasteSelectedNodeClip(
    const QString& targetLaneId,
    const int playheadMs,
    QString* pastedLaneId,
    QString* pastedClipId)
{
    return m_nodeController->pasteSelectedNodeClip(targetLaneId, playheadMs, pastedLaneId, pastedClipId);
}

bool PlayerController::dropSelectedNodeClip(
    const QString& sourceLaneId,
    const QString& clipId,
    const QString& targetLaneId,
    const int laneOffsetMs,
    const bool copyClip,
    QString* droppedLaneId,
    QString* droppedClipId)
{
    return m_nodeController->dropSelectedNodeClip(
        sourceLaneId,
        clipId,
        targetLaneId,
        laneOffsetMs,
        copyClip,
        droppedLaneId,
        droppedClipId);
}

std::optional<int> PlayerController::nodeLaneClipCount(const QString& laneId) const
{
    return m_nodeController->nodeLaneClipCount(laneId);
}

bool PlayerController::deleteSelectedNodeClipOrLane(
    const QString& laneId,
    const QString& laneHeaderId,
    const QString& clipId,
    const bool allowDeletePopulatedLane,
    QString* nextSelectedLaneId)
{
    return m_nodeController->deleteSelectedNodeClipOrLane(
        laneId,
        laneHeaderId,
        clipId,
        allowDeletePopulatedLane,
        nextSelectedLaneId);
}

bool PlayerController::deleteNodeTimelineSelection(
    const int startLaneIndex,
    const int endLaneIndex,
    const int startMs,
    const int endMs,
    QString* nextSelectedLaneId)
{
    return m_nodeController->deleteNodeTimelineSelection(
        startLaneIndex,
        endLaneIndex,
        startMs,
        endMs,
        nextSelectedLaneId);
}

bool PlayerController::setNodeLaneMuted(const QString& laneId, const bool muted)
{
    return m_nodeController->setNodeLaneMuted(laneId, muted);
}

bool PlayerController::setNodeLaneSoloed(const QString& laneId, const bool soloed)
{
    return m_nodeController->setNodeLaneSoloed(laneId, soloed);
}

bool PlayerController::moveNodeClip(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs,
    const int nodeDurationMs)
{
    return m_nodeController->moveNodeClip(laneId, clipId, laneOffsetMs, nodeDurationMs);
}

bool PlayerController::splitNodeClipAtPlayhead(
    const QString& laneId,
    const QString& clipId,
    const int playheadMs,
    QString* selectedLaneId,
    QString* selectedClipId)
{
    return m_nodeController->splitNodeClipAtPlayhead(
        laneId,
        clipId,
        playheadMs,
        selectedLaneId,
        selectedClipId);
}

bool PlayerController::resolveNodeClipAtPlayhead(
    const QString& preferredLaneId,
    const int playheadMs,
    QString* resolvedLaneId,
    QString* resolvedClipId) const
{
    return m_nodeController->resolveNodeClipAtPlayhead(
        preferredLaneId,
        playheadMs,
        resolvedLaneId,
        resolvedClipId);
}

bool PlayerController::trimNodeClip(
    const QString& laneId,
    const QString& clipId,
    const int targetMs,
    const bool trimStart,
    const int nodeDurationMs)
{
    return m_nodeController->trimNodeClip(laneId, clipId, targetMs, trimStart, nodeDurationMs);
}

bool PlayerController::addNodeLaneOrImportClip(
    const QString& nodesDirectoryPath,
    const QString& importedAudioFilePath,
    const QString& selectedLaneId,
    QString* resolvedLaneId,
    QString* resolvedLaneLabel)
{
    return m_nodeController->addNodeLaneOrImportClip(
        nodesDirectoryPath,
        importedAudioFilePath,
        selectedLaneId,
        resolvedLaneId,
        resolvedLaneLabel);
}

bool PlayerController::saveSelectedNodeToFile(
    const QString& nodeFilePath,
    const bool bindToSelectedTrack,
    const QString& nodeLabelOverride,
    QString* errorMessage)
{
    return m_nodeController->saveSelectedNodeToFile(
        nodeFilePath,
        bindToSelectedTrack,
        nodeLabelOverride,
        errorMessage);
}

bool PlayerController::openNodeFileAsNewNode(
    const QString& nodeFilePath,
    const QString& projectRootPath,
    QString* errorMessage)
{
    return m_nodeController->openNodeFileAsNewNode(nodeFilePath, projectRootPath, errorMessage);
}
