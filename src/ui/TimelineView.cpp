#include "ui/TimelineView.h"

#include <algorithm>

#include <QDebug>
#include <QQmlContext>
#include <QQuickView>
#include <QVBoxLayout>

#include "ui/TimelineQuickController.h"

namespace
{
QUrl timelineSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/TimelineScene.qml"));
}

void ensureTimelineQuickTypesRegistered()
{
    static const bool registered = true;
    Q_UNUSED(registered);
}
}

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
    , m_timelineController(new TimelineQuickController(this))
{
    ensureTimelineQuickTypesRegistered();

    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(104);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_quickView = new QQuickView();
    m_quickView->setColor(Qt::transparent);
    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_quickView->rootContext()->setContextProperty(QStringLiteral("timelineController"), m_timelineController);
    connect(m_quickView, &QQuickView::statusChanged, this, [this]()
    {
        handleStatusChanged();
    });

    m_container = QWidget::createWindowContainer(m_quickView, this);
    m_container->setFocusPolicy(Qt::StrongFocus);
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_container, 1);

    connect(m_timelineController, &TimelineQuickController::frameRequested, this, &TimelineView::frameRequested);
    connect(m_timelineController, &TimelineQuickController::loopStartFrameRequested, this, &TimelineView::loopStartFrameRequested);
    connect(m_timelineController, &TimelineQuickController::loopEndFrameRequested, this, &TimelineView::loopEndFrameRequested);
    connect(m_timelineController, &TimelineQuickController::trackSelected, this, &TimelineView::trackSelected);
    connect(m_timelineController, &TimelineQuickController::trackActivated, this, &TimelineView::trackActivated);
    connect(m_timelineController, &TimelineQuickController::trackStartFrameRequested, this, &TimelineView::trackStartFrameRequested);
    connect(m_timelineController, &TimelineQuickController::trackEndFrameRequested, this, &TimelineView::trackEndFrameRequested);
    connect(m_timelineController, &TimelineQuickController::trackSpanMoveRequested, this, &TimelineView::trackSpanMoveRequested);
    connect(m_timelineController, &TimelineQuickController::trackContextMenuRequested, this, &TimelineView::trackContextMenuRequested);
    connect(m_timelineController, &TimelineQuickController::trackGainAdjustRequested, this, &TimelineView::trackGainAdjustRequested);
    connect(m_timelineController, &TimelineQuickController::trackGainPopupRequested, this, &TimelineView::trackGainPopupRequested);
    connect(m_timelineController, &TimelineQuickController::loopContextMenuRequested, this, &TimelineView::loopContextMenuRequested);
    m_quickView->setSource(timelineSceneUrl());
    updatePreferredHeight();
    syncController();
}

void TimelineView::clear()
{
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_fps = 0.0;
    m_loopStartFrame.reset();
    m_loopEndFrame.reset();
    m_trackSpans.clear();
    if (m_timelineController)
    {
        m_timelineController->clear();
    }
    updatePreferredHeight();
}

void TimelineView::setTimeline(const int totalFrames, const double fps)
{
    m_totalFrames = std::max(0, totalFrames);
    m_fps = fps > 0.0 ? fps : 30.0;
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_totalFrames - 1));
    if (m_timelineController)
    {
        m_timelineController->setTimeline(m_totalFrames, m_fps);
    }
}

void TimelineView::setCurrentFrame(const int frameIndex)
{
    if (m_currentFrame == frameIndex)
    {
        return;
    }

    m_currentFrame = frameIndex;
    if (m_timelineController)
    {
        m_timelineController->setCurrentFrame(frameIndex);
    }
}

void TimelineView::setLoopRange(const std::optional<int> startFrame, const std::optional<int> endFrame)
{
    m_loopStartFrame = startFrame;
    m_loopEndFrame = endFrame;
    if (m_timelineController)
    {
        m_timelineController->setLoopRange(startFrame, endFrame);
    }
}

void TimelineView::setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans)
{
    if (m_trackSpans == trackSpans)
    {
        return;
    }

    m_trackSpans = trackSpans;
    if (m_timelineController)
    {
        m_timelineController->setTrackSpans(trackSpans);
    }
    updatePreferredHeight();
}

void TimelineView::setSeekOnClickEnabled(const bool enabled)
{
    m_seekOnClickEnabled = enabled;
    if (m_timelineController)
    {
        m_timelineController->setSeekOnClickEnabled(enabled);
    }
}

std::optional<int> TimelineView::loopEditFrame() const
{
    return m_timelineController ? m_timelineController->loopEditFrame() : std::nullopt;
}

std::optional<int> TimelineView::loopShortcutFrame() const
{
    return m_timelineController ? m_timelineController->loopShortcutFrame() : std::nullopt;
}

bool TimelineView::hasSelectedLoopRange() const
{
    return m_timelineController && m_timelineController->hasSelectedLoopRange();
}

QSize TimelineView::sizeHint() const
{
    return QSize{640, preferredHeight()};
}

QSize TimelineView::minimumSizeHint() const
{
    return QSize{320, preferredHeight()};
}

void TimelineView::handleStatusChanged()
{
    if (!m_quickView)
    {
        return;
    }

    if (m_quickView->status() == QQuickView::Error)
    {
        for (const auto& error : m_quickView->errors())
        {
            qWarning() << "Timeline QML error:" << error.toString();
        }
    }

    if (m_quickView->status() == QQuickView::Ready)
    {
        syncController();
    }
}

void TimelineView::syncController()
{
    if (!m_timelineController)
    {
        return;
    }

    m_timelineController->setSeekOnClickEnabled(m_seekOnClickEnabled);
    m_timelineController->setTimeline(m_totalFrames, m_fps);
    m_timelineController->setTrackSpans(m_trackSpans);
    m_timelineController->setLoopRange(m_loopStartFrame, m_loopEndFrame);
    m_timelineController->setCurrentFrame(m_currentFrame);
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
