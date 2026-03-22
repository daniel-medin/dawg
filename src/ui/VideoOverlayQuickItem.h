#pragma once

#include <cstdint>

#include <QHash>
#include <QQuickPaintedItem>
#include <QStaticText>

class VideoViewportQuickController;

class VideoOverlayQuickItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    struct DebugStats
    {
        double paintMs = 0.0;
        int overlayCount = 0;
        int labelCount = 0;
    };

    explicit VideoOverlayQuickItem(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* controller() const;
    void setController(QObject* controllerObject);
    [[nodiscard]] static DebugStats debugStats();

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();

private:
    struct LabelCacheEntry
    {
        QStaticText text;
        qreal width = 0.0;
        qreal height = 0.0;
    };

    void disconnectController();

    VideoViewportQuickController* m_controller = nullptr;
    QList<QMetaObject::Connection> m_controllerConnections;
    QHash<QString, LabelCacheEntry> m_labelCache;
};
