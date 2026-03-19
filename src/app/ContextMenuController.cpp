#include "app/ContextMenuController.h"

#include <QTimer>

ContextMenuController::ContextMenuController(QObject* parent)
    : QObject(parent)
{
}

bool ContextMenuController::visible() const
{
    return m_visible;
}

QString ContextMenuController::title() const
{
    return m_title;
}

int ContextMenuController::menuX() const
{
    return m_menuX;
}

int ContextMenuController::menuY() const
{
    return m_menuY;
}

QVariantList ContextMenuController::items() const
{
    return m_items;
}

void ContextMenuController::showMenu(const QString& title, const int menuX, const int menuY, const QVariantList& items)
{
    m_visible = true;
    m_title = title;
    m_menuX = menuX;
    m_menuY = menuY;
    m_items = items;
    emit changed();
}

void ContextMenuController::hide()
{
    if (!m_visible && m_title.isEmpty() && m_items.isEmpty())
    {
        return;
    }

    m_visible = false;
    m_title.clear();
    m_items.clear();
    emit changed();
}

void ContextMenuController::triggerItem(const QString& key)
{
    hide();
    QTimer::singleShot(0, this, [this, key]()
    {
        emit itemTriggered(key);
    });
}

void ContextMenuController::dismiss()
{
    hide();
}
