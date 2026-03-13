#pragma once

#include <vector>

#include <QColor>
#include <QUuid>
#include <QWidget>

struct TimelineTrackSpan
{
    QUuid id;
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

signals:
    void frameRequested(int frameIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QRectF timelineRect() const;
    [[nodiscard]] int frameForPosition(double x) const;
    [[nodiscard]] double xForFrame(int frameIndex) const;
    void requestFrameAt(const QPointF& position);

    int m_totalFrames = 0;
    int m_currentFrame = 0;
    double m_fps = 0.0;
    std::vector<TimelineTrackSpan> m_trackSpans;
    bool m_dragging = false;
};
