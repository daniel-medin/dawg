#pragma once

#include <QList>
#include <QImage>
#include <QMetaObject>
#include <QQuickItem>

#include "core/video/VideoFrame.h"

class VideoViewportQuickController;

class VideoViewportQuickItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit VideoViewportQuickItem(QQuickItem* parent = nullptr);
    ~VideoViewportQuickItem() override;

    [[nodiscard]] QObject* controller() const;
    void setController(QObject* controllerObject);

signals:
    void controllerChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateData) override;
    void releaseResources() override;

private:
    struct FrameSnapshot
    {
        QImage image;
        VideoFrame videoFrame;
        bool hasFrame = false;
        bool nativePresentationActive = false;
        int revision = 0;
    };

    void syncSnapshot();
    void handleFrameChanged();
    void disconnectController();

    VideoViewportQuickController* m_controller = nullptr;
    FrameSnapshot m_snapshot;
    QList<QMetaObject::Connection> m_controllerConnections;
};
