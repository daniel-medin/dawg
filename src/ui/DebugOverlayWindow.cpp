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
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(true);
    setFixedWidth(300);
    setMinimumHeight(120);
    setStyleSheet(QStringLiteral(
        "QFrame#debugOverlay {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#debugOverlayTitleBar {"
        "  background: rgba(17, 24, 33, 204);"
        "  border: 1px solid #253142;"
        "  border-radius: 8px;"
        "}"
        "QLabel#debugOverlayTitle {"
        "  color: #f3f5f7;"
        "  font-weight: 600;"
        "}"
        "QLabel#debugOverlayText {"
        "  color: #d8dde4;"
        "  font-size: 9pt;"
        "  padding: 10px;"
        "  background: rgba(11, 15, 20, 204);"
        "  border: 1px solid #253142;"
        "  border-radius: 8px;"
        "}"
        "QPushButton#debugOverlayCloseButton {"
        "  background: #18202b;"
        "  color: #ecf1f6;"
        "  border: 1px solid #324155;"
        "  border-radius: 4px;"
        "  padding: 0px;"
        "}"
        "QPushButton#debugOverlayCloseButton:hover {"
        "  background: #223146;"
        "}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

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
        adjustSize();
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
