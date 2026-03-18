#pragma once

#include <QObject>
#include <QPoint>
#include <QQuickView>
#include <QString>

class DebugOverlayWindow final : public QQuickView
{
    Q_OBJECT

public:
    explicit DebugOverlayWindow(QWindow* parent = nullptr);

    void setListText(const QString& text);

signals:
    void closeRequested();

private slots:
    void handleCloseClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_dragging = false;
    QPoint m_dragOffset;
};
