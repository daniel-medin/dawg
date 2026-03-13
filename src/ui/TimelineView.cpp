#include "ui/TimelineView.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(84);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
}

void TimelineView::clear()
{
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_fps = 0.0;
    m_trackSpans.clear();
    m_dragging = false;
    update();
}

void TimelineView::setTimeline(const int totalFrames, const double fps)
{
    m_totalFrames = std::max(0, totalFrames);
    m_fps = fps > 0.0 ? fps : 30.0;
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_totalFrames - 1));
    update();
}

void TimelineView::setCurrentFrame(const int frameIndex)
{
    m_currentFrame = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    update();
}

void TimelineView::setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans)
{
    m_trackSpans = trackSpans;
    update();
}

void TimelineView::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{5, 6, 8});

    const auto laneRect = timelineRect();
    painter.setPen(QPen(QColor{32, 36, 42}, 1.0));
    painter.setBrush(QColor{7, 8, 10});
    painter.drawRect(laneRect);

    if (m_totalFrames <= 0)
    {
        return;
    }

    if (!m_trackSpans.empty())
    {
        const auto trackCount = static_cast<int>(m_trackSpans.size());
        const auto verticalPadding = 8.0;
        const auto rowGap = trackCount > 10 ? 1.0 : 2.0;
        const auto availableHeight = std::max(8.0, laneRect.height() - verticalPadding * 2.0);
        const auto rowHeight = std::max(
            3.0,
            std::min(10.0, (availableHeight - std::max(0, trackCount - 1) * rowGap) / std::max(1, trackCount)));
        const auto capHeight = std::max(3.0, std::min(6.0, rowHeight + 1.0));

        for (int index = 0; index < trackCount; ++index)
        {
            const auto& trackSpan = m_trackSpans[static_cast<std::size_t>(index)];
            const auto y = laneRect.top() + verticalPadding + (rowHeight + rowGap) * index + rowHeight * 0.5;
            const auto startX = xForFrame(std::max(0, trackSpan.startFrame));
            const auto endX = xForFrame(std::max(trackSpan.startFrame, trackSpan.endFrame));
            auto lineColor = trackSpan.color;
            lineColor.setAlphaF(trackSpan.isSelected ? 1.0 : 0.85);
            const auto lineWidth = trackSpan.isSelected ? 2.0 : 1.0;

            painter.setPen(QPen(lineColor, lineWidth));
            painter.drawLine(QPointF{startX, y}, QPointF{endX, y});
            painter.drawLine(
                QPointF{startX, y - capHeight * 0.5},
                QPointF{startX, y + capHeight * 0.5});
            painter.drawLine(
                QPointF{endX, y - capHeight * 0.5},
                QPointF{endX, y + capHeight * 0.5});
        }
    }

    const auto markerX = xForFrame(m_currentFrame);
    painter.setPen(QPen(QColor{225, 229, 234}, 2.0));
    painter.drawLine(QPointF{markerX, laneRect.top()}, QPointF{markerX, laneRect.bottom()});
}

void TimelineView::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    if (event->button() != Qt::LeftButton || m_totalFrames <= 0)
    {
        return;
    }

    m_dragging = true;
    setFocus(Qt::MouseFocusReason);
    requestFrameAt(event->position());
}

void TimelineView::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    if (!m_dragging || m_totalFrames <= 0)
    {
        return;
    }

    requestFrameAt(event->position());
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
    }
}

QRectF TimelineView::timelineRect() const
{
    return QRectF{
        8.0,
        8.0,
        std::max(120.0, static_cast<double>(width() - 16)),
        std::max(48.0, static_cast<double>(height() - 16))
    };
}

int TimelineView::frameForPosition(const double x) const
{
    if (m_totalFrames <= 1)
    {
        return 0;
    }

    const auto rect = timelineRect();
    const auto ratio = std::clamp((x - rect.left()) / rect.width(), 0.0, 1.0);
    return static_cast<int>(std::lround(ratio * static_cast<double>(m_totalFrames - 1)));
}

double TimelineView::xForFrame(const int frameIndex) const
{
    const auto rect = timelineRect();
    if (m_totalFrames <= 1)
    {
        return rect.left();
    }

    const auto ratio = static_cast<double>(std::clamp(frameIndex, 0, m_totalFrames - 1)) / static_cast<double>(m_totalFrames - 1);
    return rect.left() + rect.width() * ratio;
}

void TimelineView::requestFrameAt(const QPointF& position)
{
    emit frameRequested(frameForPosition(position.x()));
}
