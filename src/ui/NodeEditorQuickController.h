#pragma once

#include <QObject>
#include <QString>

class NodeEditorQuickController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString titleText READ titleText CONSTANT)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(QString selectedNodeText READ selectedNodeText NOTIFY stateChanged)
    Q_PROPERTY(QString bodyText READ bodyText NOTIFY stateChanged)

public:
    explicit NodeEditorQuickController(QObject* parent = nullptr);

    void setSelectedNodeLabel(const QString& label);

    [[nodiscard]] QString titleText() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectedNodeText() const;
    [[nodiscard]] QString bodyText() const;

signals:
    void stateChanged();

private:
    QString m_selectedNodeLabel;
};
