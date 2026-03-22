#pragma once

#include <functional>

#include <QAction>
#include <QObject>

class MainWindow;

class ActionEntry final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool checkable READ checkable NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY changed)
    Q_PROPERTY(bool separator READ separator CONSTANT)
    Q_PROPERTY(bool hasSubmenu READ hasSubmenu CONSTANT)
    Q_PROPERTY(QObjectList subItems READ subItems CONSTANT)

public:
    explicit ActionEntry(const QString& id, QAction* action, QObject* parent = nullptr);
    ActionEntry(const QString& id, const QString& text, QObject* parent = nullptr);
    ActionEntry(
        const QString& id,
        const QString& text,
        const std::function<void()>& trigger,
        bool enabled,
        QObject* parent = nullptr);
    explicit ActionEntry(QObject* parent = nullptr);

    [[nodiscard]] QString id() const;
    [[nodiscard]] QString text() const;
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool checkable() const;
    [[nodiscard]] bool checked() const;
    [[nodiscard]] QString shortcut() const;
    [[nodiscard]] bool separator() const;
    [[nodiscard]] bool hasSubmenu() const;
    [[nodiscard]] QObjectList subItems() const;
    void addSubEntry(ActionEntry* entry);

    Q_INVOKABLE void trigger();

signals:
    void changed();

private:
    QString m_id;
    QAction* m_action = nullptr;
    QString m_text;
    bool m_enabled = false;
    std::function<void()> m_trigger;
    bool m_separator = false;
    QObjectList m_subItems;
};

class ActionMenu final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QObjectList items READ items CONSTANT)

public:
    explicit ActionMenu(const QString& title, QObject* parent = nullptr);

    void addEntry(ActionEntry* entry);
    [[nodiscard]] QString title() const;
    [[nodiscard]] QObjectList items() const;

private:
    QString m_title;
    QObjectList m_items;
};

class ActionRegistry final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObjectList menus READ menus NOTIFY menusChanged)

public:
    explicit ActionRegistry(MainWindow& window, QObject* parent = nullptr);

    [[nodiscard]] QObjectList menus() const;
    void rebuild();

signals:
    void menusChanged();

private:
    [[nodiscard]] ActionMenu* addMenu(const QString& title);
    static void addAction(ActionMenu* menu, const QString& id, QAction* action);
    static ActionEntry* addSubmenu(ActionMenu* menu, const QString& id, const QString& text);
    static void addCallback(
        ActionMenu* menu,
        const QString& id,
        const QString& text,
        const std::function<void()>& trigger,
        bool enabled = true);
    static void addSeparator(ActionMenu* menu);

    MainWindow& m_window;
    QObjectList m_menus;
};
