#include "ui/TimelineView.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(84);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    updatePreferredHeight();
}

void TimelineView::clear()
{
    m_trimmedTrack.reset();
    m_draggedTrack.reset();
    m_trimmingStart = false;
    m_dragAnchorFrame = 0;
    m_dragAccumulatedDeltaFrames = 0;
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_fps = 0.0;
    m_trackSpans.clear();
    m_dragging = false;
    m_lastRequestedFrame = -1;
    updatePreferredHeight();
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
    m_lastRequestedFrame = m_currentFrame;
    update();
}

void TimelineView::setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans)
{
    m_trackSpans = trackSpans;
    updatePreferredHeight();
    update();
}

void TimelineView::setSeekOnClickEnabled(const bool enabled)
{
    m_seekOnClickEnabled = enabled;
}

QSize TimelineView::sizeHint() const
{
    return QSize{640, preferredHeight()};
}

QSize TimelineView::minimumSizeHint() const
{
    return QSize{320, preferredHeight()};
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

    const auto geometries = trackGeometries();
    if (!m_trackSpans.empty())
    {
        for (int index = 0; index < static_cast<int>(m_trackSpans.size()); ++index)
        {
            const auto& trackSpan = m_trackSpans[static_cast<std::size_t>(index)];
            const auto& geometry = geometries[static_cast<std::size_t>(index)];
            const auto y = geometry.hitRect.center().y();
            const auto startX = geometry.lineStartX;
            const auto endX = geometry.lineEndX;
            auto lineColor = trackSpan.color;
            lineColor.setAlphaF(trackSpan.isSelected ? 1.0 : 0.85);
            const auto lineWidth = trackSpan.isSelected ? 2.0 : 1.0;
            const auto capHeight = std::max(3.0, std::min(6.0, geometry.hitRect.height() - 1.0));

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

    if (m_totalFrames <= 0)
    {
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        if (const auto track = trackAt(event->position()); track.has_value())
        {
            setFocus(Qt::MouseFocusReason);
            emit trackSelected(track->id);
            emit trackContextMenuRequested(track->id, event->globalPosition().toPoint());
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    if (const auto track = trackAt(event->position()); track.has_value())
    {
        setFocus(Qt::MouseFocusReason);
        emit trackSelected(track->id);

        if (track->startHandleRect.contains(event->position()))
        {
            m_trimmedTrack = track;
            m_trimmingStart = true;
            updateTrimAt(event->position());
            return;
        }

        if (track->endHandleRect.contains(event->position()))
        {
            m_trimmedTrack = track;
            m_trimmingStart = false;
            updateTrimAt(event->position());
            return;
        }

        const auto spanIt = std::find_if(
            m_trackSpans.begin(),
            m_trackSpans.end(),
            [&track](const TimelineTrackSpan& span)
            {
                return span.id == track->id;
            });
        const auto isSelected = spanIt != m_trackSpans.end() && spanIt->isSelected;
        if (isSelected)
        {
            if (m_seekOnClickEnabled)
            {
                requestFrameAt(event->position());
            }
            m_draggedTrack = track;
            m_dragAnchorFrame = frameForPosition(event->position().x());
            m_dragAccumulatedDeltaFrames = 0;
            return;
        }
    }

    m_dragging = true;
    setFocus(Qt::MouseFocusReason);
    if (m_seekOnClickEnabled)
    {
        requestFrameAt(event->position());
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    if (const auto track = trackAt(event->position()); track.has_value())
    {
        QToolTip::showText(event->globalPosition().toPoint(), track->label, this);
        if (track->startHandleRect.contains(event->position()) || track->endHandleRect.contains(event->position()))
        {
            setCursor(Qt::SizeHorCursor);
        }
        else
        {
            unsetCursor();
        }
    }
    else
    {
        QToolTip::hideText();
        unsetCursor();
    }

    if (m_trimmedTrack.has_value())
    {
        updateTrimAt(event->position());
        return;
    }

    if (m_draggedTrack.has_value())
    {
        updateSpanDragAt(event->position());
        return;
    }

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
        m_trimmedTrack.reset();
        m_draggedTrack.reset();
        m_trimmingStart = false;
        m_dragAnchorFrame = 0;
        m_dragAccumulatedDeltaFrames = 0;
        m_dragging = false;
    }
}

std::vector<TimelineView::TimelineTrackGeometry> TimelineView::trackGeometries() const
{
    std::vector<TimelineTrackGeometry> geometries;
    geometries.reserve(m_trackSpans.size());

    if (m_trackSpans.empty())
    {
        return geometries;
    }

    const auto laneRect = timelineRect();
    const auto trackCount = static_cast<int>(m_trackSpans.size());
    const auto verticalPadding = 8.0;
    const auto rowGap = trackCount > 10 ? 1.0 : 2.0;
    const auto availableHeight = std::max(8.0, laneRect.height() - verticalPadding * 2.0);
    const auto rowHeight = std::max(
        3.0,
        std::min(10.0, (availableHeight - std::max(0, trackCount - 1) * rowGap) / std::max(1, trackCount)));

    for (int index = 0; index < trackCount; ++index)
    {
        const auto& trackSpan = m_trackSpans[static_cast<std::size_t>(index)];
        const auto y = laneRect.top() + verticalPadding + (rowHeight + rowGap) * index;
        const auto startX = xForFrame(std::max(0, trackSpan.startFrame));
        const auto endX = xForFrame(std::max(trackSpan.startFrame, trackSpan.endFrame));
        const auto left = std::min(startX, endX);
        const auto right = std::max(startX, endX);

        geometries.push_back(TimelineTrackGeometry{
            .hitRect = QRectF{
                left - 4.0,
                y - 2.0,
                std::max(8.0, right - left + 8.0),
                rowHeight + 4.0
            },
            .startHandleRect = QRectF{
                startX - 6.0,
                y - 4.0,
                12.0,
                rowHeight + 8.0
            },
            .endHandleRect = QRectF{
                endX - 6.0,
                y - 4.0,
                12.0,
                rowHeight + 8.0
            },
            .lineStartX = startX,
            .lineEndX = endX,
            .id = trackSpan.id,
            .label = trackSpan.label
        });
    }

    return geometries;
}

std::optional<TimelineView::TimelineTrackGeometry> TimelineView::trackAt(const QPointF& position) const
{
    const auto geometries = trackGeometries();
    for (const auto& geometry : geometries)
    {
        if (geometry.hitRect.contains(position))
        {
            return geometry;
        }
    }

    return std::nullopt;
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
    requestFrame(frameForPosition(position.x()));
}

int TimelineView::preferredHeight() const
{
    constexpr int baseHeight = 84;
    constexpr int verticalPadding = 16;
    constexpr int rowHeight = 10;
    constexpr int rowGap = 2;

    if (m_trackSpans.empty())
    {
        return baseHeight;
    }

    const auto trackCount = static_cast<int>(m_trackSpans.size());
    return std::max(baseHeight, verticalPadding + trackCount * rowHeight + std::max(0, trackCount - 1) * rowGap + 16);
}

void TimelineView::updatePreferredHeight()
{
    const auto height = preferredHeight();
    setMinimumHeight(height);
    setMaximumHeight(height);
    updateGeometry();
}

void TimelineView::requestFrame(const int frameIndex)
{
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (clampedFrameIndex == m_lastRequestedFrame)
    {
        return;
    }

    m_lastRequestedFrame = clampedFrameIndex;
    emit frameRequested(clampedFrameIndex);
}

void TimelineView::updateTrimAt(const QPointF& position)
{
    if (!m_trimmedTrack.has_value())
    {
        return;
    }

    const auto frameIndex = frameForPosition(position.x());
    if (m_trimmingStart)
    {
        emit trackStartFrameRequested(m_trimmedTrack->id, frameIndex);
    }
    else
    {
        emit trackEndFrameRequested(m_trimmedTrack->id, frameIndex);
    }

    requestFrame(frameIndex);
}

void TimelineView::updateSpanDragAt(const QPointF& position)
{
    if (!m_draggedTrack.has_value())
    {
        return;
    }

    const auto currentFrame = frameForPosition(position.x());
    const auto deltaFrames = currentFrame - m_dragAnchorFrame;
    const auto incrementalDelta = deltaFrames - m_dragAccumulatedDeltaFrames;
    if (incrementalDelta == 0)
    {
        return;
    }

    emit trackSpanMoveRequested(m_draggedTrack->id, incrementalDelta);
    m_dragAccumulatedDeltaFrames = deltaFrames;
    requestFrame(currentFrame);
}
