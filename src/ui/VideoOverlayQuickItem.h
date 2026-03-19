#pragma once

#include <QQuickPaintedItem>

class VideoViewportQuickController;

class VideoOverlayQuickItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit VideoOverlayQuickItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* controller() const;
    void setController(QObject* controllerObject);

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();

private:
    void disconnectController();

    VideoViewportQuickController* m_controller = nullptr;
    QList<QMetaObject::Connection> m_controllerConnections;
};
