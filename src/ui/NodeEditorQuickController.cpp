#include "ui/NodeEditorQuickController.h"

#include <cmath>

#include <QFileInfo>
#include <QTimer>

NodeEditorQuickController::NodeEditorQuickController(QObject* parent)
    : QObject(parent)
{
}

void NodeEditorQuickController::setState(
    const bool canOpenNode,
    const QString& label,
    const QString& nodeContainerPath,
    const bool hasUnsavedChanges,
    const std::optional<ClipEditorState>& clipState,
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
            && m_clipState->clipStartMs == clipState->clipStartMs
            && m_clipState->clipEndMs == clipState->clipEndMs
            && std::abs(m_clipState->gainDb - clipState->gainDb) < 0.001F
            && m_clipState->loopEnabled == clipState->loopEnabled;
    }();
    if (m_canOpenNode == canOpenNode
        && m_selectedNodeLabel == label
        && m_nodeContainerPath == nodeContainerPath
        && m_hasUnsavedChanges == hasUnsavedChanges
        && sameClipState
        && m_nodeTracks == nodeTracks)
    {
        return;
    }

    m_canOpenNode = canOpenNode;
    m_selectedNodeLabel = label;
    m_nodeContainerPath = nodeContainerPath;
    m_hasUnsavedChanges = hasUnsavedChanges;
    m_clipState = clipState;
    m_nodeTracks = nodeTracks;
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
            ? QStringLiteral("%1 internal track(s), no previewable audio").arg(nodeTrackCount())
            : QStringLiteral("No internal audio tracks");
    }

    return nodeTrackCount() > 0
        ? QStringLiteral("%1 internal track(s)").arg(nodeTrackCount())
        : QFileInfo(m_clipState->assetPath).fileName();
}

QString NodeEditorQuickController::emptyBodyText() const
{
    return hasSelection()
        ? QStringLiteral("Import audio to this node and each file will become its own internal track.")
        : (canOpenNode()
            ? QStringLiteral("Open a saved node or select a node to work with it here.")
            : QStringLiteral("Open a project and load a video to use the Node Editor."));
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
