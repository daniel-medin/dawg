#include "ui/NodeEditorQuickController.h"

NodeEditorQuickController::NodeEditorQuickController(QObject* parent)
    : QObject(parent)
{
}

void NodeEditorQuickController::setSelectedNodeLabel(const QString& label)
{
    if (m_selectedNodeLabel == label)
    {
        return;
    }

    m_selectedNodeLabel = label;
    emit stateChanged();
}

QString NodeEditorQuickController::titleText() const
{
    return QStringLiteral("Node Editor");
}

bool NodeEditorQuickController::hasSelection() const
{
    return !m_selectedNodeLabel.isEmpty();
}

QString NodeEditorQuickController::selectedNodeText() const
{
    return hasSelection()
        ? QStringLiteral("Selected Node  %1").arg(m_selectedNodeLabel)
        : QStringLiteral("No node selected");
}

QString NodeEditorQuickController::bodyText() const
{
    return hasSelection()
        ? QStringLiteral("Blank panel for now. Node editing tools will live here.")
        : QStringLiteral("Select a node to open the Node Editor.");
}
