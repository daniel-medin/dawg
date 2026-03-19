#include "ui/TimelineQuickController.h"

#include <algorithm>
#include <cmath>

#include <QToolTip>
#include <QUrl>

#include "ui/TimelineThumbnailCache.h"

namespace
{
constexpr double kMinVisibleFrameSpan = 12.0;
constexpr double kZoomStepFactor = 1.2;
constexpr double kFilmstripHeight = 42.0;

double clampedVisibleFrameSpan(const int totalFrames, const double requestedSpan)
{
    const auto maxFrameIndex = std::max(0, totalFrames - 1);
    const auto fullSpan = std::max(1.0, static_cast<double>(maxFrameIndex));
    return std::clamp(requestedSpan, std::min(kMinVisibleFrameSpan, fullSpan), fullSpan);
}

int roundedFpsFrames(const double fps)
{
    return std::max(1, static_cast<int>(std::lround(fps > 0.0 ? fps : 30.0)));
}

QString thumbnailSourceUrl(const QString& videoPath, const int frameIndex)
{
    const auto encodedPath = QString::fromLatin1(QUrl::toPercentEncoding(videoPath));
    return QStringLiteral("image://timeline-thumbnail/")
        + encodedPath
        + QLatin1Char('/')
        + QString::number(frameIndex);
}
}

TimelineQuickController::TimelineQuickController(QObject* parent)
    : QObject(parent)
{
    m_scrubRequestTimer.setSingleShot(true);
    m_scrubRequestTimer.setInterval(16);
    connect(&m_scrubRequestTimer, &QTimer::timeout, this, &TimelineQuickController::flushPendingFrameRequest);
    connect(
        &timelineThumbnailCache(),
        &TimelineThumbnailCache::thumbnailReady,
        this,
        [this](const QString& videoPath, const int frameIndex)
        {
            if (videoPath != m_videoPath)
            {
                return;
            }

            const auto sourceUrl = thumbnailSourceUrl(videoPath, frameIndex);
            if (m_thumbnailSources.value(frameIndex) == sourceUrl)
            {
                return;
            }

            m_thumbnailSources.insert(frameIndex, sourceUrl);
            refreshVisuals();
        });
}

void TimelineQuickController::clear()
{
    m_trimmedTrack.reset();
    m_draggedTrack.reset();
    m_loopStartFrame.reset();
    m_loopEndFrame.reset();
    m_loopEditFrame.reset();
    m_loopSelected = false;
    m_hoveredLoopFrame.reset();
    m_hoveredTimelineX.reset();
    m_trimmingStart = false;
    m_loopHandleDragMode = LoopHandleDragMode::None;
    m_loopDragAnchorFrame = 0;
    m_dragAnchorFrame = 0;
    m_dragAccumulatedDeltaFrames = 0;
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_fps = 0.0;
    m_horizontalZoom = 1.0;
    m_viewStartFrame = 0.0;
    m_trackSpans.clear();
    m_dragging = false;
    m_lastRequestedFrame = -1;
    m_pendingRequestedFrame = -1;
    m_cursorShape = static_cast<int>(Qt::ArrowCursor);
    m_videoPath.clear();
    m_thumbnailFrames.clear();
    m_thumbnailSources.clear();
    m_scrubRequestTimer.stop();
    timelineThumbnailCache().clear();
    QToolTip::hideText();
    refreshVisuals();
}

void TimelineQuickController::setVideoPath(const QString& videoPath)
{
    if (m_videoPath == videoPath)
    {
        return;
    }

    m_videoPath = videoPath;
    m_thumbnailFrames.clear();
    m_thumbnailSources.clear();
    if (m_videoPath.isEmpty())
    {
        timelineThumbnailCache().clear();
    }
    refreshVisuals();
}

void TimelineQuickController::setTimeline(const int totalFrames, const double fps)
{
    m_totalFrames = std::max(0, totalFrames);
    m_fps = fps > 0.0 ? fps : 30.0;
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_totalFrames - 1));
    m_horizontalZoom = 1.0;
    m_viewStartFrame = 0.0;
    if (m_totalFrames <= 0)
    {
        m_loopStartFrame.reset();
        m_loopEndFrame.reset();
        m_loopEditFrame.reset();
        m_loopSelected = false;
        m_hoveredLoopFrame.reset();
        m_hoveredTimelineX.reset();
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
    clampViewWindow();
    refreshVisuals();
}

void TimelineQuickController::setCurrentFrame(const int frameIndex)
{
    const auto clampedFrame = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (m_currentFrame == clampedFrame)
    {
        return;
    }

    m_currentFrame = clampedFrame;
    m_lastRequestedFrame = m_currentFrame;
    m_pendingRequestedFrame = -1;
    const auto previousViewStartFrame = m_viewStartFrame;
    ensureFrameVisible(m_currentFrame);
    if (std::abs(m_viewStartFrame - previousViewStartFrame) > 0.001)
    {
        refreshVisuals();
        return;
    }

    m_markerX = xForFrame(m_currentFrame);
    emit markerChanged();
}

void TimelineQuickController::setLoopRange(const std::optional<int> startFrame, const std::optional<int> endFrame)
{
    if (m_loopStartFrame == startFrame && m_loopEndFrame == endFrame)
    {
        return;
    }

    m_loopStartFrame = startFrame;
    m_loopEndFrame = endFrame;
    if (!m_loopStartFrame.has_value() && !m_loopEndFrame.has_value())
    {
        m_loopSelected = false;
    }
    refreshVisuals();
}

void TimelineQuickController::setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans)
{
    if (m_trackSpans == trackSpans)
    {
        return;
    }

    m_trackSpans = trackSpans;
    refreshVisuals();
}

void TimelineQuickController::setSeekOnClickEnabled(const bool enabled)
{
    m_seekOnClickEnabled = enabled;
}

void TimelineQuickController::setThumbnailsVisible(const bool visible)
{
    if (m_showThumbnails == visible)
    {
        return;
    }

    m_showThumbnails = visible;
    refreshVisuals();
}

void TimelineQuickController::setPlaybackActive(const bool active)
{
    if (m_playbackActive == active)
    {
        return;
    }

    const auto previousCursorShape = m_cursorShape;
    m_playbackActive = active;
    if (m_playbackActive)
    {
        m_hoveredLoopFrame.reset();
        m_hoveredTimelineX.reset();
        QToolTip::hideText();
        m_cursorShape = static_cast<int>(Qt::ArrowCursor);
    }

    refreshVisuals();
    emit playbackActiveChanged();
    if (m_cursorShape != previousCursorShape)
    {
        emit cursorShapeChanged();
    }
}

std::optional<int> TimelineQuickController::loopEditFrame() const
{
    return m_loopEditFrame;
}

std::optional<int> TimelineQuickController::loopShortcutFrame() const
{
    if (m_loopEditFrame.has_value())
    {
        return m_loopEditFrame;
    }

    return m_hoveredLoopFrame;
}

bool TimelineQuickController::hasSelectedLoopRange() const
{
    return m_loopSelected && (m_loopStartFrame.has_value() || m_loopEndFrame.has_value());
}

QVariantMap TimelineQuickController::timelineRect() const
{
    return m_timelineRect;
}

QVariantMap TimelineQuickController::filmstripRect() const
{
    return m_filmstripRect;
}

QVariantMap TimelineQuickController::loopBarRect() const
{
    return m_loopBarRect;
}

QVariantMap TimelineQuickController::trackAreaRect() const
{
    return m_trackAreaRect;
}

QVariantList TimelineQuickController::gridLines() const
{
    return m_gridLines;
}

QVariantList TimelineQuickController::thumbnailTiles() const
{
    return m_thumbnailTiles;
}

QVariantList TimelineQuickController::trackGeometries() const
{
    return m_trackGeometryMaps;
}

QVariantMap TimelineQuickController::loopRangeGeometry() const
{
    return m_loopRangeGeometryMap;
}

double TimelineQuickController::markerX() const
{
    return m_markerX;
}

bool TimelineQuickController::hasLoopIndicator() const
{
    return m_hasLoopIndicator;
}

double TimelineQuickController::loopIndicatorX() const
{
    return m_loopIndicatorX;
}

bool TimelineQuickController::hasHoverLine() const
{
    return m_hasHoverLine;
}

double TimelineQuickController::hoverX() const
{
    return m_hoverX;
}

int TimelineQuickController::cursorShape() const
{
    return m_cursorShape;
}

bool TimelineQuickController::playbackActive() const
{
    return m_playbackActive;
}

void TimelineQuickController::setViewportSize(const double width, const double height)
{
    if (std::abs(m_viewportWidth - width) < 0.5 && std::abs(m_viewportHeight - height) < 0.5)
    {
        return;
    }

    m_viewportWidth = width;
    m_viewportHeight = height;
    refreshVisuals();
}

void TimelineQuickController::handleMousePress(
    const int button,
    const double x,
    const double y,
    const int modifiers,
    const int globalX,
    const int globalY)
{
    if (m_totalFrames <= 0)
    {
        return;
    }

    const auto position = QPointF{x, y};
    const auto globalPosition = QPoint{globalX, globalY};

    if (button == static_cast<int>(Qt::RightButton))
    {
        if (const auto loopGeometry = computeLoopRangeGeometry(); loopGeometry.has_value()
            && (loopGeometry->selectionRect.contains(position)
                || loopGeometry->startHandleRect.contains(position)
                || loopGeometry->endHandleRect.contains(position)))
        {
            m_trimmedTrack.reset();
            m_draggedTrack.reset();
            m_dragging = false;
            m_loopEditFrame = frameForPosition(x);
            m_loopSelected = true;
            m_hoveredLoopFrame = m_loopEditFrame;
            m_hoveredTimelineX = x;
            refreshVisuals();
            emit loopContextMenuRequested(globalPosition);
            return;
        }

        if (const auto track = trackAt(position); track.has_value())
        {
            m_trimmedTrack.reset();
            m_draggedTrack.reset();
            m_dragging = false;
            m_loopSelected = false;
            emit trackSelected(track->id);
            emit trackContextMenuRequested(track->id, globalPosition);
            refreshVisuals();
            return;
        }

        return;
    }

    if (button != static_cast<int>(Qt::LeftButton))
    {
        return;
    }

    if (computeLoopBarRect().contains(position))
    {
        m_trimmedTrack.reset();
        m_draggedTrack.reset();
        m_dragging = false;
        m_loopEditFrame = frameForPosition(x);
        m_hoveredLoopFrame = m_loopEditFrame;
        m_hoveredTimelineX = x;
        m_loopHandleDragMode = LoopHandleDragMode::None;
        m_loopDragAnchorFrame = *m_loopEditFrame;
        m_loopSelected = false;
        if (const auto loopGeometry = computeLoopRangeGeometry(); loopGeometry.has_value())
        {
            if (m_loopStartFrame.has_value() && loopGeometry->startHandleRect.contains(position))
            {
                m_loopSelected = true;
                m_loopHandleDragMode = LoopHandleDragMode::Start;
            }
            else if (m_loopEndFrame.has_value() && loopGeometry->endHandleRect.contains(position))
            {
                m_loopSelected = true;
                m_loopHandleDragMode = LoopHandleDragMode::End;
            }
            else if (loopGeometry->selectionRect.contains(position))
            {
                m_loopSelected = true;
            }
        }
        if (m_loopHandleDragMode == LoopHandleDragMode::None)
        {
            m_loopHandleDragMode = LoopHandleDragMode::Range;
        }

        if (m_seekOnClickEnabled)
        {
            requestFrameAt(position);
        }
        if (m_loopHandleDragMode != LoopHandleDragMode::None)
        {
            updateLoopHandleDragAt(position);
        }
        refreshVisuals();
        return;
    }

    if (m_loopEditFrame.has_value())
    {
        m_loopEditFrame.reset();
    }
    m_loopSelected = false;
    m_hoveredLoopFrame.reset();
    if (computeTrackAreaRect().contains(position))
    {
        m_hoveredTimelineX = x;
    }
    else
    {
        m_hoveredTimelineX.reset();
    }

    if (const auto track = trackAt(position); track.has_value())
    {
        emit trackSelected(track->id);

        if ((modifiers & static_cast<int>(Qt::ControlModifier)) && track->hasAttachedAudio)
        {
            emit trackGainPopupRequested(track->id, globalPosition);
            refreshVisuals();
            return;
        }

        if (track->startHandleRect.contains(position))
        {
            m_trimmedTrack = track;
            m_trimmingStart = true;
            updateTrimAt(position);
            refreshVisuals();
            return;
        }

        if (track->endHandleRect.contains(position))
        {
            m_trimmedTrack = track;
            m_trimmingStart = false;
            updateTrimAt(position);
            refreshVisuals();
            return;
        }

        if (track->selected)
        {
            if (m_seekOnClickEnabled)
            {
                requestFrameAt(position);
            }
            m_draggedTrack = track;
            m_dragAnchorFrame = frameForPosition(x);
            m_dragAccumulatedDeltaFrames = 0;
            refreshVisuals();
            return;
        }
    }

    m_dragging = true;
    if (m_seekOnClickEnabled)
    {
        requestFrameAt(position);
    }
    refreshVisuals();
}

void TimelineQuickController::handleMouseDoubleClick(const int button, const double x, const double y)
{
    if (button != static_cast<int>(Qt::LeftButton) || m_totalFrames <= 0)
    {
        return;
    }

    if (const auto track = trackAt(QPointF{x, y}); track.has_value())
    {
        m_loopSelected = false;
        emit trackSelected(track->id);
        emit trackActivated(track->id);
        refreshVisuals();
    }
}

void TimelineQuickController::handleMouseMove(const double x, const double y, const int globalX, const int globalY)
{
    const auto position = QPointF{x, y};
    updateHoverState(position);
    updateCursorAndTooltip(position, QPoint{globalX, globalY});

    if (m_loopHandleDragMode != LoopHandleDragMode::None)
    {
        updateLoopHandleDragAt(position);
        return;
    }

    if (m_trimmedTrack.has_value())
    {
        updateTrimAt(position);
        return;
    }

    if (m_draggedTrack.has_value())
    {
        updateSpanDragAt(position);
        return;
    }

    if (!m_dragging || m_totalFrames <= 0)
    {
        return;
    }

    requestFrameCoalesced(frameForPosition(x));
}

void TimelineQuickController::handleMouseRelease(const int button)
{
    if (button == static_cast<int>(Qt::LeftButton))
    {
        flushPendingFrameRequest();
        m_trimmedTrack.reset();
        m_draggedTrack.reset();
        m_trimmingStart = false;
        m_loopHandleDragMode = LoopHandleDragMode::None;
        m_loopDragAnchorFrame = 0;
        m_dragAnchorFrame = 0;
        m_dragAccumulatedDeltaFrames = 0;
        m_dragging = false;
        refreshVisuals();
    }
}

void TimelineQuickController::handleWheel(
    const double x,
    const double y,
    const int angleDeltaY,
    const int modifiers,
    const int globalX,
    const int globalY)
{
    if ((modifiers & static_cast<int>(Qt::ControlModifier)) && angleDeltaY != 0)
    {
        if (const auto track = trackAt(QPointF{x, y}); track.has_value())
        {
            if (track->selected && track->hasAttachedAudio)
            {
                emit trackGainAdjustRequested(track->id, angleDeltaY, QPoint{globalX, globalY});
                return;
            }
        }
    }

    if (m_totalFrames <= 1 || angleDeltaY == 0)
    {
        return;
    }

    const auto anchorRect = computeLoopBarRect();
    const auto anchorRatio = std::clamp(
        (x - anchorRect.left()) / std::max(1.0, anchorRect.width()),
        0.0,
        1.0);
    const auto anchorFrame = m_viewStartFrame + visibleFrameSpan() * anchorRatio;
    const auto steps = static_cast<double>(angleDeltaY) / 120.0;
    const auto nextZoom = std::clamp(
        m_horizontalZoom * std::pow(kZoomStepFactor, steps),
        1.0,
        maxZoomScale());
    if (std::abs(nextZoom - m_horizontalZoom) < 0.001)
    {
        return;
    }

    m_horizontalZoom = nextZoom;
    const auto nextSpan = visibleFrameSpan();
    m_viewStartFrame = anchorFrame - nextSpan * anchorRatio;
    clampViewWindow();
    refreshVisuals();
}

void TimelineQuickController::handleHoverMove(const double x, const double y, const int globalX, const int globalY)
{
    if (m_playbackActive)
    {
        return;
    }

    const auto position = QPointF{x, y};
    updateHoverState(position);
    updateCursorAndTooltip(position, QPoint{globalX, globalY});
}

void TimelineQuickController::handleHoverLeave()
{
    if (m_playbackActive)
    {
        return;
    }

    const auto previousCursorShape = m_cursorShape;
    if (m_hoveredLoopFrame.has_value() || m_hoveredTimelineX.has_value())
    {
        m_hoveredLoopFrame.reset();
        m_hoveredTimelineX.reset();
    }
    QToolTip::hideText();
    m_cursorShape = static_cast<int>(Qt::ArrowCursor);
    refreshVisuals();
    if (m_cursorShape != previousCursorShape)
    {
        emit cursorShapeChanged();
    }
}

std::vector<TimelineQuickController::TimelineTrackGeometry> TimelineQuickController::computeTrackGeometries() const
{
    std::vector<TimelineTrackGeometry> geometries;
    geometries.reserve(m_trackSpans.size());

    if (m_trackSpans.empty())
    {
        return geometries;
    }

    const auto laneRect = computeTrackAreaRect();
    const auto trackLaneCount = laneCount();
    const auto verticalPadding = 8.0;
    const auto rowGap = trackLaneCount > 10 ? 1.0 : 2.0;
    const auto availableHeight = std::max(8.0, laneRect.height() - verticalPadding * 2.0);
    const auto rowHeight = std::max(
        3.0,
        std::min(10.0, (availableHeight - std::max(0, trackLaneCount - 1) * rowGap) / std::max(1, trackLaneCount)));

    for (const auto& trackSpan : m_trackSpans)
    {
        const auto laneIndex = std::max(0, trackSpan.laneIndex);
        const auto y = laneRect.top() + verticalPadding + (rowHeight + rowGap) * laneIndex;
        const auto startX = xForFrame(std::max(0, trackSpan.startFrame));
        const auto endX = xForFrame(std::max(trackSpan.startFrame, trackSpan.endFrame));
        const auto left = std::min(startX, endX);
        const auto right = std::max(startX, endX);

        geometries.push_back(TimelineTrackGeometry{
            .hitRect = QRectF{left - 4.0, y - 2.0, std::max(8.0, right - left + 8.0), rowHeight + 4.0},
            .startHandleRect = QRectF{startX - 6.0, y - 4.0, 12.0, rowHeight + 8.0},
            .endHandleRect = QRectF{endX - 6.0, y - 4.0, 12.0, rowHeight + 8.0},
            .lineStartX = startX,
            .lineEndX = endX,
            .id = trackSpan.id,
            .label = trackSpan.label,
            .color = trackSpan.color,
            .selected = trackSpan.isSelected,
            .hasAttachedAudio = trackSpan.hasAttachedAudio});
    }

    return geometries;
}

std::optional<TimelineQuickController::LoopRangeGeometry> TimelineQuickController::computeLoopRangeGeometry() const
{
    const auto barRect = computeLoopBarRect();
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

std::optional<TimelineQuickController::TimelineTrackGeometry> TimelineQuickController::trackAt(const QPointF& position) const
{
    const auto geometries = computeTrackGeometries();
    for (const auto& geometry : geometries)
    {
        if (geometry.hitRect.contains(position))
        {
            return geometry;
        }
    }

    return std::nullopt;
}

void TimelineQuickController::updateTrimAt(const QPointF& position)
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

void TimelineQuickController::updateLoopHandleDragAt(const QPointF& position)
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
    else if (m_loopHandleDragMode == LoopHandleDragMode::Range)
    {
        if (frameIndex == m_loopDragAnchorFrame)
        {
            refreshVisuals();
            return;
        }

        emit loopStartFrameRequested(std::min(m_loopDragAnchorFrame, frameIndex));
        emit loopEndFrameRequested(std::max(m_loopDragAnchorFrame, frameIndex));
    }

    requestFrame(frameIndex);
    refreshVisuals();
}

void TimelineQuickController::updateSpanDragAt(const QPointF& position)
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

void TimelineQuickController::updateHoverState(const QPointF& position)
{
    std::optional<int> nextHoveredLoopFrame;
    std::optional<double> nextHoveredTimelineX;
    if (computeLoopBarRect().contains(position))
    {
        nextHoveredLoopFrame = frameForPosition(position.x());
        nextHoveredTimelineX = position.x();
    }
    else if (computeTrackAreaRect().contains(position))
    {
        nextHoveredTimelineX = position.x();
    }

    if (m_hoveredLoopFrame != nextHoveredLoopFrame
        || m_hoveredTimelineX != nextHoveredTimelineX)
    {
        m_hoveredLoopFrame = nextHoveredLoopFrame;
        m_hoveredTimelineX = nextHoveredTimelineX;
        refreshVisuals();
    }
}

void TimelineQuickController::updateCursorAndTooltip(const QPointF& position, const QPoint& globalPosition)
{
    auto nextCursor = static_cast<int>(Qt::ArrowCursor);
    if (const auto loopGeometry = computeLoopRangeGeometry(); loopGeometry.has_value()
        && (loopGeometry->startHandleRect.contains(position) || loopGeometry->endHandleRect.contains(position)))
    {
        QToolTip::hideText();
        nextCursor = static_cast<int>(Qt::SizeHorCursor);
    }
    else if (computeLoopBarRect().contains(position))
    {
        QToolTip::hideText();
    }
    else if (const auto track = trackAt(position); track.has_value())
    {
        QToolTip::showText(globalPosition, track->label);
        if (track->startHandleRect.contains(position) || track->endHandleRect.contains(position))
        {
            nextCursor = static_cast<int>(Qt::SizeHorCursor);
        }
        else
        {
            nextCursor = static_cast<int>(Qt::PointingHandCursor);
        }
    }
    else
    {
        QToolTip::hideText();
    }

    if (m_cursorShape != nextCursor)
    {
        m_cursorShape = nextCursor;
        emit cursorShapeChanged();
    }
}

void TimelineQuickController::refreshVisuals()
{
    const auto previousMarkerX = m_markerX;
    const auto previousHasLoopIndicator = m_hasLoopIndicator;
    const auto previousLoopIndicatorX = m_loopIndicatorX;
    const auto previousHasHoverLine = m_hasHoverLine;
    const auto previousHoverX = m_hoverX;

    const auto timelineRect = computeTimelineRect();
    const auto filmstripRect = computeFilmstripRect();
    const auto loopRect = computeLoopBarRect();
    const auto trackRect = computeTrackAreaRect();

    m_timelineRect = rectMap(timelineRect);
    m_filmstripRect = rectMap(filmstripRect);
    m_loopBarRect = rectMap(loopRect);
    m_trackAreaRect = rectMap(trackRect);

    const auto nextThumbnailFrames = computeThumbnailFrames();
    if (m_thumbnailFrames != nextThumbnailFrames)
    {
        m_thumbnailFrames = nextThumbnailFrames;
    }
    requestThumbnailFrames(m_thumbnailFrames);

    m_thumbnailTiles.clear();
    if (!m_thumbnailFrames.isEmpty())
    {
        const auto tileCount = std::max(1, static_cast<int>(m_thumbnailFrames.size()));
        const auto tileWidth = filmstripRect.width() / static_cast<double>(tileCount);
        for (int index = 0; index < m_thumbnailFrames.size(); ++index)
        {
            const auto frameIndex = m_thumbnailFrames.at(index);
            QVariantMap tile;
            tile.insert(QStringLiteral("x"), filmstripRect.left() + tileWidth * index);
            tile.insert(QStringLiteral("y"), filmstripRect.top());
            tile.insert(QStringLiteral("width"), tileWidth);
            tile.insert(QStringLiteral("height"), filmstripRect.height());
            tile.insert(QStringLiteral("frameIndex"), frameIndex);
            tile.insert(QStringLiteral("source"), m_thumbnailSources.value(frameIndex));
            m_thumbnailTiles.push_back(tile);
        }
    }

    m_gridLines.clear();
    if (m_totalFrames > 0)
    {
        const auto stepFrames = gridStepFrames();
        const auto secondFrames = roundedFpsFrames(m_fps);
        const auto visibleEndFrame = m_viewStartFrame + visibleFrameSpan();
        const auto firstGridFrame =
            std::max(0, (static_cast<int>(std::floor(m_viewStartFrame)) / stepFrames) * stepFrames);

        for (int frame = firstGridFrame; frame <= static_cast<int>(std::ceil(visibleEndFrame)); frame += stepFrames)
        {
            const auto isMajor = (frame % secondFrames) == 0;
            if (m_playbackActive && !isMajor)
            {
                continue;
            }

            QVariantMap line;
            line.insert(QStringLiteral("x"), xForFrame(frame));
            line.insert(QStringLiteral("major"), isMajor);
            m_gridLines.push_back(line);
        }
    }

    m_trackGeometryMaps.clear();
    for (const auto& geometry : computeTrackGeometries())
    {
        QVariantMap track;
        track.insert(QStringLiteral("id"), geometry.id.toString());
        track.insert(QStringLiteral("label"), geometry.label);
        track.insert(QStringLiteral("lineStartX"), geometry.lineStartX);
        track.insert(QStringLiteral("lineEndX"), geometry.lineEndX);
        track.insert(QStringLiteral("y"), geometry.hitRect.center().y());
        track.insert(QStringLiteral("capHeight"), std::max(3.0, std::min(6.0, geometry.hitRect.height() - 1.0)));
        track.insert(QStringLiteral("color"), geometry.color);
        track.insert(QStringLiteral("selected"), geometry.selected);
        track.insert(QStringLiteral("hitRect"), rectMap(geometry.hitRect));
        track.insert(QStringLiteral("startHandleRect"), rectMap(geometry.startHandleRect));
        track.insert(QStringLiteral("endHandleRect"), rectMap(geometry.endHandleRect));
        track.insert(QStringLiteral("hasAttachedAudio"), geometry.hasAttachedAudio);
        m_trackGeometryMaps.push_back(track);
    }

    if (const auto loopGeometry = computeLoopRangeGeometry(); loopGeometry.has_value())
    {
        m_loopRangeGeometryMap = QVariantMap{
            {QStringLiteral("selectionRect"), rectMap(loopGeometry->selectionRect)},
            {QStringLiteral("startHandleRect"), rectMap(loopGeometry->startHandleRect)},
            {QStringLiteral("endHandleRect"), rectMap(loopGeometry->endHandleRect)},
            {QStringLiteral("selectionVisible"), loopGeometry->selectionRect.width() > 0.0},
            {QStringLiteral("startHandleVisible"), m_loopStartFrame.has_value()},
            {QStringLiteral("endHandleVisible"), m_loopEndFrame.has_value()},
            {QStringLiteral("selected"), m_loopSelected}
        };
    }
    else
    {
        m_loopRangeGeometryMap.clear();
    }

    m_markerX = xForFrame(m_currentFrame);
    const auto loopIndicatorFrame = loopShortcutFrame();
    m_hasLoopIndicator = loopIndicatorFrame.has_value();
    m_loopIndicatorX = m_hasLoopIndicator ? xForFrame(*loopIndicatorFrame) : 0.0;
    m_hasHoverLine = m_hoveredTimelineX.has_value();
    m_hoverX = m_hasHoverLine ? std::clamp(*m_hoveredTimelineX, loopRect.left(), loopRect.right()) : 0.0;

    emit visualsChanged();
    if (std::abs(m_markerX - previousMarkerX) > 0.001)
    {
        emit markerChanged();
    }
    if (m_hasLoopIndicator != previousHasLoopIndicator
        || std::abs(m_loopIndicatorX - previousLoopIndicatorX) > 0.001
        || m_hasHoverLine != previousHasHoverLine
        || std::abs(m_hoverX - previousHoverX) > 0.001)
    {
        emit overlayChanged();
    }
}

QRectF TimelineQuickController::computeTimelineRect() const
{
    return QRectF{
        8.0,
        8.0,
        std::max(120.0, m_viewportWidth - 16.0),
        std::max(48.0, m_viewportHeight - 16.0)
    };
}

QRectF TimelineQuickController::computeFilmstripRect() const
{
    const auto rect = computeTimelineRect();
    if (!m_showThumbnails)
    {
        return QRectF{
            rect.left() + 6.0,
            rect.top() + 6.0,
            std::max(108.0, rect.width() - 12.0),
            0.0
        };
    }

    return QRectF{
        rect.left() + 6.0,
        rect.top() + 6.0,
        std::max(108.0, rect.width() - 12.0),
        kFilmstripHeight
    };
}

QRectF TimelineQuickController::computeLoopBarRect() const
{
    const auto rect = computeTimelineRect();
    return QRectF{
        rect.left() + 6.0,
        computeFilmstripRect().bottom() + 6.0,
        std::max(108.0, rect.width() - 12.0),
        14.0
    };
}

QRectF TimelineQuickController::computeTrackAreaRect() const
{
    const auto rect = computeTimelineRect();
    const auto top = computeLoopBarRect().bottom() + 6.0;
    return QRectF{
        rect.left() + 6.0,
        top,
        std::max(108.0, rect.width() - 12.0),
        std::max(28.0, rect.bottom() - top - 6.0)
    };
}

QVector<int> TimelineQuickController::computeThumbnailFrames() const
{
    QVector<int> frameIndices;
    if (!m_showThumbnails || m_playbackActive || m_totalFrames <= 0 || m_videoPath.isEmpty())
    {
        return frameIndices;
    }

    const auto filmstripRect = computeFilmstripRect();
    const auto tileCount = std::clamp(
        static_cast<int>(std::floor(filmstripRect.width() / 92.0)),
        4,
        12);
    frameIndices.reserve(tileCount);
    for (int index = 0; index < tileCount; ++index)
    {
        const auto ratio = (static_cast<double>(index) + 0.5) / static_cast<double>(tileCount);
        frameIndices.push_back(static_cast<int>(std::lround(m_viewStartFrame + visibleFrameSpan() * ratio)));
    }
    return frameIndices;
}

void TimelineQuickController::requestThumbnailFrames(const QVector<int>& frameIndices)
{
    if (m_videoPath.isEmpty() || frameIndices.isEmpty())
    {
        return;
    }

    QVector<int> missingFrames;
    missingFrames.reserve(frameIndices.size());
    for (const auto frameIndex : frameIndices)
    {
        if (!m_thumbnailSources.contains(frameIndex) && !timelineThumbnailCache().hasThumbnail(m_videoPath, frameIndex))
        {
            missingFrames.push_back(frameIndex);
        }
        else if (!m_thumbnailSources.contains(frameIndex) && timelineThumbnailCache().hasThumbnail(m_videoPath, frameIndex))
        {
            m_thumbnailSources.insert(frameIndex, thumbnailSourceUrl(m_videoPath, frameIndex));
        }
    }

    if (!missingFrames.isEmpty())
    {
        timelineThumbnailCache().requestFrames(m_videoPath, missingFrames);
    }
}

double TimelineQuickController::visibleFrameSpan() const
{
    return visibleFrameSpanForZoom(m_horizontalZoom);
}

double TimelineQuickController::visibleFrameSpanForZoom(const double zoomScale) const
{
    if (m_totalFrames <= 1)
    {
        return 1.0;
    }

    const auto maxFrameIndex = std::max(1, m_totalFrames - 1);
    const auto fullSpan = static_cast<double>(maxFrameIndex);
    return clampedVisibleFrameSpan(m_totalFrames, fullSpan / std::max(1.0, zoomScale));
}

double TimelineQuickController::maxZoomScale() const
{
    if (m_totalFrames <= 1)
    {
        return 1.0;
    }

    const auto maxFrameIndex = std::max(1, m_totalFrames - 1);
    const auto fullSpan = static_cast<double>(maxFrameIndex);
    return std::max(1.0, fullSpan / std::min(kMinVisibleFrameSpan, fullSpan));
}

int TimelineQuickController::gridStepFrames() const
{
    const auto secondFrames = roundedFpsFrames(m_fps);
    if (m_horizontalZoom <= 1.001)
    {
        return secondFrames;
    }

    const auto pixelsPerFrame = computeLoopBarRect().width() / std::max(1.0, visibleFrameSpan());
    if (pixelsPerFrame >= 10.0)
    {
        return 1;
    }
    if (pixelsPerFrame >= 5.0)
    {
        return std::max(1, secondFrames / 4);
    }
    if (pixelsPerFrame >= 2.5)
    {
        return std::max(1, secondFrames / 2);
    }

    return secondFrames;
}

void TimelineQuickController::clampViewWindow()
{
    if (m_totalFrames <= 1)
    {
        m_viewStartFrame = 0.0;
        m_horizontalZoom = 1.0;
        return;
    }

    const auto maxFrameIndex = std::max(1, m_totalFrames - 1);
    const auto visibleSpan = visibleFrameSpan();
    const auto maxStart = std::max(0.0, static_cast<double>(maxFrameIndex) - visibleSpan);
    m_viewStartFrame = std::clamp(m_viewStartFrame, 0.0, maxStart);
}

void TimelineQuickController::ensureFrameVisible(const int frameIndex)
{
    if (m_totalFrames <= 1 || m_horizontalZoom <= 1.001)
    {
        clampViewWindow();
        return;
    }

    const auto visibleSpan = visibleFrameSpan();
    const auto leftMargin = std::max(1.0, visibleSpan * 0.15);
    const auto rightMargin = std::max(1.0, visibleSpan * 0.15);
    const auto frame = static_cast<double>(std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1)));
    if (frame < m_viewStartFrame + leftMargin)
    {
        m_viewStartFrame = frame - leftMargin;
        clampViewWindow();
        return;
    }

    const auto visibleEnd = m_viewStartFrame + visibleSpan;
    if (frame > visibleEnd - rightMargin)
    {
        m_viewStartFrame = frame - visibleSpan + rightMargin;
        clampViewWindow();
    }
}

int TimelineQuickController::frameForPosition(const double x) const
{
    if (m_totalFrames <= 1)
    {
        return 0;
    }

    const auto rect = computeLoopBarRect();
    const auto ratio = std::clamp((x - rect.left()) / rect.width(), 0.0, 1.0);
    return static_cast<int>(std::lround(m_viewStartFrame + ratio * visibleFrameSpan()));
}

double TimelineQuickController::xForFrame(const int frameIndex) const
{
    const auto rect = computeLoopBarRect();
    if (m_totalFrames <= 1)
    {
        return rect.left();
    }

    const auto ratio =
        (static_cast<double>(std::clamp(frameIndex, 0, m_totalFrames - 1)) - m_viewStartFrame)
        / std::max(1.0, visibleFrameSpan());
    return rect.left() + rect.width() * ratio;
}

int TimelineQuickController::laneCount() const
{
    int maxLaneIndex = -1;
    for (const auto& trackSpan : m_trackSpans)
    {
        maxLaneIndex = std::max(maxLaneIndex, trackSpan.laneIndex);
    }

    return std::max(0, maxLaneIndex + 1);
}

void TimelineQuickController::requestFrame(const int frameIndex)
{
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (clampedFrameIndex == m_lastRequestedFrame)
    {
        return;
    }

    m_lastRequestedFrame = clampedFrameIndex;
    emit frameRequested(clampedFrameIndex);
}

void TimelineQuickController::requestFrameCoalesced(const int frameIndex)
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

void TimelineQuickController::flushPendingFrameRequest()
{
    if (m_pendingRequestedFrame < 0)
    {
        return;
    }

    const auto frameIndex = m_pendingRequestedFrame;
    m_pendingRequestedFrame = -1;
    requestFrame(frameIndex);
}

void TimelineQuickController::requestFrameAt(const QPointF& position)
{
    requestFrame(frameForPosition(position.x()));
}

QVariantMap TimelineQuickController::rectMap(const QRectF& rect)
{
    return QVariantMap{
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()}
    };
}
