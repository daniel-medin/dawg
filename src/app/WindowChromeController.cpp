#include "app/WindowChromeController.h"

#include <algorithm>

#include <QEvent>
#include <QWindow>

#include "app/MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

WindowChromeController::WindowChromeController(MainWindow& window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
    m_window.installEventFilter(this);
    connect(&m_window, &QWindow::windowTitleChanged, this, &WindowChromeController::windowTitleChanged);
}

QString WindowChromeController::windowTitle() const
{
    return m_window.windowTitle();
}

QString WindowChromeController::iconSource() const
{
    return QStringLiteral("qrc:/branding/dawg.png");
}

QString WindowChromeController::frameText() const
{
    return m_frameText;
}

bool WindowChromeController::maximized() const
{
    return m_window.isMaximized();
}

bool WindowChromeController::active() const
{
    return m_window.isActive();
}

int WindowChromeController::titleBarHeight() const
{
    return m_titleBarHeight;
}

void WindowChromeController::setTitleBarHeight(const int height)
{
    const auto clamped = (std::max)(32, height);
    if (m_titleBarHeight == clamped)
    {
        return;
    }

    m_titleBarHeight = clamped;
    emit titleBarHeightChanged();
}

void WindowChromeController::setFrameText(const QString& text)
{
    if (m_frameText == text)
    {
        return;
    }

    m_frameText = text;
    emit frameTextChanged();
}

void WindowChromeController::minimize()
{
    m_window.showMinimized();
}

void WindowChromeController::toggleMaximized()
{
    if (m_window.isMaximized())
    {
        m_window.showNormal();
    }
    else
    {
        m_window.showMaximized();
    }
}

void WindowChromeController::closeWindow()
{
    m_window.close();
}

void WindowChromeController::startSystemMove()
{
    m_window.startSystemMove();
}

void WindowChromeController::startSystemResize(const int edge)
{
    m_window.startSystemResize(static_cast<Qt::Edges>(edge));
}

void WindowChromeController::showSystemMenu(const int globalX, const int globalY)
{
#ifdef Q_OS_WIN
    const auto hwnd = reinterpret_cast<HWND>(m_window.winId());
    if (!hwnd)
    {
        return;
    }

    const auto menu = GetSystemMenu(hwnd, FALSE);
    if (!menu)
    {
        return;
    }

    const auto selected = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        globalX,
        globalY,
        0,
        hwnd,
        nullptr);
    if (selected != 0)
    {
        PostMessage(hwnd, WM_SYSCOMMAND, selected, 0);
    }
#else
    Q_UNUSED(globalX);
    Q_UNUSED(globalY);
#endif
}

bool WindowChromeController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &m_window && event)
    {
        switch (event->type())
        {
        case QEvent::WindowStateChange:
            emit maximizedChanged();
            break;
        case QEvent::ActivationChange:
            emit activeChanged();
            break;
        default:
            break;
        }
    }

    return QObject::eventFilter(watched, event);
}
