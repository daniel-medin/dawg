#pragma once

#include <optional>

#include <QObject>
#include <QString>

#include "ui/ClipEditorTypes.h"

class NodeEditorQuickController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canOpenNode READ canOpenNode NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(QString selectedNodeName READ selectedNodeName NOTIFY stateChanged)
    Q_PROPERTY(QString nodeContainerText READ nodeContainerText NOTIFY stateChanged)
    Q_PROPERTY(bool hasAttachedAudio READ hasAttachedAudio NOTIFY stateChanged)
    Q_PROPERTY(QString audioSummaryText READ audioSummaryText NOTIFY stateChanged)
    Q_PROPERTY(QString emptyBodyText READ emptyBodyText NOTIFY stateChanged)

public:
    explicit NodeEditorQuickController(QObject* parent = nullptr);

    void setState(bool canOpenNode, const QString& label, const QString& nodeContainerPath, const std::optional<ClipEditorState>& clipState);

    [[nodiscard]] bool canOpenNode() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectedNodeName() const;
    [[nodiscard]] QString nodeContainerText() const;
    [[nodiscard]] bool hasAttachedAudio() const;
    [[nodiscard]] QString audioSummaryText() const;
    [[nodiscard]] QString emptyBodyText() const;

    Q_INVOKABLE void triggerFileAction(const QString& actionKey);
    Q_INVOKABLE void triggerAudioAction(const QString& actionKey);

signals:
    void stateChanged();
    void fileActionRequested(const QString& actionKey);
    void audioActionRequested(const QString& actionKey);

private:
    bool m_canOpenNode = false;
    QString m_selectedNodeLabel;
    QString m_nodeContainerPath;
    std::optional<ClipEditorState> m_clipState;
};
