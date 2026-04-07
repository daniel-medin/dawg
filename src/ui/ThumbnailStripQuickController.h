#pragma once

#include <optional>

#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "app/ProjectTimelineThumbnails.h"

class ThumbnailStripQuickController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap stripRect READ stripRect NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList thumbnailTiles READ thumbnailTiles NOTIFY visualsChanged)
    Q_PROPERTY(double markerX READ markerX NOTIFY markerChanged)
    Q_PROPERTY(bool hasSelectedNodeRange READ hasSelectedNodeRange NOTIFY overlayChanged)
    Q_PROPERTY(double selectedNodeRangeX READ selectedNodeRangeX NOTIFY overlayChanged)
    Q_PROPERTY(double selectedNodeRangeWidth READ selectedNodeRangeWidth NOTIFY overlayChanged)
    Q_PROPERTY(bool hasHoverLine READ hasHoverLine NOTIFY overlayChanged)
    Q_PROPERTY(double hoverX READ hoverX NOTIFY overlayChanged)
    Q_PROPERTY(bool playbackActive READ playbackActive NOTIFY playbackActiveChanged)

public:
    explicit ThumbnailStripQuickController(QObject* parent = nullptr);

    void clear();
    void setProjectRootPath(const QString& projectRootPath);
    void setVideoPath(const QString& videoPath);
    void setTimeline(int totalFrames, double fps);
    void setViewWindow(double startFrame, double visibleFrameSpan);
    void setCurrentFrame(int frameIndex);
    void setCurrentFramePosition(double framePosition);
    void setSelectedNodeRange(std::optional<int> startFrame, std::optional<int> endFrame);
    void setPlaybackActive(bool active);

    [[nodiscard]] QVariantMap stripRect() const;
    [[nodiscard]] QVariantList thumbnailTiles() const;
    [[nodiscard]] double markerX() const;
    [[nodiscard]] bool hasSelectedNodeRange() const;
    [[nodiscard]] double selectedNodeRangeX() const;
    [[nodiscard]] double selectedNodeRangeWidth() const;
    [[nodiscard]] bool hasHoverLine() const;
    [[nodiscard]] double hoverX() const;
    [[nodiscard]] bool playbackActive() const;
    [[nodiscard]] bool hasThumbnailManifest() const;

    Q_INVOKABLE void setViewportSize(double width, double height);
    Q_INVOKABLE void handleMousePress(int button, double x, double y);
    Q_INVOKABLE void handleMouseMove(double x, double y);
    Q_INVOKABLE void handleMouseRelease(int button);
    Q_INVOKABLE void handleHoverMove(double x, double y);
    Q_INVOKABLE void handleHoverLeave();

signals:
    void frameRequested(int frameIndex);
    void visualsChanged();
    void markerChanged();
    void overlayChanged();
    void playbackActiveChanged();

private:
    void refreshVisuals();
    void reloadThumbnailManifest();
    [[nodiscard]] QRectF computeStripRect() const;
    [[nodiscard]] QVector<int> computeThumbnailFrames() const;
    [[nodiscard]] QString thumbnailSourceForFrame(int targetFrameIndex) const;
    [[nodiscard]] int frameForPosition(double x) const;
    [[nodiscard]] double xForFrame(int frameIndex) const;
    [[nodiscard]] double xForFramePosition(double framePosition) const;
    void requestFrame(int frameIndex);
    void requestFrameCoalesced(int frameIndex);
    void flushPendingFrameRequest();
    void requestFrameAt(double x);
    void updateHover(double x, double y);
    void clampViewWindow();
    [[nodiscard]] static QVariantMap rectMap(const QRectF& rect);

    double m_viewportWidth = 0.0;
    double m_viewportHeight = 0.0;
    int m_totalFrames = 0;
    int m_currentFrame = 0;
    double m_currentFramePosition = 0.0;
    double m_fps = 0.0;
    double m_viewStartFrame = 0.0;
    double m_visibleFrameSpan = 1.0;
    bool m_dragging = false;
    int m_lastRequestedFrame = -1;
    int m_pendingRequestedFrame = -1;
    QTimer m_scrubRequestTimer;

    QVariantMap m_stripRect;
    QVariantList m_thumbnailTiles;
    double m_markerX = 0.0;
    bool m_hasSelectedNodeRange = false;
    double m_selectedNodeRangeX = 0.0;
    double m_selectedNodeRangeWidth = 0.0;
    std::optional<int> m_selectedNodeStartFrame;
    std::optional<int> m_selectedNodeEndFrame;
    bool m_hasHoverLine = false;
    double m_hoverX = 0.0;
    QString m_projectRootPath;
    QString m_videoPath;
    QVector<int> m_thumbnailFrames;
    std::optional<dawg::timeline::ThumbnailManifest> m_thumbnailManifest;
    std::optional<double> m_hoveredStripX;
    bool m_playbackActive = false;
};
