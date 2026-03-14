#pragma once

#include <optional>
#include <vector>

#include <QColor>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QWidget>

struct TimelineTrackSpan
{
    QUuid id;
    QString label;
    QColor color;
    int startFrame = 0;
    int endFrame = 0;
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
    void setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans);
    void setSeekOnClickEnabled(bool enabled);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void frameRequested(int frameIndex);
    void trackSelected(const QUuid& trackId);
    void trackStartFrameRequested(const QUuid& trackId, int frameIndex);
    void trackEndFrameRequested(const QUuid& trackId, int frameIndex);
    void trackSpanMoveRequested(const QUuid& trackId, int deltaFrames);
    void trackContextMenuRequested(const QUuid& trackId, const QPoint& globalPosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
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
    [[nodiscard]] std::optional<TimelineTrackGeometry> trackAt(const QPointF& position) const;
    void updateTrimAt(const QPointF& position);
    void updateSpanDragAt(const QPointF& position);
    [[nodiscard]] QRectF timelineRect() const;
    [[nodiscard]] int frameForPosition(double x) const;
    [[nodiscard]] double xForFrame(int frameIndex) const;
    [[nodiscard]] int preferredHeight() const;
    void updatePreferredHeight();
    void requestFrame(int frameIndex);
    void requestFrameAt(const QPointF& position);

    std::optional<TimelineTrackGeometry> m_trimmedTrack;
    std::optional<TimelineTrackGeometry> m_draggedTrack;
    bool m_trimmingStart = false;
    int m_dragAnchorFrame = 0;
    int m_dragAccumulatedDeltaFrames = 0;
    int m_totalFrames = 0;
    int m_currentFrame = 0;
    double m_fps = 0.0;
    std::vector<TimelineTrackSpan> m_trackSpans;
    bool m_dragging = false;
    bool m_seekOnClickEnabled = true;
    int m_lastRequestedFrame = -1;
};
