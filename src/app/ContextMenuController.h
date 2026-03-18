#pragma once

#include <QObject>
#include <QVariantList>

class ContextMenuController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(int menuX READ menuX NOTIFY changed)
    Q_PROPERTY(int menuY READ menuY NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    explicit ContextMenuController(QObject* parent = nullptr);

    [[nodiscard]] bool visible() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] int menuX() const;
    [[nodiscard]] int menuY() const;
    [[nodiscard]] QVariantList items() const;

    void showMenu(const QString& title, int menuX, int menuY, const QVariantList& items);
    void hide();

    Q_INVOKABLE void triggerItem(const QString& key);
    Q_INVOKABLE void dismiss();

signals:
    void changed();
    void itemTriggered(const QString& key);

private:
    bool m_visible = false;
    QString m_title;
    int m_menuX = 0;
    int m_menuY = 0;
    QVariantList m_items;
};
