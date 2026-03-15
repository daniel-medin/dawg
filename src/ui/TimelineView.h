#pragma once

#include <optional>
#include <vector>

#include <QColor>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QWidget>

struct TimelineTrackSpan
{
    QUuid id;
    QString label;
    QColor color;
    int startFrame = 0;
    int endFrame = 0;
    int laneIndex = 0;
    bool hasAttachedAudio = false;
    bool isSelected = false;
};

class TimelineView final : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);

    void clear();
    void setTimeline(int totalFrames, double fps);
    void setCurrentFrame(int frameIndex);
    void setLoopRange(std::optional<int> startFrame, std::optional<int> endFrame);
    void setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans);
    void setSeekOnClickEnabled(bool enabled);
    [[nodiscard]] std::optional<int> loopEditFrame() const;
    [[nodiscard]] std::optional<int> loopShortcutFrame() const;
    [[nodiscard]] bool hasSelectedLoopRange() const;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

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
    };

    [[nodiscard]] std::vector<TimelineTrackGeometry> trackGeometries() const;
    [[nodiscard]] std::optional<LoopRangeGeometry> loopRangeGeometry() const;
    [[nodiscard]] std::optional<TimelineTrackGeometry> trackAt(const QPointF& position) const;
    void updateTrimAt(const QPointF& position);
    void updateLoopHandleDragAt(const QPointF& position);
    void updateSpanDragAt(const QPointF& position);
    [[nodiscard]] QRectF timelineRect() const;
    [[nodiscard]] QRectF loopBarRect() const;
    [[nodiscard]] QRectF trackAreaRect() const;
    [[nodiscard]] double visibleFrameSpan() const;
    [[nodiscard]] double visibleFrameSpanForZoom(double zoomScale) const;
    [[nodiscard]] double maxZoomScale() const;
    [[nodiscard]] int gridStepFrames() const;
    void clampViewWindow();
    void ensureFrameVisible(int frameIndex);
    [[nodiscard]] int frameForPosition(double x) const;
    [[nodiscard]] double xForFrame(int frameIndex) const;
    [[nodiscard]] int laneCount() const;
    [[nodiscard]] int preferredHeight() const;
    void updatePreferredHeight();
    void requestFrame(int frameIndex);
    void requestFrameCoalesced(int frameIndex);
    void flushPendingFrameRequest();
    void requestFrameAt(const QPointF& position);

    enum class LoopHandleDragMode
    {
        None,
        Start,
        End,
        Range
    };

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
};
