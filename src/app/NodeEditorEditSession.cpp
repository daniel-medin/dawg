#include "app/NodeEditorEditSession.h"

#include "app/PlayerController.h"
#include "ui/NodeEditorQuickController.h"

NodeEditorEditSession::NodeEditorEditSession(
    PlayerController& controller,
    NodeEditorQuickController& nodeEditorQuickController)
    : m_controller(controller)
    , m_nodeEditorQuickController(nodeEditorQuickController)
{
}

void NodeEditorEditSession::setStatusCallback(std::function<void(const QString&)> statusCallback)
{
    m_statusCallback = std::move(statusCallback);
}

NodeEditorEditSession::Outcome NodeEditorEditSession::handleEditAction(const QString& actionKey)
{
    Outcome outcome;
    if (!m_controller.hasSelection())
    {
        return outcome;
    }

    if (actionKey == QStringLiteral("copyClip"))
    {
        static_cast<void>(m_controller.copySelectedNodeClip(
            m_nodeEditorQuickController.selectedLaneId(),
            m_nodeEditorQuickController.selectedClipId()));
        outcome.updatePasteAvailability = true;
        return outcome;
    }

    if (actionKey == QStringLiteral("cutClip"))
    {
        QString selectedLaneId;
        if (m_controller.cutSelectedNodeClip(
                m_nodeEditorQuickController.selectedLaneId(),
                m_nodeEditorQuickController.selectedClipId(),
                &selectedLaneId))
        {
            outcome.documentChanged = true;
            outcome.forcePreviewSync = true;
            outcome.updatePasteAvailability = true;
            outcome.selectedLaneId = selectedLaneId;
        }
        else
        {
            outcome.updatePasteAvailability = true;
        }
        return outcome;
    }

    if (actionKey == QStringLiteral("pasteClip"))
    {
        QString pastedLaneId;
        QString pastedClipId;
        if (m_controller.pasteSelectedNodeClip(
                m_nodeEditorQuickController.selectedLaneId(),
                m_nodeEditorQuickController.playheadMs(),
                &pastedLaneId,
                &pastedClipId))
        {
            outcome.documentChanged = true;
            outcome.forcePreviewSync = true;
            outcome.updatePasteAvailability = true;
            outcome.selectedLaneId = pastedLaneId;
            outcome.selectedClipId = pastedClipId;
        }
        else
        {
            outcome.updatePasteAvailability = true;
        }
    }

    if (actionKey == QStringLiteral("splitClip"))
    {
        return splitSelectedClipAtPlayhead();
    }

    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::splitSelectedClipAtPlayhead()
{
    Outcome outcome;
    const auto laneId = m_nodeEditorQuickController.selectedLaneId();
    const auto clipId = m_nodeEditorQuickController.selectedClipId();
    if (laneId.isEmpty() || clipId.isEmpty())
    {
        showStatus(QStringLiteral("Select an audio clip before cutting it at the marker."));
        return outcome;
    }

    QString selectedLaneId;
    QString selectedClipId;
    if (!m_controller.splitNodeClipAtPlayhead(
            laneId,
            clipId,
            m_nodeEditorQuickController.playheadMs(),
            &selectedLaneId,
            &selectedClipId))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = selectedLaneId;
    outcome.selectedClipId = selectedClipId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::deleteSelection(
    const std::function<bool(int clipCount)>& confirmDeletePopulatedLane)
{
    Outcome outcome;
    if (!m_controller.hasSelection())
    {
        return outcome;
    }

    const auto selectedLaneId = m_nodeEditorQuickController.selectedLaneId();
    const auto selectedLaneHeaderId = m_nodeEditorQuickController.selectedLaneHeaderId();
    const auto selectedClipId = m_nodeEditorQuickController.selectedClipId();
    if (selectedClipId.isEmpty() && selectedLaneHeaderId.isEmpty())
    {
        showStatus(QStringLiteral("Select a lane name or double-click an audio clip before deleting."));
        return outcome;
    }

    if (selectedClipId.isEmpty())
    {
        const auto clipCount = m_controller.nodeLaneClipCount(selectedLaneHeaderId).value_or(0);
        if (clipCount > 0 && confirmDeletePopulatedLane && !confirmDeletePopulatedLane(clipCount))
        {
            showStatus(QStringLiteral("Lane delete canceled."));
            return outcome;
        }
    }

    QString nextSelectedLaneId;
    if (!m_controller.deleteSelectedNodeClipOrLane(
            selectedLaneId,
            selectedLaneHeaderId,
            selectedClipId,
            true,
            &nextSelectedLaneId))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = nextSelectedLaneId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::trimSelectedClipToPlayhead(const bool trimStart)
{
    Outcome outcome;
    auto laneId = m_nodeEditorQuickController.selectedLaneId();
    auto clipId = m_nodeEditorQuickController.selectedClipId();
    if (clipId.isEmpty())
    {
        QString resolvedLaneId;
        QString resolvedClipId;
        if (m_controller.resolveNodeClipAtPlayhead(
                laneId,
                m_nodeEditorQuickController.playheadMs(),
                &resolvedLaneId,
                &resolvedClipId))
        {
            laneId = resolvedLaneId;
            clipId = resolvedClipId;
        }
    }

    if (laneId.isEmpty() || clipId.isEmpty())
    {
        showStatus(QStringLiteral("Move the marker inside an audio clip before trimming it."));
        return outcome;
    }

    if (!m_controller.trimNodeClip(
            laneId,
            clipId,
            m_nodeEditorQuickController.playheadMs(),
            trimStart,
            m_nodeEditorQuickController.nodeDurationMs()))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = laneId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::setLaneMuted(const QString& laneId, const bool muted)
{
    Outcome outcome;
    if (!m_controller.setNodeLaneMuted(laneId, muted))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.refreshMix = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::setLaneSoloed(const QString& laneId, const bool soloed)
{
    Outcome outcome;
    if (!m_controller.setNodeLaneSoloed(laneId, soloed))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.refreshMix = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::moveClip(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs)
{
    Outcome outcome;
    if (!m_controller.moveNodeClip(
            laneId,
            clipId,
            laneOffsetMs,
            m_nodeEditorQuickController.nodeDurationMs()))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = laneId;
    outcome.selectedClipId = clipId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::copyClipAtOffset(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs)
{
    Outcome outcome;
    QString droppedLaneId;
    QString droppedClipId;
    if (!m_controller.dropSelectedNodeClip(
            laneId,
            clipId,
            laneId,
            laneOffsetMs,
            true,
            &droppedLaneId,
            &droppedClipId))
    {
        outcome.updatePasteAvailability = true;
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = droppedLaneId;
    outcome.selectedClipId = droppedClipId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::dropClip(
    const QString& sourceLaneId,
    const QString& clipId,
    const QString& targetLaneId,
    const int laneOffsetMs,
    const bool copyClip)
{
    Outcome outcome;
    QString droppedLaneId;
    QString droppedClipId;
    if (!m_controller.dropSelectedNodeClip(
            sourceLaneId,
            clipId,
            targetLaneId,
            laneOffsetMs,
            copyClip,
            &droppedLaneId,
            &droppedClipId))
    {
        outcome.updatePasteAvailability = true;
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = droppedLaneId;
    outcome.selectedClipId = droppedClipId;
    return outcome;
}

NodeEditorEditSession::Outcome NodeEditorEditSession::trimClip(
    const QString& laneId,
    const QString& clipId,
    const int targetMs,
    const bool trimStart)
{
    Outcome outcome;
    if (!m_controller.trimNodeClip(
            laneId,
            clipId,
            targetMs,
            trimStart,
            m_nodeEditorQuickController.nodeDurationMs()))
    {
        return outcome;
    }

    outcome.documentChanged = true;
    outcome.forcePreviewSync = true;
    outcome.updatePasteAvailability = true;
    outcome.selectedLaneId = laneId;
    outcome.selectedClipId = clipId;
    return outcome;
}

void NodeEditorEditSession::showStatus(const QString& message) const
{
    if (m_statusCallback)
    {
        m_statusCallback(message);
    }
}
