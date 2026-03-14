#include "ui/TimelineView.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(104);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_scrubRequestTimer.setSingleShot(true);
    m_scrubRequestTimer.setInterval(16);
    connect(&m_scrubRequestTimer, &QTimer::timeout, this, &TimelineView::flushPendingFrameRequest);
    updatePreferredHeight();
}

void TimelineView::clear()
{
    m_trimmedTrack.reset();
    m_draggedTrack.reset();
    m_loopStartFrame.reset();
    m_loopEndFrame.reset();
    m_loopEditFrame.reset();
    m_hoveredLoopFrame.reset();
    m_trimmingStart = false;
    m_loopHandleDragMode = LoopHandleDragMode::None;
    m_dragAnchorFrame = 0;
    m_dragAccumulatedDeltaFrames = 0;
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_fps = 0.0;
    m_trackSpans.clear();
    m_dragging = false;
    m_lastRequestedFrame = -1;
    m_pendingRequestedFrame = -1;
    m_scrubRequestTimer.stop();
    updatePreferredHeight();
    update();
}

void TimelineView::setTimeline(const int totalFrames, const double fps)
{
    m_totalFrames = std::max(0, totalFrames);
    m_fps = fps > 0.0 ? fps : 30.0;
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_totalFrames - 1));
    if (m_totalFrames <= 0)
    {
        m_loopStartFrame.reset();
        m_loopEndFrame.reset();
        m_loopEditFrame.reset();
        m_hoveredLoopFrame.reset();
    }
    else
    {
        const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
        if (m_loopStartFrame.has_value())
        {
            m_loopStartFrame = std::clamp(*m_loopStartFrame, 0, maxFrameIndex);
        }
        if (m_loopEndFrame.has_value())
        {
            m_loopEndFrame = std::clamp(*m_loopEndFrame, 0, maxFrameIndex);
        }
        if (m_loopEditFrame.has_value())
        {
            m_loopEditFrame = std::clamp(*m_loopEditFrame, 0, maxFrameIndex);
        }
        if (m_hoveredLoopFrame.has_value())
        {
            m_hoveredLoopFrame = std::clamp(*m_hoveredLoopFrame, 0, maxFrameIndex);
        }
    }
    update();
}

void TimelineView::setCurrentFrame(const int frameIndex)
{
    m_currentFrame = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    m_lastRequestedFrame = m_currentFrame;
    m_pendingRequestedFrame = -1;
    update();
}

void TimelineView::setLoopRange(const std::optional<int> startFrame, const std::optional<int> endFrame)
{
    if (m_loopStartFrame == startFrame && m_loopEndFrame == endFrame)
    {
        return;
    }

    m_loopStartFrame = startFrame;
    m_loopEndFrame = endFrame;
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

std::optional<int> TimelineView::loopEditFrame() const
{
    return m_loopEditFrame;
}

std::optional<int> TimelineView::loopShortcutFrame() const
{
    if (m_loopEditFrame.has_value())
    {
        return m_loopEditFrame;
    }

    return m_hoveredLoopFrame;
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

    const auto fullRect = timelineRect();
    const auto loopRect = loopBarRect();
    const auto laneRect = trackAreaRect();
    painter.setPen(QPen(QColor{32, 36, 42}, 1.0));
    painter.setBrush(QColor{7, 8, 10});
    painter.drawRect(fullRect);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor{11, 14, 18});
    painter.drawRoundedRect(loopRect, 4.0, 4.0);
    painter.setBrush(QColor{8, 10, 13});
    painter.drawRoundedRect(laneRect, 4.0, 4.0);

    if (m_totalFrames <= 0)
    {
        return;
    }

    if (const auto loopGeometry = loopRangeGeometry(); loopGeometry.has_value())
    {
        if (loopGeometry->selectionRect.width() > 0.0)
        {
            painter.setPen(QPen(QColor{117, 165, 228, 220}, 1.0));
            painter.setBrush(QColor{66, 110, 170, 72});
            painter.drawRoundedRect(loopGeometry->selectionRect, 3.0, 3.0);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{233, 239, 246});
        if (m_loopStartFrame.has_value())
        {
            painter.drawRoundedRect(loopGeometry->startHandleRect, 2.0, 2.0);
        }
        if (m_loopEndFrame.has_value())
        {
            painter.drawRoundedRect(loopGeometry->endHandleRect, 2.0, 2.0);
        }
    }

    const auto loopIndicatorFrame = loopShortcutFrame();
    if (loopIndicatorFrame.has_value())
    {
        const auto anchorX = xForFrame(*loopIndicatorFrame);
        painter.setPen(QPen(QColor{220, 228, 236, 176}, 1.0, Qt::DashLine));
        painter.drawLine(QPointF{anchorX, loopRect.top() + 1.0}, QPointF{anchorX, loopRect.bottom() - 1.0});
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
    painter.drawLine(QPointF{markerX, loopRect.top()}, QPointF{markerX, laneRect.bottom()});
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

    if (loopBarRect().contains(event->position()))
    {
        m_trimmedTrack.reset();
        m_draggedTrack.reset();
        m_dragging = false;
        m_loopEditFrame = frameForPosition(event->position().x());
        m_hoveredLoopFrame = m_loopEditFrame;
        m_loopHandleDragMode = LoopHandleDragMode::None;
        if (const auto loopGeometry = loopRangeGeometry(); loopGeometry.has_value())
        {
            if (m_loopStartFrame.has_value() && loopGeometry->startHandleRect.contains(event->position()))
            {
                m_loopHandleDragMode = LoopHandleDragMode::Start;
            }
            else if (m_loopEndFrame.has_value() && loopGeometry->endHandleRect.contains(event->position()))
            {
                m_loopHandleDragMode = LoopHandleDragMode::End;
            }
        }

        setFocus(Qt::MouseFocusReason);
        if (m_seekOnClickEnabled)
        {
            requestFrameAt(event->position());
        }
        if (m_loopHandleDragMode != LoopHandleDragMode::None)
        {
            updateLoopHandleDragAt(event->position());
        }
        update();
        return;
    }

    if (m_loopEditFrame.has_value())
    {
        m_loopEditFrame.reset();
        update();
    }
    m_hoveredLoopFrame.reset();

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

    std::optional<int> nextHoveredLoopFrame;
    if (loopBarRect().contains(event->position()))
    {
        nextHoveredLoopFrame = frameForPosition(event->position().x());
    }
    if (m_hoveredLoopFrame != nextHoveredLoopFrame)
    {
        m_hoveredLoopFrame = nextHoveredLoopFrame;
        update();
    }

    if (const auto loopGeometry = loopRangeGeometry(); loopGeometry.has_value()
        && (loopGeometry->startHandleRect.contains(event->position()) || loopGeometry->endHandleRect.contains(event->position())))
    {
        QToolTip::hideText();
        setCursor(Qt::SizeHorCursor);
    }
    else if (loopBarRect().contains(event->position()))
    {
        QToolTip::hideText();
        unsetCursor();
    }
    else if (const auto track = trackAt(event->position()); track.has_value())
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

    if (m_loopHandleDragMode != LoopHandleDragMode::None)
    {
        updateLoopHandleDragAt(event->position());
        return;
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

    requestFrameCoalesced(frameForPosition(event->position().x()));
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        flushPendingFrameRequest();
        m_trimmedTrack.reset();
        m_draggedTrack.reset();
        m_trimmingStart = false;
        m_loopHandleDragMode = LoopHandleDragMode::None;
        m_dragAnchorFrame = 0;
        m_dragAccumulatedDeltaFrames = 0;
        m_dragging = false;
    }
}

void TimelineView::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);

    if (m_hoveredLoopFrame.has_value())
    {
        m_hoveredLoopFrame.reset();
        update();
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

    const auto laneRect = trackAreaRect();
    const auto trackLaneCount = laneCount();
    const auto verticalPadding = 8.0;
    const auto rowGap = trackLaneCount > 10 ? 1.0 : 2.0;
    const auto availableHeight = std::max(8.0, laneRect.height() - verticalPadding * 2.0);
    const auto rowHeight = std::max(
        3.0,
        std::min(10.0, (availableHeight - std::max(0, trackLaneCount - 1) * rowGap) / std::max(1, trackLaneCount)));

    for (int index = 0; index < static_cast<int>(m_trackSpans.size()); ++index)
    {
        const auto& trackSpan = m_trackSpans[static_cast<std::size_t>(index)];
        const auto laneIndex = std::max(0, trackSpan.laneIndex);
        const auto y = laneRect.top() + verticalPadding + (rowHeight + rowGap) * laneIndex;
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

std::optional<TimelineView::LoopRangeGeometry> TimelineView::loopRangeGeometry() const
{
    const auto barRect = loopBarRect();
    if (!m_loopStartFrame.has_value() && !m_loopEndFrame.has_value())
    {
        return LoopRangeGeometry{
            .barRect = barRect,
            .selectionRect = {},
            .startHandleRect = {},
            .endHandleRect = {}
        };
    }

    const auto startFrame = m_loopStartFrame.value_or(m_loopEndFrame.value_or(0));
    const auto endFrame = m_loopEndFrame.value_or(m_loopStartFrame.value_or(0));
    const auto startX = xForFrame(startFrame);
    const auto endX = xForFrame(endFrame);
    const auto left = std::min(startX, endX);
    const auto right = std::max(startX, endX);
    const auto handleWidth = 7.0;
    const auto handleTop = barRect.top() - 1.0;
    const auto handleHeight = barRect.height() + 2.0;

    return LoopRangeGeometry{
        .barRect = barRect,
        .selectionRect = QRectF{left, barRect.top(), std::max(0.0, right - left), barRect.height()},
        .startHandleRect = QRectF{startX - handleWidth * 0.5, handleTop, handleWidth, handleHeight},
        .endHandleRect = QRectF{endX - handleWidth * 0.5, handleTop, handleWidth, handleHeight}
    };
}

int TimelineView::laneCount() const
{
    int maxLaneIndex = -1;
    for (const auto& trackSpan : m_trackSpans)
    {
        maxLaneIndex = std::max(maxLaneIndex, trackSpan.laneIndex);
    }

    return std::max(0, maxLaneIndex + 1);
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

QRectF TimelineView::loopBarRect() const
{
    const auto rect = timelineRect();
    return QRectF{
        rect.left() + 6.0,
        rect.top() + 6.0,
        std::max(108.0, rect.width() - 12.0),
        14.0
    };
}

QRectF TimelineView::trackAreaRect() const
{
    const auto rect = timelineRect();
    const auto top = loopBarRect().bottom() + 6.0;
    return QRectF{
        rect.left() + 6.0,
        top,
        std::max(108.0, rect.width() - 12.0),
        std::max(28.0, rect.bottom() - top - 6.0)
    };
}

int TimelineView::frameForPosition(const double x) const
{
    if (m_totalFrames <= 1)
    {
        return 0;
    }

    const auto rect = loopBarRect();
    const auto ratio = std::clamp((x - rect.left()) / rect.width(), 0.0, 1.0);
    return static_cast<int>(std::lround(ratio * static_cast<double>(m_totalFrames - 1)));
}

double TimelineView::xForFrame(const int frameIndex) const
{
    const auto rect = loopBarRect();
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
    constexpr int baseHeight = 104;
    constexpr int verticalPadding = 40;
    constexpr int rowHeight = 10;
    constexpr int rowGap = 2;

    if (m_trackSpans.empty())
    {
        return baseHeight;
    }

    const auto trackLaneCount = laneCount();
    return std::max(
        baseHeight,
        verticalPadding + trackLaneCount * rowHeight + std::max(0, trackLaneCount - 1) * rowGap + 16);
}

void TimelineView::updatePreferredHeight()
{
    const auto height = preferredHeight();
    setMinimumHeight(height);
    setMaximumHeight(QWIDGETSIZE_MAX);
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

void TimelineView::requestFrameCoalesced(const int frameIndex)
{
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (clampedFrameIndex == m_lastRequestedFrame)
    {
        m_pendingRequestedFrame = -1;
        return;
    }

    m_pendingRequestedFrame = clampedFrameIndex;
    if (!m_scrubRequestTimer.isActive())
    {
        m_scrubRequestTimer.start();
    }
}

void TimelineView::flushPendingFrameRequest()
{
    if (m_pendingRequestedFrame < 0)
    {
        return;
    }

    const auto frameIndex = m_pendingRequestedFrame;
    m_pendingRequestedFrame = -1;
    requestFrame(frameIndex);
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

void TimelineView::updateLoopHandleDragAt(const QPointF& position)
{
    if (m_loopHandleDragMode == LoopHandleDragMode::None)
    {
        return;
    }

    const auto frameIndex = frameForPosition(position.x());
    m_loopEditFrame = frameIndex;
    if (m_loopHandleDragMode == LoopHandleDragMode::Start)
    {
        emit loopStartFrameRequested(frameIndex);
    }
    else if (m_loopHandleDragMode == LoopHandleDragMode::End)
    {
        emit loopEndFrameRequested(frameIndex);
    }

    requestFrame(frameIndex);
    update();
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
