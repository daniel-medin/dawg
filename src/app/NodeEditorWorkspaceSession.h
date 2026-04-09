#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QSet>
#include <QString>
#include <QUuid>

#include "app/NodeEditorEditSession.h"
#include "ui/AudioClipPreviewTypes.h"

class QQuickItem;
class QAction;
class ActionRegistry;
class ClipWaveformQuickItem;
class DialogController;
class FilePickerController;
class PlayerController;
class NodeEditorPreviewSession;
class NodeEditorQuickController;

class NodeEditorWorkspaceSession
{
public:
    NodeEditorWorkspaceSession(
        PlayerController& controller,
        NodeEditorQuickController& nodeEditorQuickController,
        NodeEditorPreviewSession& previewSession);

    void setStatusCallback(std::function<void(const QString&)> statusCallback);
    void setProjectDirtyCallback(std::function<void()> projectDirtyCallback);
    void setRefreshAudioPoolCallback(std::function<void()> refreshAudioPoolCallback);
    void setRefreshMixCallback(std::function<void()> refreshMixCallback);
    void setChooseOpenFileCallback(std::function<QString(const QString&, const QString&, const QString&)> chooseOpenFileCallback);
    void setChooseSaveFileCallback(
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> chooseSaveFileCallback);
    void setCopyMediaIntoProjectCallback(
        std::function<std::optional<QString>(const QString&, const QString&, QString*)> copyMediaIntoProjectCallback);
    void setEnsureProjectForMediaActionCallback(std::function<bool(const QString&)> ensureProjectForMediaActionCallback);
    void setDialogController(DialogController* dialogController);
    void setFilePickerController(FilePickerController* filePickerController);
    void bindShellRootItem(QQuickItem* shellRootItem);

    void handleFileAction(const QString& actionKey, bool hasOpenProject, const QString& projectRootPath);
    void handleAudioAction(const QString& actionKey, bool hasOpenProject, const QString& projectRootPath);
    void handleEditAction(const QString& actionKey, bool hasOpenProject);
    void setLaneMuted(const QString& laneId, bool muted, bool hasOpenProject);
    void setLaneSoloed(const QString& laneId, bool soloed, bool hasOpenProject);
    void moveClip(const QString& laneId, const QString& clipId, int laneOffsetMs, bool hasOpenProject);
    void copyClipAtOffset(const QString& laneId, const QString& clipId, int laneOffsetMs, bool hasOpenProject);
    void dropClip(
        const QString& sourceLaneId,
        const QString& clipId,
        const QString& targetLaneId,
        int laneOffsetMs,
        bool copyClip,
        bool hasOpenProject);
    void trimClip(const QString& laneId, const QString& clipId, int targetMs, bool trimStart, bool hasOpenProject);
    void deleteSelection(bool hasOpenProject);
    void trimSelectedClipToPlayhead(bool trimStart, bool hasOpenProject);
    void nudgeSelectionOrSelectedClipFrames(int frameDelta, bool hasOpenProject);
    void resetPlayheadToStart();
    void refresh(bool hasOpenProject);
    void syncAvailability(QAction* showNodeEditorAction, ActionRegistry* actionRegistry);
    void markTrackUnsaved(const QUuid& trackId);
    void clearTrackUnsaved(const QUuid& trackId);

private:
    [[nodiscard]] QString projectNodesDirectoryPath(const QString& projectRootPath) const;
    void applyEditOutcome(const NodeEditorEditSession::Outcome& outcome, bool hasOpenProject);
    void syncWaveformItem();
    void showDialogMessage(const QString& title, const QString& message) const;
    void showStatus(const QString& message) const;

    PlayerController& m_controller;
    NodeEditorQuickController& m_nodeEditorQuickController;
    NodeEditorPreviewSession& m_previewSession;
    std::unique_ptr<NodeEditorEditSession> m_editSession;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_projectDirtyCallback;
    std::function<void()> m_refreshAudioPoolCallback;
    std::function<void()> m_refreshMixCallback;
    std::function<QString(const QString&, const QString&, const QString&)> m_chooseOpenFileCallback;
    std::function<QString(const QString&, const QString&, const QString&, const QString&)> m_chooseSaveFileCallback;
    std::function<std::optional<QString>(const QString&, const QString&, QString*)> m_copyMediaIntoProjectCallback;
    std::function<bool(const QString&)> m_ensureProjectForMediaActionCallback;
    DialogController* m_dialogController = nullptr;
    FilePickerController* m_filePickerController = nullptr;
    QQuickItem* m_shellRootItem = nullptr;
    ClipWaveformQuickItem* m_nodeEditorWaveformItem = nullptr;
    QSet<QUuid> m_nodeTracksWithUnsavedChanges;
    std::optional<AudioClipPreviewState> m_nodeEditorState;
};
