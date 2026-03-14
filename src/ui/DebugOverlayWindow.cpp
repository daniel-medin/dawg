#include "ui/DebugOverlayWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

DebugOverlayWindow::DebugOverlayWindow(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("debugOverlay"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAutoFillBackground(true);
    setFixedWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("debugOverlayTitleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 8, 8, 8);
    titleLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("Debug"), titleBar);
    titleLabel->setObjectName(QStringLiteral("debugOverlayTitle"));

    auto* closeButton = new QPushButton(QStringLiteral("x"), titleBar);
    closeButton->setObjectName(QStringLiteral("debugOverlayCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(22, 22);

    m_textLabel = new QLabel(this);
    m_textLabel->setObjectName(QStringLiteral("debugOverlayText"));
    m_textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_textLabel->setWordWrap(true);
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);
    titleLayout->addWidget(closeButton);
    layout->addWidget(titleBar);
    layout->addWidget(m_textLabel);

    connect(closeButton, &QPushButton::clicked, this, &DebugOverlayWindow::closeRequested);
}

void DebugOverlayWindow::setListText(const QString& text)
{
    if (m_textLabel)
    {
        m_textLabel->setText(text);
    }
}

void DebugOverlayWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QFrame::mousePressEvent(event);
}

void DebugOverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QFrame::mouseMoveEvent(event);
}

void DebugOverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
    }

    QFrame::mouseReleaseEvent(event);
}
