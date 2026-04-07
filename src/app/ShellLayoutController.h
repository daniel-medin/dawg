#pragma once

#include <QRect>
#include <QObject>
#include <QString>
#include <QVariantMap>

class ShellLayoutController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap canvasRect READ canvasRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap thumbnailRect READ thumbnailRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap timelineRect READ timelineRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap nodeEditorRect READ nodeEditorRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap mixRect READ mixRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap audioPoolRect READ audioPoolRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap timelineHandleRect READ timelineHandleRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap nodeEditorHandleRect READ nodeEditorHandleRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap mixHandleRect READ mixHandleRect NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap audioPoolHandleRect READ audioPoolHandleRect NOTIFY layoutChanged)
    Q_PROPERTY(int handleThickness READ handleThickness CONSTANT)

public:
    explicit ShellLayoutController(QObject* parent = nullptr);

    [[nodiscard]] QVariantMap canvasRect() const;
    [[nodiscard]] QVariantMap thumbnailRect() const;
    [[nodiscard]] QVariantMap timelineRect() const;
    [[nodiscard]] QVariantMap nodeEditorRect() const;
    [[nodiscard]] QVariantMap mixRect() const;
    [[nodiscard]] QVariantMap audioPoolRect() const;
    [[nodiscard]] QVariantMap timelineHandleRect() const;
    [[nodiscard]] QVariantMap nodeEditorHandleRect() const;
    [[nodiscard]] QVariantMap mixHandleRect() const;
    [[nodiscard]] QVariantMap audioPoolHandleRect() const;
    [[nodiscard]] int handleThickness() const;

    [[nodiscard]] QRect canvasPanelRect() const;
    [[nodiscard]] QRect thumbnailPanelRect() const;
    [[nodiscard]] QRect timelinePanelRect() const;
    [[nodiscard]] QRect nodeEditorPanelRect() const;
    [[nodiscard]] QRect mixPanelRect() const;
    [[nodiscard]] QRect audioPoolPanelRect() const;
    [[nodiscard]] bool thumbnailsVisible() const;
    [[nodiscard]] bool timelineVisible() const;
    [[nodiscard]] bool nodeEditorVisible() const;
    [[nodiscard]] bool mixVisible() const;
    [[nodiscard]] bool audioPoolVisible() const;

    Q_INVOKABLE void setViewportSize(int width, int height);
    void setVideoDetached(bool detached);
    void setThumbnailsVisible(bool visible);
    void setTimelineVisible(bool visible);
    void setNodeEditorVisible(bool visible);
    void setMixVisible(bool visible);
    void setAudioPoolVisible(bool visible);
    void setPreferredSizes(int audioPoolWidth, int timelineHeight, int nodeEditorHeight, int mixHeight);
    void setTimelineMinimumHeight(int height);

    Q_INVOKABLE void beginResize(const QString& handleKey, double x, double y);
    Q_INVOKABLE void updateResize(double x, double y);
    Q_INVOKABLE void endResize();

signals:
    void layoutChanged();
    void preferredSizesChanged(int audioPoolWidth, int timelineHeight, int nodeEditorHeight, int mixHeight);

private:
    enum class ActiveHandle
    {
        None,
        AudioPool,
        Timeline,
        NodeEditor,
        Mix,
    };

    void recomputeLayout();
    [[nodiscard]] static QVariantMap rectMap(const QRect& rect);
    void emitPreferredSizesIfNeeded(
        int previousAudioPoolWidth,
        int previousTimelineHeight,
        int previousNodeEditorHeight,
        int previousMixHeight);

    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    bool m_videoDetached = false;
    bool m_thumbnailsVisible = true;
    bool m_timelineVisible = true;
    bool m_nodeEditorVisible = false;
    bool m_mixVisible = false;
    bool m_audioPoolVisible = false;
    int m_audioPoolPreferredWidth = 320;
    int m_timelinePreferredHeight = 148;
    int m_nodeEditorPreferredHeight = 260;
    int m_mixPreferredHeight = 368;
    int m_timelineMinimumHeight = 154;

    QRect m_canvasRect;
    QRect m_thumbnailRect;
    QRect m_timelineRect;
    QRect m_nodeEditorRect;
    QRect m_mixRect;
    QRect m_audioPoolRect;
    QRect m_timelineHandleRect;
    QRect m_nodeEditorHandleRect;
    QRect m_mixHandleRect;
    QRect m_audioPoolHandleRect;

    ActiveHandle m_activeHandle = ActiveHandle::None;
    int m_resizeAnchorX = 0;
    int m_resizeAnchorY = 0;
    int m_resizeStartAudioPoolWidth = 320;
    int m_resizeStartTimelineHeight = 148;
    int m_resizeStartNodeEditorHeight = 260;
    int m_resizeStartMixHeight = 368;
};
