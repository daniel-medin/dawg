#pragma once

#include <QFrame>
#include <QPoint>

class QLabel;

class DebugOverlayWindow final : public QFrame
{
    Q_OBJECT

public:
    explicit DebugOverlayWindow(QWidget* parent = nullptr);

    void setListText(const QString& text);

signals:
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QLabel* m_textLabel = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
