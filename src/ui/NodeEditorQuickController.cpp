#include "ui/NodeEditorQuickController.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QFileInfo>
#include <QTimer>
#include <QVariantMap>

namespace
{
QString formatTimeMs(const int timeMs)
{
    const auto safeMs = std::max(0, timeMs);
    const auto totalSeconds = safeMs / 1000;
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    const auto milliseconds = safeMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar{'0'})
        .arg(milliseconds / 10, 2, 10, QChar{'0'});
}

int frameToMs(const int frameIndex, const double fps)
{
    if (fps <= 0.0)
    {
        return 0;
    }

    return std::max(0, static_cast<int>(std::lround((static_cast<double>(frameIndex) * 1000.0) / fps)));
}

int nodeDurationMsFromState(const std::optional<AudioClipPreviewState>& state, const double fps)
{
    if (!state.has_value())
    {
        return 0;
    }

    return std::max(
        1,
        frameToMs(state->nodeEndFrame + 1, fps) - frameToMs(state->nodeStartFrame, fps));
}
}

NodeEditorQuickController::NodeEditorQuickController(QObject* parent)
    : QObject(parent)
{
}

void NodeEditorQuickController::setState(
    const bool canOpenNode,
    const QString& label,
    const QString& nodeContainerPath,
    const bool hasUnsavedChanges,
    const double timelineFps,
    const std::optional<AudioClipPreviewState>& clipState,
    const QVariantList& nodeTracks)
{
    const auto sameClipState = [this, &clipState]()
    {
        if (m_clipState.has_value() != clipState.has_value())
        {
            return false;
        }
        if (!m_clipState.has_value())
        {
            return true;
        }

        return m_clipState->label == clipState->label
            && m_clipState->assetPath == clipState->assetPath
            && m_clipState->hasAttachedAudio == clipState->hasAttachedAudio
            && m_clipState->nodeStartFrame == clipState->nodeStartFrame
            && m_clipState->nodeEndFrame == clipState->nodeEndFrame
            && m_clipState->clipStartMs == clipState->clipStartMs
            && m_clipState->clipEndMs == clipState->clipEndMs
            && m_clipState->sourceDurationMs == clipState->sourceDurationMs
            && std::abs(m_clipState->gainDb - clipState->gainDb) < 0.001F
            && m_clipState->loopEnabled == clipState->loopEnabled;
    }();
    if (m_canOpenNode == canOpenNode
        && m_selectedNodeLabel == label
        && m_nodeContainerPath == nodeContainerPath
        && m_hasUnsavedChanges == hasUnsavedChanges
        && std::abs(m_timelineFps - timelineFps) < 0.0001
        && sameClipState
        && m_nodeTracks == nodeTracks)
    {
        return;
    }

    const auto nextSelectedLaneId = [this, &nodeTracks]()
    {
        QString firstLaneId;
        for (const auto& laneValue : nodeTracks)
        {
            const auto laneMap = laneValue.toMap();
            const auto laneId = laneMap.value(QStringLiteral("laneId")).toString();
            if (laneId.isEmpty())
            {
                continue;
            }
            if (firstLaneId.isEmpty())
            {
                firstLaneId = laneId;
            }
            if (laneId == m_selectedLaneId)
            {
                return m_selectedLaneId;
            }
        }
        return firstLaneId;
    }();
    const auto nextSelectedClipId = [this, &nodeTracks, &nextSelectedLaneId]()
    {
        if (m_selectedClipId.isEmpty())
        {
            return QString{};
        }

        for (const auto& laneValue : nodeTracks)
        {
            const auto laneMap = laneValue.toMap();
            if (laneMap.value(QStringLiteral("laneId")).toString() != nextSelectedLaneId)
            {
                continue;
            }

            for (const auto& clipValue : laneMap.value(QStringLiteral("clips")).toList())
            {
                const auto clipMap = clipValue.toMap();
                if (clipMap.value(QStringLiteral("clipId")).toString() == m_selectedClipId)
                {
                    return m_selectedClipId;
                }
            }
        }
        return QString{};
    }();
    const auto nextSelectedLaneHeaderId = [this, &nodeTracks]()
    {
        if (m_selectedLaneHeaderId.isEmpty())
        {
            return QString{};
        }

        for (const auto& laneValue : nodeTracks)
        {
            const auto laneMap = laneValue.toMap();
            if (laneMap.value(QStringLiteral("laneId")).toString() == m_selectedLaneHeaderId)
            {
                return m_selectedLaneHeaderId;
            }
        }
        return QString{};
    }();

    m_canOpenNode = canOpenNode;
    m_selectedNodeLabel = label;
    m_nodeContainerPath = nodeContainerPath;
    m_hasUnsavedChanges = hasUnsavedChanges;
    m_timelineFps = timelineFps;
    m_clipState = clipState;
    m_nodeTracks = nodeTracks;
    m_selectedLaneId = nextSelectedLaneId;
    m_selectedLaneHeaderId = nextSelectedLaneHeaderId;
    m_selectedClipId = nextSelectedClipId;
    m_playheadMs = std::clamp(m_playheadMs, 0, nodeDurationMs());
    m_insertionMarkerMs = std::clamp(m_insertionMarkerMs, 0, nodeDurationMs());
    const auto meterTopologyChanged = syncLaneMeterTopology(m_nodeTracks);
    if (meterTopologyChanged)
    {
        ++m_laneMeterToken;
        ++m_meterResetToken;
        emit laneMeterLevelsChanged();
        emit meterResetTokenChanged();
    }
    emit playheadPositionChanged();
    emit stateChanged();
}

bool NodeEditorQuickController::canOpenNode() const
{
    return m_canOpenNode;
}

bool NodeEditorQuickController::canSaveNode() const
{
    return hasSelection() && m_hasUnsavedChanges;
}

bool NodeEditorQuickController::canSaveNodeAs() const
{
    return hasSelection();
}

bool NodeEditorQuickController::canExportNode() const
{
    return hasSelection();
}

bool NodeEditorQuickController::hasSelection() const
{
    return !m_selectedNodeLabel.isEmpty();
}

QString NodeEditorQuickController::selectedNodeName() const
{
    return hasSelection() ? m_selectedNodeLabel : QStringLiteral("No Node Selected");
}

QString NodeEditorQuickController::nodeContainerText() const
{
    if (m_nodeContainerPath.isEmpty())
    {
        return hasSelection()
            ? QStringLiteral("Container: not saved yet")
            : QStringLiteral("Container: none");
    }

    return QStringLiteral("Container: %1").arg(QFileInfo(m_nodeContainerPath).fileName());
}

bool NodeEditorQuickController::hasAttachedAudio() const
{
    return m_clipState.has_value() && m_clipState->hasAttachedAudio;
}

int NodeEditorQuickController::nodeTrackCount() const
{
    return m_nodeTracks.size();
}

QVariantList NodeEditorQuickController::nodeTracks() const
{
    return m_nodeTracks;
}

QString NodeEditorQuickController::selectedLaneId() const
{
    return m_selectedLaneId;
}

QString NodeEditorQuickController::selectedLaneHeaderId() const
{
    return m_selectedLaneHeaderId;
}

QString NodeEditorQuickController::selectedClipId() const
{
    return m_selectedClipId;
}

QString NodeEditorQuickController::audioSummaryText() const
{
    if (!hasSelection())
    {
        return canOpenNode()
            ? QStringLiteral("Open a saved node or select a node to edit its audio.")
            : QStringLiteral("Open a project and load a video to work with nodes.");
    }

    if (!hasAttachedAudio())
    {
        return nodeTrackCount() > 0
            ? QStringLiteral("%1 lane(s), no previewable audio").arg(nodeTrackCount())
            : QStringLiteral("No audio lanes");
    }

    return nodeTrackCount() > 0
        ? QStringLiteral("%1 lane(s)").arg(nodeTrackCount())
        : QFileInfo(m_clipState->assetPath).fileName();
}

QString NodeEditorQuickController::emptyBodyText() const
{
    return hasSelection()
        ? QStringLiteral("Import audio to this node and each file will become a lane audio clip.")
        : (canOpenNode()
            ? QStringLiteral("Open a saved node or select a node to work with it here.")
            : QStringLiteral("Open a project and load a video to use the Node Editor."));
}

bool NodeEditorQuickController::showTimeline() const
{
    return hasSelection() && m_clipState.has_value();
}

QString NodeEditorQuickController::timelineStartText() const
{
    if (!showTimeline())
    {
        return QStringLiteral("--");
    }

    return formatTimeMs(frameToMs(m_clipState->nodeStartFrame, m_timelineFps));
}

QString NodeEditorQuickController::timelineEndText() const
{
    if (!showTimeline())
    {
        return QStringLiteral("--");
    }

    return formatTimeMs(frameToMs(m_clipState->nodeEndFrame + 1, m_timelineFps));
}

QString NodeEditorQuickController::timelineDurationText() const
{
    if (!showTimeline())
    {
        return QStringLiteral("--");
    }

    return QStringLiteral("Node  %1").arg(formatTimeMs(nodeDurationMs()));
}

QString NodeEditorQuickController::playheadText() const
{
    return showTimeline() ? formatTimeMs(m_playheadMs) : QStringLiteral("--");
}

int NodeEditorQuickController::nodeDurationMs() const
{
    return nodeDurationMsFromState(m_clipState, m_timelineFps);
}

int NodeEditorQuickController::playheadMs() const
{
    return m_playheadMs;
}

qreal NodeEditorQuickController::playheadRatio() const
{
    const auto durationMs = nodeDurationMs();
    if (durationMs <= 0)
    {
        return 0.0;
    }

    return std::clamp(
        static_cast<qreal>(m_playheadMs) / static_cast<qreal>(durationMs),
        static_cast<qreal>(0.0),
        static_cast<qreal>(1.0));
}

int NodeEditorQuickController::insertionMarkerMs() const
{
    return m_insertionMarkerMs;
}

qreal NodeEditorQuickController::insertionMarkerRatio() const
{
    const auto durationMs = nodeDurationMs();
    if (durationMs <= 0)
    {
        return 0.0;
    }

    return std::clamp(
        static_cast<qreal>(m_insertionMarkerMs) / static_cast<qreal>(durationMs),
        static_cast<qreal>(0.0),
        static_cast<qreal>(1.0));
}

bool NodeEditorQuickController::insertionMarkerStationary() const
{
    return !m_playbackActive || !m_insertionFollowsPlayback;
}

bool NodeEditorQuickController::insertionFollowsPlayback() const
{
    return m_insertionFollowsPlayback;
}

bool NodeEditorQuickController::playbackActive() const
{
    return m_playbackActive;
}

int NodeEditorQuickController::laneMeterToken() const
{
    return m_laneMeterToken;
}

int NodeEditorQuickController::meterResetToken() const
{
    return m_meterResetToken;
}

qreal NodeEditorQuickController::waveformWidthRatio() const
{
    if (!showTimeline() || !hasAttachedAudio())
    {
        return 0.0;
    }

    const auto durationMs = std::max(1, nodeDurationMs());
    const auto clipDurationMs = std::max(1, m_clipState->clipEndMs - m_clipState->clipStartMs);
    if (m_clipState->loopEnabled)
    {
        return 1.0;
    }

    return std::clamp(
        static_cast<qreal>(clipDurationMs) / static_cast<qreal>(durationMs),
        static_cast<qreal>(0.0),
        static_cast<qreal>(1.0));
}

qreal NodeEditorQuickController::waveformOffsetRatio() const
{
    return 0.0;
}

void NodeEditorQuickController::triggerFileAction(const QString& actionKey)
{
    QTimer::singleShot(0, this, [this, actionKey]()
    {
        emit fileActionRequested(actionKey);
    });
}

void NodeEditorQuickController::triggerAudioAction(const QString& actionKey)
{
    QTimer::singleShot(0, this, [this, actionKey]()
    {
        emit audioActionRequested(actionKey);
    });
}

void NodeEditorQuickController::selectLane(const QString& laneId)
{
    if (m_selectedLaneId == laneId && m_selectedClipId.isEmpty() && m_selectedLaneHeaderId.isEmpty())
    {
        return;
    }

    m_selectedLaneId = laneId;
    m_selectedLaneHeaderId.clear();
    m_selectedClipId.clear();
    emit stateChanged();
}

void NodeEditorQuickController::selectLaneHeader(const QString& laneId)
{
    if (laneId.isEmpty())
    {
        return;
    }

    if (m_selectedLaneId == laneId && m_selectedLaneHeaderId == laneId && m_selectedClipId.isEmpty())
    {
        return;
    }

    m_selectedLaneId = laneId;
    m_selectedLaneHeaderId = laneId;
    m_selectedClipId.clear();
    emit stateChanged();
}

void NodeEditorQuickController::selectClip(const QString& laneId, const QString& clipId)
{
    if (clipId.isEmpty())
    {
        return;
    }

    if (m_selectedLaneId == laneId && m_selectedClipId == clipId)
    {
        return;
    }

    m_selectedLaneId = laneId;
    m_selectedLaneHeaderId.clear();
    m_selectedClipId = clipId;
    emit stateChanged();
}

void NodeEditorQuickController::moveClipToRatio(
    const QString& laneId,
    const QString& clipId,
    const qreal offsetRatio)
{
    if (laneId.isEmpty() || clipId.isEmpty())
    {
        return;
    }

    selectClip(laneId, clipId);
    const auto durationMs = nodeDurationMs();
    if (durationMs <= 0)
    {
        return;
    }

    const auto laneOffsetMs = std::clamp(
        static_cast<int>(std::lround(std::clamp(offsetRatio, 0.0, 1.0) * static_cast<qreal>(durationMs))),
        0,
        durationMs);
    emit clipMoveRequested(laneId, clipId, laneOffsetMs);
}

void NodeEditorQuickController::trimClipToRatio(
    const QString& laneId,
    const QString& clipId,
    const bool trimStart,
    const qreal targetRatio)
{
    if (laneId.isEmpty() || clipId.isEmpty())
    {
        return;
    }

    selectClip(laneId, clipId);
    const auto durationMs = nodeDurationMs();
    if (durationMs <= 0)
    {
        return;
    }

    const auto targetMs = std::clamp(
        static_cast<int>(std::lround(std::clamp(targetRatio, 0.0, 1.0) * static_cast<qreal>(durationMs))),
        0,
        durationMs);
    emit clipTrimRequested(laneId, clipId, targetMs, trimStart);
}

void NodeEditorQuickController::setLaneMuted(const QString& laneId, const bool muted)
{
    if (laneId.isEmpty())
    {
        return;
    }

    emit laneMuteRequested(laneId, muted);
}

void NodeEditorQuickController::setLaneSoloed(const QString& laneId, const bool soloed)
{
    if (laneId.isEmpty())
    {
        return;
    }

    emit laneSoloRequested(laneId, soloed);
}

qreal NodeEditorQuickController::laneMeterLevel(const QString& laneId) const
{
    const auto stateIt = m_laneMeterStates.constFind(laneId);
    return stateIt == m_laneMeterStates.cend()
        ? 0.0
        : static_cast<qreal>(stateIt->meterLevel);
}

qreal NodeEditorQuickController::laneMeterLeftLevel(const QString& laneId) const
{
    const auto stateIt = m_laneMeterStates.constFind(laneId);
    return stateIt == m_laneMeterStates.cend()
        ? 0.0
        : static_cast<qreal>(stateIt->meterLeftLevel);
}

qreal NodeEditorQuickController::laneMeterRightLevel(const QString& laneId) const
{
    const auto stateIt = m_laneMeterStates.constFind(laneId);
    return stateIt == m_laneMeterStates.cend()
        ? 0.0
        : static_cast<qreal>(stateIt->meterRightLevel);
}

bool NodeEditorQuickController::laneUsesStereoMeter(const QString& laneId) const
{
    const auto stateIt = m_laneMeterStates.constFind(laneId);
    return stateIt != m_laneMeterStates.cend() && stateIt->useStereoMeter;
}

void NodeEditorQuickController::setPlayheadFromRatio(const qreal ratio)
{
    const auto durationMs = nodeDurationMs();
    if (durationMs <= 0)
    {
        return;
    }

    const auto nextPlayheadMs = std::clamp(
        static_cast<int>(std::lround(std::clamp(ratio, 0.0, 1.0) * static_cast<qreal>(durationMs))),
        0,
        durationMs);
    if (m_playbackActive && !m_insertionFollowsPlayback)
    {
        setInsertionMarkerMs(nextPlayheadMs);
    }
    setPlayheadMs(nextPlayheadMs);
}

void NodeEditorQuickController::setPlayheadMs(const int playheadMs)
{
    const auto nextPlayheadMs = std::clamp(playheadMs, 0, std::max(0, nodeDurationMs()));
    if (m_playheadMs == nextPlayheadMs)
    {
        return;
    }

    m_playheadMs = nextPlayheadMs;
    if (!m_playbackActive || m_insertionFollowsPlayback)
    {
        m_insertionMarkerMs = nextPlayheadMs;
    }
    emit playheadChanged(m_playheadMs);
    emit playheadPositionChanged();
}

void NodeEditorQuickController::setInsertionMarkerMs(const int markerMs)
{
    const auto nextMarkerMs = std::clamp(markerMs, 0, std::max(0, nodeDurationMs()));
    if (m_insertionMarkerMs == nextMarkerMs)
    {
        return;
    }

    m_insertionMarkerMs = nextMarkerMs;
    emit playheadPositionChanged();
}

void NodeEditorQuickController::setInsertionFollowsPlayback(const bool enabled)
{
    if (m_insertionFollowsPlayback == enabled)
    {
        return;
    }

    m_insertionFollowsPlayback = enabled;
    if (m_insertionFollowsPlayback)
    {
        m_insertionMarkerMs = m_playheadMs;
    }
    emit playheadPositionChanged();
    emit playbackStateChanged();
    emit stateChanged();
}

void NodeEditorQuickController::setPlaybackActive(const bool active)
{
    if (m_playbackActive == active)
    {
        return;
    }

    m_playbackActive = active;
    if (active)
    {
        ++m_meterResetToken;
        emit meterResetTokenChanged();
    }
    emit playbackStateChanged();
}

void NodeEditorQuickController::setLaneMeterStates(const QVariantList& meterStates)
{
    bool changed = false;
    for (auto stateIt = m_laneMeterStates.begin(); stateIt != m_laneMeterStates.end(); ++stateIt)
    {
        if (std::abs(stateIt->meterLevel) > 0.0001F
            || std::abs(stateIt->meterLeftLevel) > 0.0001F
            || std::abs(stateIt->meterRightLevel) > 0.0001F)
        {
            stateIt->meterLevel = 0.0F;
            stateIt->meterLeftLevel = 0.0F;
            stateIt->meterRightLevel = 0.0F;
            changed = true;
        }
    }

    for (const auto& meterStateValue : meterStates)
    {
        const auto meterStateMap = meterStateValue.toMap();
        const auto laneId = meterStateMap.value(QStringLiteral("laneId")).toString();
        if (laneId.isEmpty())
        {
            continue;
        }

        auto& state = m_laneMeterStates[laneId];
        const auto nextLeftLevel = std::clamp(
            meterStateMap.value(QStringLiteral("meterLeftLevel")).toFloat(),
            0.0F,
            1.0F);
        const auto nextRightLevel = std::clamp(
            meterStateMap.value(QStringLiteral("meterRightLevel")).toFloat(),
            0.0F,
            1.0F);
        const auto nextMeterLevel = std::clamp(
            meterStateMap.value(QStringLiteral("meterLevel")).toFloat(),
            0.0F,
            1.0F);
        const auto nextUseStereoMeter =
            state.useStereoMeter || meterStateMap.value(QStringLiteral("useStereoMeter")).toBool();
        if (std::abs(state.meterLeftLevel - nextLeftLevel) > 0.0001F
            || std::abs(state.meterRightLevel - nextRightLevel) > 0.0001F
            || std::abs(state.meterLevel - nextMeterLevel) > 0.0001F
            || state.useStereoMeter != nextUseStereoMeter)
        {
            state.meterLeftLevel = nextLeftLevel;
            state.meterRightLevel = nextRightLevel;
            state.meterLevel = nextMeterLevel;
            state.useStereoMeter = nextUseStereoMeter;
            changed = true;
        }
    }

    if (!changed)
    {
        return;
    }

    ++m_laneMeterToken;
    emit laneMeterLevelsChanged();
}

bool NodeEditorQuickController::syncLaneMeterTopology(const QVariantList& nodeTracks)
{
    QHash<QString, LaneMeterState> nextStates;
    for (const auto& laneValue : nodeTracks)
    {
        const auto laneMap = laneValue.toMap();
        const auto laneId = laneMap.value(QStringLiteral("laneId")).toString();
        if (laneId.isEmpty())
        {
            continue;
        }

        auto nextState = m_laneMeterStates.value(laneId);
        nextState.useStereoMeter = laneMap.value(QStringLiteral("useStereoMeter")).toBool();
        nextStates.insert(laneId, nextState);
    }

    if (nextStates.size() != m_laneMeterStates.size())
    {
        m_laneMeterStates = std::move(nextStates);
        return true;
    }

    for (auto stateIt = nextStates.cbegin(); stateIt != nextStates.cend(); ++stateIt)
    {
        const auto previousIt = m_laneMeterStates.constFind(stateIt.key());
        if (previousIt == m_laneMeterStates.cend()
            || previousIt->useStereoMeter != stateIt->useStereoMeter)
        {
            m_laneMeterStates = std::move(nextStates);
            return true;
        }
    }

    m_laneMeterStates = std::move(nextStates);
    return false;
}
