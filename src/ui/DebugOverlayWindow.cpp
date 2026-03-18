#include "ui/DebugOverlayWindow.h"

#include <QMouseEvent>
#include <QQuickItem>
#include <QUrl>

#include "ui/QuickEngineSupport.h"

namespace
{
    QUrl debugOverlayQmlUrl()
    {
        return QUrl(QStringLiteral("qrc:/qml/DebugOverlayScene.qml"));
    }
}

DebugOverlayWindow::DebugOverlayWindow(QWindow* parent)
    : QQuickView(parent)
{
    setObjectName(QStringLiteral("debugOverlay"));
    setFlags(Qt::Tool | Qt::FramelessWindowHint);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize(QSize(300, 120));
    resize(300, 400);

    configureQuickEngine(*engine());
    setSource(debugOverlayQmlUrl());

    QObject::connect(rootObject(), SIGNAL(closeClicked()), this, SLOT(handleCloseClicked()));
}

void DebugOverlayWindow::setListText(const QString& text)
{
    if (rootObject())
    {
        rootObject()->setProperty("listText", text);
    }
}

void DebugOverlayWindow::handleCloseClicked()
{
    emit closeRequested();
}

void DebugOverlayWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - geometry().topLeft();
        event->accept();
        return;
    }

    QQuickView::mousePressEvent(event);
}

void DebugOverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        setPosition(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QQuickView::mouseMoveEvent(event);
}

void DebugOverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
    }

    QQuickView::mouseReleaseEvent(event);
}
