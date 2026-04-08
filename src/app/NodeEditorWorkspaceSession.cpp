#include "app/NodeEditorWorkspaceSession.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QQuickItem>

#include "app/ActionRegistry.h"
#include "app/DialogController.h"
#include "app/FilePickerController.h"
#include "app/NodeDocument.h"
#include "app/NodeEditorDocumentUtils.h"
#include "app/NodeEditorEditSession.h"
#include "app/NodeEditorPreviewSession.h"
#include "app/PlayerController.h"
#include "ui/ClipWaveformQuickItem.h"
#include "ui/NodeEditorQuickController.h"

namespace
{

QString selectedNodeDisplayLabel(PlayerController& controller)
{
    const auto selectedTrackId = controller.selectedTrackId();
    if (selectedTrackId.isNull())
    {
        return {};
    }

    const auto label = controller.trackLabel(selectedTrackId).trimmed();
    return label.isEmpty() ? QStringLiteral("Node") : label;
}

void setActionCheckedSilently(QAction* action, const bool checked)
{
    if (!action || action->isChecked() == checked)
    {
        return;
    }

    const QSignalBlocker blocker{action};
    action->setChecked(checked);
}

}

NodeEditorWorkspaceSession::NodeEditorWorkspaceSession(
    PlayerController& controller,
    NodeEditorQuickController& nodeEditorQuickController,
    NodeEditorPreviewSession& previewSession)
    : m_controller(controller)
    , m_nodeEditorQuickController(nodeEditorQuickController)
    , m_previewSession(previewSession)
    , m_editSession(std::make_unique<NodeEditorEditSession>(controller, nodeEditorQuickController))
{
}

void NodeEditorWorkspaceSession::setStatusCallback(std::function<void(const QString&)> statusCallback)
{
    m_statusCallback = std::move(statusCallback);
    if (m_editSession)
    {
        m_editSession->setStatusCallback(m_statusCallback);
    }
}

void NodeEditorWorkspaceSession::setProjectDirtyCallback(std::function<void()> projectDirtyCallback)
{
    m_projectDirtyCallback = std::move(projectDirtyCallback);
}

void NodeEditorWorkspaceSession::setRefreshAudioPoolCallback(std::function<void()> refreshAudioPoolCallback)
{
    m_refreshAudioPoolCallback = std::move(refreshAudioPoolCallback);
}

void NodeEditorWorkspaceSession::setRefreshMixCallback(std::function<void()> refreshMixCallback)
{
    m_refreshMixCallback = std::move(refreshMixCallback);
}

void NodeEditorWorkspaceSession::setChooseOpenFileCallback(
    std::function<QString(const QString&, const QString&, const QString&)> chooseOpenFileCallback)
{
    m_chooseOpenFileCallback = std::move(chooseOpenFileCallback);
}

void NodeEditorWorkspaceSession::setChooseSaveFileCallback(
    std::function<QString(const QString&, const QString&, const QString&, const QString&)> chooseSaveFileCallback)
{
    m_chooseSaveFileCallback = std::move(chooseSaveFileCallback);
}

void NodeEditorWorkspaceSession::setCopyMediaIntoProjectCallback(
    std::function<std::optional<QString>(const QString&, const QString&, QString*)> copyMediaIntoProjectCallback)
{
    m_copyMediaIntoProjectCallback = std::move(copyMediaIntoProjectCallback);
}

void NodeEditorWorkspaceSession::setEnsureProjectForMediaActionCallback(
    std::function<bool(const QString&)> ensureProjectForMediaActionCallback)
{
    m_ensureProjectForMediaActionCallback = std::move(ensureProjectForMediaActionCallback);
}

void NodeEditorWorkspaceSession::setDialogController(DialogController* dialogController)
{
    m_dialogController = dialogController;
}

void NodeEditorWorkspaceSession::setFilePickerController(FilePickerController* filePickerController)
{
    m_filePickerController = filePickerController;
}

void NodeEditorWorkspaceSession::bindShellRootItem(QQuickItem* shellRootItem)
{
    m_shellRootItem = shellRootItem;
    m_nodeEditorWaveformItem = nullptr;
}

void NodeEditorWorkspaceSession::handleFileAction(
    const QString& actionKey,
    const bool hasOpenProject,
    const QString& projectRootPath)
{
    if (!hasOpenProject)
    {
        return;
    }

    const auto nodesDirectoryPath = projectNodesDirectoryPath(projectRootPath);
    if (nodesDirectoryPath.isEmpty())
    {
        return;
    }
    static_cast<void>(QDir().mkpath(nodesDirectoryPath));

    if (actionKey == QStringLiteral("save"))
    {
        if (!m_controller.hasSelection())
        {
            showStatus(QStringLiteral("Select a node before saving it."));
            return;
        }
        const auto selectedTrackId = m_controller.selectedTrackId();
        const auto boundNodeDocumentPath = selectedTrackId.isNull()
            ? QString{}
            : m_controller.trackNodeDocumentPath(selectedTrackId).trimmed();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller.trackLabel(selectedTrackId).trimmed();
        const auto nodeFilePath = !boundNodeDocumentPath.isEmpty()
            ? QDir::cleanPath(boundNodeDocumentPath)
            : QDir(nodesDirectoryPath).filePath(
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel));
        QString errorMessage;
        if (m_controller.saveSelectedNodeToFile(nodeFilePath, true, {}, &errorMessage))
        {
            clearTrackUnsaved(selectedTrackId);
            refresh(hasOpenProject);
        }
        else if (!errorMessage.isEmpty())
        {
            showDialogMessage(QStringLiteral("Save Node"), errorMessage);
        }
        return;
    }

    if (actionKey == QStringLiteral("saveAs"))
    {
        if (!m_controller.hasSelection())
        {
            showStatus(QStringLiteral("Select a node before saving it."));
            return;
        }
        const auto selectedTrackId = m_controller.selectedTrackId();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller.trackLabel(selectedTrackId).trimmed();
        const auto selectedNodeFilePath = m_chooseSaveFileCallback
            ? m_chooseSaveFileCallback(
                QStringLiteral("Save Node As"),
                nodesDirectoryPath,
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel),
                QStringLiteral("DAWG Nodes (*%1)").arg(QString::fromLatin1(dawg::node::kNodeFileSuffix)))
            : QString{};
        if (!selectedNodeFilePath.isEmpty())
        {
            const auto savedNodeLabel = QFileInfo(selectedNodeFilePath).completeBaseName().trimmed();
            QString errorMessage;
            if (m_controller.saveSelectedNodeToFile(
                    selectedNodeFilePath,
                    true,
                    savedNodeLabel.isEmpty() ? QStringLiteral("Node") : savedNodeLabel,
                    &errorMessage))
            {
                if (!selectedTrackId.isNull())
                {
                    clearTrackUnsaved(selectedTrackId);
                }
                refresh(hasOpenProject);
            }
            else if (!errorMessage.isEmpty())
            {
                showDialogMessage(QStringLiteral("Save Node"), errorMessage);
            }
        }
        return;
    }

    if (actionKey == QStringLiteral("open"))
    {
        const auto selectedNodeFilePath = m_chooseOpenFileCallback
            ? m_chooseOpenFileCallback(
                QStringLiteral("Open Node"),
                nodesDirectoryPath,
                QStringLiteral("DAWG Nodes (*%1 *%2);;All Files (*.*)")
                    .arg(
                        QString::fromLatin1(dawg::node::kNodeFileSuffix),
                        QString::fromLatin1(dawg::node::kLegacyNodeFileSuffix)))
            : QString{};
        if (!selectedNodeFilePath.isEmpty())
        {
            QString errorMessage;
            if (m_controller.openNodeFileAsNewNode(selectedNodeFilePath, projectRootPath, &errorMessage))
            {
                const auto openedTrackId = m_controller.selectedTrackId();
                if (!openedTrackId.isNull())
                {
                    clearTrackUnsaved(openedTrackId);
                }
                if (m_projectDirtyCallback)
                {
                    m_projectDirtyCallback();
                }
                refresh(hasOpenProject);
            }
            else if (!errorMessage.isEmpty())
            {
                showDialogMessage(QStringLiteral("Open Node"), errorMessage);
            }
        }
        return;
    }

    if (actionKey == QStringLiteral("export"))
    {
        if (!m_controller.hasSelection())
        {
            showStatus(QStringLiteral("Select a node before exporting it."));
            return;
        }
        const auto selectedTrackId = m_controller.selectedTrackId();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller.trackLabel(selectedTrackId).trimmed();
        const auto selectedNodeFilePath = m_chooseSaveFileCallback
            ? m_chooseSaveFileCallback(
                QStringLiteral("Export Node"),
                nodesDirectoryPath,
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel),
                QStringLiteral("DAWG Nodes (*%1)").arg(QString::fromLatin1(dawg::node::kNodeFileSuffix)))
            : QString{};
        if (!selectedNodeFilePath.isEmpty())
        {
            QString errorMessage;
            if (!m_controller.saveSelectedNodeToFile(selectedNodeFilePath, false, {}, &errorMessage)
                && !errorMessage.isEmpty())
            {
                showDialogMessage(QStringLiteral("Export Node"), errorMessage);
            }
        }
    }
}

void NodeEditorWorkspaceSession::handleAudioAction(
    const QString& actionKey,
    const bool hasOpenProject,
    const QString& projectRootPath)
{
    const auto isImportAction = actionKey == QStringLiteral("import");
    const auto isNewLaneAction = actionKey == QStringLiteral("newLane") || actionKey == QStringLiteral("createTrack");
    if (!isImportAction && !isNewLaneAction)
    {
        return;
    }

    if (!m_ensureProjectForMediaActionCallback
        || !m_ensureProjectForMediaActionCallback(
            isImportAction ? QStringLiteral("import audio") : QStringLiteral("create a node track")))
    {
        return;
    }

    if (!m_controller.hasSelection())
    {
        showStatus(isImportAction
            ? QStringLiteral("Select a node before importing audio.")
            : QStringLiteral("Select a node before creating a track."));
        return;
    }

    QString errorMessage;
    std::optional<QString> copiedFilePath;
    QFileInfo importedAudioInfo;
    if (isImportAction)
    {
        const auto filePath = m_chooseOpenFileCallback
            ? m_chooseOpenFileCallback(
                QStringLiteral("Import Audio"),
                QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"))
            : QString{};
        if (filePath.isEmpty())
        {
            return;
        }

        copiedFilePath = m_copyMediaIntoProjectCallback
            ? m_copyMediaIntoProjectCallback(filePath, QStringLiteral("audio"), &errorMessage)
            : std::nullopt;
        if (!copiedFilePath.has_value())
        {
            showDialogMessage(QStringLiteral("Import Audio"), errorMessage);
            return;
        }

        importedAudioInfo = QFileInfo(*copiedFilePath);
        static_cast<void>(m_controller.importAudioToPool(*copiedFilePath));
    }

    const auto selectedTrackId = m_controller.selectedTrackId();
    if (selectedTrackId.isNull())
    {
        showStatus(isImportAction
            ? QStringLiteral("Select a node before importing audio.")
            : QStringLiteral("Select a node before creating a lane."));
        return;
    }

    const auto nodesDirectoryPath = projectNodesDirectoryPath(projectRootPath);
    if (nodesDirectoryPath.isEmpty() || !QDir().mkpath(nodesDirectoryPath))
    {
        showStatus(QStringLiteral("Failed to create the project nodes folder."));
        return;
    }

    const auto selectedLaneId = m_nodeEditorQuickController.selectedLaneId();
    QString createdLaneId;
    QString createdLaneLabel;
    if (!m_controller.addNodeLaneOrImportClip(
            nodesDirectoryPath,
            copiedFilePath.value_or(QString{}),
            isNewLaneAction ? QString{} : selectedLaneId,
            &createdLaneId,
            &createdLaneLabel))
    {
        showDialogMessage(
            isImportAction ? QStringLiteral("Import Audio") : QStringLiteral("Create Track"),
            QStringLiteral("Failed to update the selected node."));
        return;
    }

    if (m_refreshAudioPoolCallback)
    {
        m_refreshAudioPoolCallback();
    }
    m_nodeEditorQuickController.selectLane(createdLaneId);
    refresh(hasOpenProject);
    if (m_projectDirtyCallback)
    {
        m_projectDirtyCallback();
    }
    if (isImportAction)
    {
        showStatus(QStringLiteral("Imported %1 into lane %2.")
            .arg(importedAudioInfo.fileName(), createdLaneLabel));
    }
    else
    {
        showStatus(QStringLiteral("Created %1.").arg(createdLaneLabel));
    }
}

void NodeEditorWorkspaceSession::handleEditAction(const QString& actionKey, const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->handleEditAction(actionKey), hasOpenProject);
}

void NodeEditorWorkspaceSession::setLaneMuted(const QString& laneId, const bool muted, const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->setLaneMuted(laneId, muted), hasOpenProject);
}

void NodeEditorWorkspaceSession::setLaneSoloed(const QString& laneId, const bool soloed, const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->setLaneSoloed(laneId, soloed), hasOpenProject);
}

void NodeEditorWorkspaceSession::moveClip(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs,
    const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->moveClip(laneId, clipId, laneOffsetMs), hasOpenProject);
}

void NodeEditorWorkspaceSession::copyClipAtOffset(
    const QString& laneId,
    const QString& clipId,
    const int laneOffsetMs,
    const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->copyClipAtOffset(laneId, clipId, laneOffsetMs), hasOpenProject);
}

void NodeEditorWorkspaceSession::dropClip(
    const QString& sourceLaneId,
    const QString& clipId,
    const QString& targetLaneId,
    const int laneOffsetMs,
    const bool copyClip,
    const bool hasOpenProject)
{
    applyEditOutcome(
        m_editSession->dropClip(sourceLaneId, clipId, targetLaneId, laneOffsetMs, copyClip),
        hasOpenProject);
}

void NodeEditorWorkspaceSession::trimClip(
    const QString& laneId,
    const QString& clipId,
    const int targetMs,
    const bool trimStart,
    const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->trimClip(laneId, clipId, targetMs, trimStart), hasOpenProject);
}

void NodeEditorWorkspaceSession::deleteSelection(const bool hasOpenProject)
{
    applyEditOutcome(
        m_editSession->deleteSelection(
            [this](const int clipCount)
            {
                if (!m_dialogController)
                {
                    showStatus(QStringLiteral("This lane contains audio clips."));
                    return false;
                }

                const auto choice = m_dialogController->execMessage(
                    QStringLiteral("Delete Lane"),
                    QStringLiteral("This lane contains %1 audio clip(s).").arg(clipCount),
                    QStringLiteral("Deleting the lane will also remove every audio clip in it."),
                    {DialogController::Button::Yes, DialogController::Button::Cancel},
                    DialogController::Button::Cancel);
                return choice == DialogController::Button::Yes;
            }),
        hasOpenProject);
}

void NodeEditorWorkspaceSession::trimSelectedClipToPlayhead(const bool trimStart, const bool hasOpenProject)
{
    applyEditOutcome(m_editSession->trimSelectedClipToPlayhead(trimStart), hasOpenProject);
}

void NodeEditorWorkspaceSession::resetPlayheadToStart()
{
    m_previewSession.resetPlayheadToStart();
    m_nodeEditorQuickController.setCanPasteClip(m_controller.canPasteNodeClip());
}

void NodeEditorWorkspaceSession::refresh(const bool hasOpenProject)
{
    m_nodeEditorState.reset();
    QVariantList nodeTrackItems;
    const auto selectedTrackId = m_controller.selectedTrackId();
    const auto nodeDocumentPath = m_controller.trackNodeDocumentPath(selectedTrackId);
    const auto selectedNodeLabel = selectedNodeDisplayLabel(m_controller);
    const auto runtimeNodeState = m_controller.selectedAudioClipPreviewState();
    auto nodeTimelineState = runtimeNodeState.value_or(AudioClipPreviewState{});
    nodeTimelineState.trackId = selectedTrackId;
    nodeTimelineState.label = selectedNodeLabel;
    nodeTimelineState.loopEnabled = false;
    if (const auto selectedFrameRange = m_controller.selectedTrackFrameRange(); selectedFrameRange.has_value())
    {
        nodeTimelineState.nodeStartFrame = selectedFrameRange->first;
        nodeTimelineState.nodeEndFrame = selectedFrameRange->second;
    }
    const auto nodeDurationMs = nodeTimelineState.nodeEndFrame >= nodeTimelineState.nodeStartFrame
        ? std::max(
            1,
            static_cast<int>(std::lround(
                (static_cast<double>(nodeTimelineState.nodeEndFrame - nodeTimelineState.nodeStartFrame + 1) * 1000.0)
                / std::max(0.0001, m_controller.fps()))))
        : 1;
    auto hasUnsavedNodeChanges =
        !selectedTrackId.isNull()
        && (nodeDocumentPath.isEmpty() || m_nodeTracksWithUnsavedChanges.contains(selectedTrackId));
    const auto durationForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileDurationMs(filePath);
    };
    const auto channelCountForPath = [this](const QString& filePath) -> std::optional<int>
    {
        return m_controller.audioFileChannelCount(filePath);
    };
    const auto viewState = dawg::nodeeditor::buildNodeEditorViewState(
        nodeDocumentPath,
        selectedNodeLabel,
        nodeTimelineState,
        nodeDurationMs,
        durationForPath,
        channelCountForPath);
    if (!viewState.savedLabelDiffers)
    {
        m_nodeTracksWithUnsavedChanges.remove(selectedTrackId);
    }
    hasUnsavedNodeChanges =
        m_nodeTracksWithUnsavedChanges.contains(selectedTrackId)
        || viewState.savedLabelDiffers;
    nodeTrackItems = viewState.nodeTrackItems;
    m_nodeEditorState = viewState.nodeEditorState;

    if (!m_nodeEditorState.has_value())
    {
        m_nodeEditorState = nodeTimelineState;
    }
    if (m_nodeEditorState.has_value())
    {
        m_nodeEditorState->loopEnabled = false;
    }
    if (nodeTrackItems.isEmpty() && m_nodeEditorState.has_value())
    {
        const auto title = m_nodeEditorState->label.trimmed().isEmpty()
            ? QStringLiteral("Track 1")
            : m_nodeEditorState->label.trimmed();
        const auto subtitle = m_nodeEditorState->hasAttachedAudio
            ? QFileInfo(m_nodeEditorState->assetPath).fileName()
            : QStringLiteral("No audio");
        QVariantMap waveformState;
        if (m_nodeEditorState->hasAttachedAudio && !m_nodeEditorState->assetPath.isEmpty())
        {
            waveformState = QVariantMap{
                {QStringLiteral("label"), title},
                {QStringLiteral("assetPath"), m_nodeEditorState->assetPath},
                {QStringLiteral("clipStartMs"), m_nodeEditorState->clipStartMs},
                {QStringLiteral("clipEndMs"), m_nodeEditorState->clipEndMs},
                {QStringLiteral("sourceDurationMs"), m_nodeEditorState->sourceDurationMs},
                {QStringLiteral("playheadMs"), m_nodeEditorState->playheadMs.value_or(m_nodeEditorState->clipStartMs)},
                {QStringLiteral("gainDb"), m_nodeEditorState->gainDb},
                {QStringLiteral("hasAttachedAudio"), true},
                {QStringLiteral("loopEnabled"), false}
            };
        }
        nodeTrackItems.push_back(QVariantMap{
            {QStringLiteral("laneId"), QStringLiteral("runtime")},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("primary"), true},
            {QStringLiteral("muted"), false},
            {QStringLiteral("soloed"), false},
            {QStringLiteral("hasWaveform"), !waveformState.isEmpty()},
            {QStringLiteral("waveformState"), waveformState}
        });
    }

    m_nodeEditorQuickController.setState(
        hasOpenProject && m_controller.hasVideoLoaded(),
        selectedNodeLabel,
        nodeDocumentPath,
        hasUnsavedNodeChanges,
        m_controller.fps(),
        m_nodeEditorState,
        nodeTrackItems);
    m_nodeEditorQuickController.setCanPasteClip(m_controller.canPasteNodeClip());
    syncWaveformItem();
}

void NodeEditorWorkspaceSession::syncAvailability(QAction* showNodeEditorAction, ActionRegistry* actionRegistry)
{
    if (!showNodeEditorAction)
    {
        return;
    }

    const auto enabled = m_controller.hasSelection();
    const auto previousEnabled = showNodeEditorAction->isEnabled();
    if (previousEnabled != enabled)
    {
        showNodeEditorAction->setEnabled(enabled);
    }

    if (!enabled && showNodeEditorAction->isChecked())
    {
        setActionCheckedSilently(showNodeEditorAction, false);
    }

    if (previousEnabled != enabled && actionRegistry)
    {
        actionRegistry->rebuild();
    }

    m_nodeEditorQuickController.setCanPasteClip(m_controller.canPasteNodeClip());
}

void NodeEditorWorkspaceSession::markTrackUnsaved(const QUuid& trackId)
{
    if (!trackId.isNull())
    {
        m_nodeTracksWithUnsavedChanges.insert(trackId);
    }
}

void NodeEditorWorkspaceSession::clearTrackUnsaved(const QUuid& trackId)
{
    if (!trackId.isNull())
    {
        m_nodeTracksWithUnsavedChanges.remove(trackId);
    }
}

QString NodeEditorWorkspaceSession::projectNodesDirectoryPath(const QString& projectRootPath) const
{
    return projectRootPath.isEmpty()
        ? QString{}
        : QDir(projectRootPath).filePath(QStringLiteral("nodes"));
}

void NodeEditorWorkspaceSession::applyEditOutcome(
    const NodeEditorEditSession::Outcome& outcome,
    const bool hasOpenProject)
{
    if (outcome.documentChanged && m_projectDirtyCallback)
    {
        m_projectDirtyCallback();
    }
    if (outcome.refreshMix && m_refreshMixCallback)
    {
        m_refreshMixCallback();
    }
    if (outcome.documentChanged || outcome.forcePreviewSync)
    {
        m_previewSession.syncFromBoundDocument(outcome.forcePreviewSync);
    }
    if (outcome.documentChanged && !outcome.selectedLaneId.isEmpty() && !outcome.selectedClipId.isEmpty())
    {
        m_nodeEditorQuickController.selectClip(outcome.selectedLaneId, outcome.selectedClipId);
    }
    else if (outcome.documentChanged && !outcome.selectedLaneId.isEmpty())
    {
        m_nodeEditorQuickController.selectLane(outcome.selectedLaneId);
    }

    if (outcome.documentChanged)
    {
        refresh(hasOpenProject);
    }
    else if (outcome.updatePasteAvailability)
    {
        m_nodeEditorQuickController.setCanPasteClip(m_controller.canPasteNodeClip());
    }
}

void NodeEditorWorkspaceSession::syncWaveformItem()
{
    if (!m_shellRootItem)
    {
        return;
    }

    if (!m_nodeEditorWaveformItem)
    {
        m_nodeEditorWaveformItem = m_shellRootItem->findChild<ClipWaveformQuickItem*>(QStringLiteral("nodeEditorWaveform"));
        if (m_nodeEditorWaveformItem)
        {
            QObject::connect(
                m_nodeEditorWaveformItem,
                &ClipWaveformQuickItem::clipRangeChanged,
                &m_controller,
                &PlayerController::setSelectedTrackClipRangeMs);
            QObject::connect(
                m_nodeEditorWaveformItem,
                &ClipWaveformQuickItem::playheadChanged,
                &m_controller,
                [this](const int playheadMs)
                {
                    if (m_controller.setSelectedTrackClipPlayheadMs(playheadMs))
                    {
                        if (m_projectDirtyCallback)
                        {
                            m_projectDirtyCallback();
                        }
                        refresh(true);
                    }
                });
        }
    }

    if (m_nodeEditorWaveformItem)
    {
        m_nodeEditorWaveformItem->setState(m_nodeEditorState);
    }
}

void NodeEditorWorkspaceSession::showDialogMessage(const QString& title, const QString& message) const
{
    if (message.isEmpty())
    {
        return;
    }

    if (m_dialogController)
    {
        static_cast<void>(m_dialogController->execMessage(
            title,
            message,
            {},
            {DialogController::Button::Ok}));
        return;
    }

    showStatus(message);
}

void NodeEditorWorkspaceSession::showStatus(const QString& message) const
{
    if (m_statusCallback)
    {
        m_statusCallback(message);
    }
}
