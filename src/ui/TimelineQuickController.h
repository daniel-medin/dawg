#pragma once

#include <optional>
#include <vector>

#include <QCursor>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QTimer>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>

#include "ui/TimelineTypes.h"

class TimelineQuickController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap timelineRect READ timelineRect NOTIFY visualsChanged)
    Q_PROPERTY(QVariantMap filmstripRect READ filmstripRect NOTIFY visualsChanged)
    Q_PROPERTY(QVariantMap loopBarRect READ loopBarRect NOTIFY visualsChanged)
    Q_PROPERTY(QVariantMap trackAreaRect READ trackAreaRect NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList gridLines READ gridLines NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList thumbnailTiles READ thumbnailTiles NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList trackGeometries READ trackGeometries NOTIFY visualsChanged)
    Q_PROPERTY(QVariantMap loopRangeGeometry READ loopRangeGeometry NOTIFY visualsChanged)
    Q_PROPERTY(double markerX READ markerX NOTIFY markerChanged)
    Q_PROPERTY(bool hasLoopIndicator READ hasLoopIndicator NOTIFY overlayChanged)
    Q_PROPERTY(double loopIndicatorX READ loopIndicatorX NOTIFY overlayChanged)
    Q_PROPERTY(bool hasHoverLine READ hasHoverLine NOTIFY overlayChanged)
    Q_PROPERTY(double hoverX READ hoverX NOTIFY overlayChanged)
    Q_PROPERTY(int cursorShape READ cursorShape NOTIFY cursorShapeChanged)

public:
    explicit TimelineQuickController(QObject* parent = nullptr);

    void clear();
    void setVideoPath(const QString& videoPath);
    void setTimeline(int totalFrames, double fps);
    void setCurrentFrame(int frameIndex);
    void setLoopRange(std::optional<int> startFrame, std::optional<int> endFrame);
    void setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans);
    void setSeekOnClickEnabled(bool enabled);
    void setThumbnailsVisible(bool visible);
    [[nodiscard]] std::optional<int> loopEditFrame() const;
    [[nodiscard]] std::optional<int> loopShortcutFrame() const;
    [[nodiscard]] bool hasSelectedLoopRange() const;

    [[nodiscard]] QVariantMap timelineRect() const;
    [[nodiscard]] QVariantMap filmstripRect() const;
    [[nodiscard]] QVariantMap loopBarRect() const;
    [[nodiscard]] QVariantMap trackAreaRect() const;
    [[nodiscard]] QVariantList gridLines() const;
    [[nodiscard]] QVariantList thumbnailTiles() const;
    [[nodiscard]] QVariantList trackGeometries() const;
    [[nodiscard]] QVariantMap loopRangeGeometry() const;
    [[nodiscard]] double markerX() const;
    [[nodiscard]] bool hasLoopIndicator() const;
    [[nodiscard]] double loopIndicatorX() const;
    [[nodiscard]] bool hasHoverLine() const;
    [[nodiscard]] double hoverX() const;
    [[nodiscard]] int cursorShape() const;

    Q_INVOKABLE void setViewportSize(double width, double height);
    Q_INVOKABLE void handleMousePress(int button, double x, double y, int modifiers, int globalX, int globalY);
    Q_INVOKABLE void handleMouseDoubleClick(int button, double x, double y);
    Q_INVOKABLE void handleMouseMove(double x, double y, int globalX, int globalY);
    Q_INVOKABLE void handleMouseRelease(int button);
    Q_INVOKABLE void handleWheel(double x, double y, int angleDeltaY, int modifiers, int globalX, int globalY);
    Q_INVOKABLE void handleHoverMove(double x, double y, int globalX, int globalY);
    Q_INVOKABLE void handleHoverLeave();

signals:
    void frameRequested(int frameIndex);
    void loopStartFrameRequested(int frameIndex);
    void loopEndFrameRequested(int frameIndex);
    void trackSelected(const QUuid& trackId);
    void trackActivated(const QUuid& trackId);
    void trackStartFrameRequested(const QUuid& trackId, int frameIndex);
    void trackEndFrameRequested(const QUuid& trackId, int frameIndex);
    void trackSpanMoveRequested(const QUuid& trackId, int deltaFrames);
    void trackContextMenuRequested(const QUuid& trackId, const QPoint& globalPosition);
    void trackGainAdjustRequested(const QUuid& trackId, int wheelDelta, const QPoint& globalPosition);
    void trackGainPopupRequested(const QUuid& trackId, const QPoint& globalPosition);
    void loopContextMenuRequested(const QPoint& globalPosition);
    void visualsChanged();
    void markerChanged();
    void overlayChanged();
    void cursorShapeChanged();

private:
    struct LoopRangeGeometry
    {
        QRectF barRect;
        QRectF selectionRect;
        QRectF startHandleRect;
        QRectF endHandleRect;
    };

    struct TimelineTrackGeometry
    {
        QRectF hitRect;
        QRectF startHandleRect;
        QRectF endHandleRect;
        double lineStartX = 0.0;
        double lineEndX = 0.0;
        QUuid id;
        QString label;
        QColor color;
        bool selected = false;
        bool hasAttachedAudio = false;
    };

    enum class LoopHandleDragMode
    {
        None,
        Start,
        End,
        Range
    };

    [[nodiscard]] std::vector<TimelineTrackGeometry> computeTrackGeometries() const;
    [[nodiscard]] std::optional<LoopRangeGeometry> computeLoopRangeGeometry() const;
    [[nodiscard]] std::optional<TimelineTrackGeometry> trackAt(const QPointF& position) const;
    void updateTrimAt(const QPointF& position);
    void updateLoopHandleDragAt(const QPointF& position);
    void updateSpanDragAt(const QPointF& position);
    void updateHoverState(const QPointF& position);
    void updateCursorAndTooltip(const QPointF& position, const QPoint& globalPosition);
    void refreshVisuals();
    [[nodiscard]] QRectF computeTimelineRect() const;
    [[nodiscard]] QRectF computeFilmstripRect() const;
    [[nodiscard]] QRectF computeLoopBarRect() const;
    [[nodiscard]] QRectF computeTrackAreaRect() const;
    [[nodiscard]] QVector<int> computeThumbnailFrames() const;
    void requestThumbnailFrames(const QVector<int>& frameIndices);
    [[nodiscard]] double visibleFrameSpan() const;
    [[nodiscard]] double visibleFrameSpanForZoom(double zoomScale) const;
    [[nodiscard]] double maxZoomScale() const;
    [[nodiscard]] int gridStepFrames() const;
    void clampViewWindow();
    void ensureFrameVisible(int frameIndex);
    [[nodiscard]] int frameForPosition(double x) const;
    [[nodiscard]] double xForFrame(int frameIndex) const;
    [[nodiscard]] int laneCount() const;
    void requestFrame(int frameIndex);
    void requestFrameCoalesced(int frameIndex);
    void flushPendingFrameRequest();
    void requestFrameAt(const QPointF& position);
    [[nodiscard]] static QVariantMap rectMap(const QRectF& rect);

    double m_viewportWidth = 0.0;
    double m_viewportHeight = 0.0;
    std::optional<TimelineTrackGeometry> m_trimmedTrack;
    std::optional<TimelineTrackGeometry> m_draggedTrack;
    std::optional<int> m_loopStartFrame;
    std::optional<int> m_loopEndFrame;
    std::optional<int> m_loopEditFrame;
    bool m_loopSelected = false;
    std::optional<int> m_hoveredLoopFrame;
    std::optional<double> m_hoveredTimelineX;
    bool m_trimmingStart = false;
    LoopHandleDragMode m_loopHandleDragMode = LoopHandleDragMode::None;
    int m_loopDragAnchorFrame = 0;
    int m_dragAnchorFrame = 0;
    int m_dragAccumulatedDeltaFrames = 0;
    int m_totalFrames = 0;
    int m_currentFrame = 0;
    double m_fps = 0.0;
    double m_horizontalZoom = 1.0;
    double m_viewStartFrame = 0.0;
    std::vector<TimelineTrackSpan> m_trackSpans;
    bool m_dragging = false;
    bool m_seekOnClickEnabled = true;
    int m_lastRequestedFrame = -1;
    int m_pendingRequestedFrame = -1;
    QTimer m_scrubRequestTimer;

    QVariantMap m_timelineRect;
    QVariantMap m_filmstripRect;
    QVariantMap m_loopBarRect;
    QVariantMap m_trackAreaRect;
    QVariantList m_gridLines;
    QVariantList m_thumbnailTiles;
    QVariantList m_trackGeometryMaps;
    QVariantMap m_loopRangeGeometryMap;
    double m_markerX = 0.0;
    bool m_hasLoopIndicator = false;
    double m_loopIndicatorX = 0.0;
    bool m_hasHoverLine = false;
    double m_hoverX = 0.0;
    int m_cursorShape = static_cast<int>(Qt::ArrowCursor);
    QString m_videoPath;
    QVector<int> m_thumbnailFrames;
    QHash<int, QString> m_thumbnailSources;
    bool m_showThumbnails = true;
};
