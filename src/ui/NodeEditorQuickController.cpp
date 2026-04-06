#include "ui/NodeEditorQuickController.h"

#include <cmath>

#include <QFileInfo>

NodeEditorQuickController::NodeEditorQuickController(QObject* parent)
    : QObject(parent)
{
}

void NodeEditorQuickController::setState(
    const bool canOpenNode,
    const QString& label,
    const QString& nodeContainerPath,
    const std::optional<ClipEditorState>& clipState)
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
        && sameClipState)
    {
        return;
    }

    m_canOpenNode = canOpenNode;
    m_selectedNodeLabel = label;
    m_nodeContainerPath = nodeContainerPath;
    m_clipState = clipState;
    emit stateChanged();
}

bool NodeEditorQuickController::canOpenNode() const
{
    return m_canOpenNode;
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
        return QStringLiteral("No audio attached");
    }

    return QFileInfo(m_clipState->assetPath).fileName();
}

QString NodeEditorQuickController::emptyBodyText() const
{
    return hasSelection()
        ? QStringLiteral("Import audio to this node and it will appear here with a waveform preview.")
        : (canOpenNode()
            ? QStringLiteral("Open a saved node or select a node to work with it here.")
            : QStringLiteral("Open a project and load a video to use the Node Editor."));
}

void NodeEditorQuickController::triggerFileAction(const QString& actionKey)
{
    emit fileActionRequested(actionKey);
}

void NodeEditorQuickController::triggerAudioAction(const QString& actionKey)
{
    emit audioActionRequested(actionKey);
}
