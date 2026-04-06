#include "app/ShellLayoutController.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr int kHandleThickness = 3;
constexpr int kMinimumMainWidth = 400;
constexpr int kMinimumAudioPoolWidth = 240;
constexpr int kMinimumCanvasHeight = 200;
constexpr int kThumbnailStripHeight = 60;
constexpr int kMinimumClipEditorHeight = 148;
constexpr int kMinimumMixHeight = 132;
}

ShellLayoutController::ShellLayoutController(QObject* parent)
    : QObject(parent)
{
}

QVariantMap ShellLayoutController::canvasRect() const
{
    return rectMap(m_canvasRect);
}

QVariantMap ShellLayoutController::timelineRect() const
{
    return rectMap(m_timelineRect);
}

QVariantMap ShellLayoutController::thumbnailRect() const
{
    return rectMap(m_thumbnailRect);
}

QVariantMap ShellLayoutController::clipEditorRect() const
{
    return rectMap(m_clipEditorRect);
}

QVariantMap ShellLayoutController::nodeEditorRect() const
{
    return rectMap(m_nodeEditorRect);
}

QVariantMap ShellLayoutController::mixRect() const
{
    return rectMap(m_mixRect);
}

QVariantMap ShellLayoutController::audioPoolRect() const
{
    return rectMap(m_audioPoolRect);
}

QVariantMap ShellLayoutController::timelineHandleRect() const
{
    return rectMap(m_timelineHandleRect);
}

QVariantMap ShellLayoutController::clipEditorHandleRect() const
{
    return rectMap(m_clipEditorHandleRect);
}

QVariantMap ShellLayoutController::nodeEditorHandleRect() const
{
    return rectMap(m_nodeEditorHandleRect);
}

QVariantMap ShellLayoutController::mixHandleRect() const
{
    return rectMap(m_mixHandleRect);
}

QVariantMap ShellLayoutController::audioPoolHandleRect() const
{
    return rectMap(m_audioPoolHandleRect);
}

int ShellLayoutController::handleThickness() const
{
    return kHandleThickness;
}

QRect ShellLayoutController::canvasPanelRect() const
{
    return m_canvasRect;
}

QRect ShellLayoutController::timelinePanelRect() const
{
    return m_timelineRect;
}

QRect ShellLayoutController::thumbnailPanelRect() const
{
    return m_thumbnailRect;
}

QRect ShellLayoutController::clipEditorPanelRect() const
{
    return m_clipEditorRect;
}

QRect ShellLayoutController::nodeEditorPanelRect() const
{
    return m_nodeEditorRect;
}

QRect ShellLayoutController::mixPanelRect() const
{
    return m_mixRect;
}

QRect ShellLayoutController::audioPoolPanelRect() const
{
    return m_audioPoolRect;
}

bool ShellLayoutController::timelineVisible() const
{
    return m_timelineVisible;
}

bool ShellLayoutController::thumbnailsVisible() const
{
    return m_thumbnailsVisible;
}

bool ShellLayoutController::clipEditorVisible() const
{
    return m_clipEditorVisible;
}

bool ShellLayoutController::nodeEditorVisible() const
{
    return m_nodeEditorVisible;
}

bool ShellLayoutController::mixVisible() const
{
    return m_mixVisible;
}

bool ShellLayoutController::audioPoolVisible() const
{
    return m_audioPoolVisible;
}

void ShellLayoutController::setViewportSize(const int width, const int height)
{
    const auto clampedWidth = std::max(0, width);
    const auto clampedHeight = std::max(0, height);
    if (m_viewportWidth == clampedWidth && m_viewportHeight == clampedHeight)
    {
        return;
    }

    m_viewportWidth = clampedWidth;
    m_viewportHeight = clampedHeight;
    recomputeLayout();
}

void ShellLayoutController::setVideoDetached(const bool detached)
{
    if (m_videoDetached == detached)
    {
        return;
    }

    m_videoDetached = detached;
    recomputeLayout();
}

void ShellLayoutController::setThumbnailsVisible(const bool visible)
{
    if (m_thumbnailsVisible == visible)
    {
        return;
    }

    m_thumbnailsVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setTimelineVisible(const bool visible)
{
    if (m_timelineVisible == visible)
    {
        return;
    }

    m_timelineVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setClipEditorVisible(const bool visible)
{
    if (m_clipEditorVisible == visible)
    {
        return;
    }

    m_clipEditorVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setNodeEditorVisible(const bool visible)
{
    if (m_nodeEditorVisible == visible)
    {
        return;
    }

    m_nodeEditorVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setMixVisible(const bool visible)
{
    if (m_mixVisible == visible)
    {
        return;
    }

    m_mixVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setAudioPoolVisible(const bool visible)
{
    if (m_audioPoolVisible == visible)
    {
        return;
    }

    m_audioPoolVisible = visible;
    recomputeLayout();
}

void ShellLayoutController::setPreferredSizes(
    const int audioPoolWidth,
    const int timelineHeight,
    const int clipEditorHeight,
    const int nodeEditorHeight,
    const int mixHeight)
{
    const auto nextAudioPoolWidth = std::max(kMinimumAudioPoolWidth, audioPoolWidth);
    const auto nextTimelineHeight = std::max(m_timelineMinimumHeight, timelineHeight);
    const auto nextClipEditorHeight = std::max(kMinimumClipEditorHeight, clipEditorHeight);
    const auto nextNodeEditorHeight = std::max(kMinimumClipEditorHeight, nodeEditorHeight);
    const auto nextMixHeight = std::max(kMinimumMixHeight, mixHeight);
    if (m_audioPoolPreferredWidth == nextAudioPoolWidth
        && m_timelinePreferredHeight == nextTimelineHeight
        && m_clipEditorPreferredHeight == nextClipEditorHeight
        && m_nodeEditorPreferredHeight == nextNodeEditorHeight
        && m_mixPreferredHeight == nextMixHeight)
    {
        return;
    }

    m_audioPoolPreferredWidth = nextAudioPoolWidth;
    m_timelinePreferredHeight = nextTimelineHeight;
    m_clipEditorPreferredHeight = nextClipEditorHeight;
    m_nodeEditorPreferredHeight = nextNodeEditorHeight;
    m_mixPreferredHeight = nextMixHeight;
    recomputeLayout();
}

void ShellLayoutController::setTimelineMinimumHeight(const int height)
{
    const auto nextHeight = std::max(96, height);
    if (m_timelineMinimumHeight == nextHeight)
    {
        return;
    }

    m_timelineMinimumHeight = nextHeight;
    m_timelinePreferredHeight = std::max(m_timelinePreferredHeight, m_timelineMinimumHeight);
    recomputeLayout();
}

void ShellLayoutController::beginResize(const QString& handleKey, const double x, const double y)
{
    if (handleKey == QStringLiteral("audioPool"))
    {
        m_activeHandle = ActiveHandle::AudioPool;
    }
    else if (handleKey == QStringLiteral("timeline"))
    {
        m_activeHandle = ActiveHandle::Timeline;
    }
    else if (handleKey == QStringLiteral("clipEditor"))
    {
        m_activeHandle = ActiveHandle::ClipEditor;
    }
    else if (handleKey == QStringLiteral("nodeEditor"))
    {
        m_activeHandle = ActiveHandle::NodeEditor;
    }
    else if (handleKey == QStringLiteral("mix"))
    {
        m_activeHandle = ActiveHandle::Mix;
    }
    else
    {
        m_activeHandle = ActiveHandle::None;
    }

    m_resizeAnchorX = static_cast<int>(std::lround(x));
    m_resizeAnchorY = static_cast<int>(std::lround(y));
    m_resizeStartAudioPoolWidth = m_audioPoolPreferredWidth;
    m_resizeStartTimelineHeight = m_timelinePreferredHeight;
    m_resizeStartClipEditorHeight = m_clipEditorPreferredHeight;
    m_resizeStartNodeEditorHeight = m_nodeEditorPreferredHeight;
    m_resizeStartMixHeight = m_mixPreferredHeight;
}

void ShellLayoutController::updateResize(const double x, const double y)
{
    if (m_activeHandle == ActiveHandle::None)
    {
        return;
    }

    const auto previousAudioPoolWidth = m_audioPoolPreferredWidth;
    const auto previousTimelineHeight = m_timelinePreferredHeight;
    const auto previousClipEditorHeight = m_clipEditorPreferredHeight;
    const auto previousNodeEditorHeight = m_nodeEditorPreferredHeight;
    const auto previousMixHeight = m_mixPreferredHeight;
    const auto currentX = static_cast<int>(std::lround(x));
    const auto currentY = static_cast<int>(std::lround(y));

    switch (m_activeHandle)
    {
    case ActiveHandle::AudioPool:
        m_audioPoolPreferredWidth = std::max(
            kMinimumAudioPoolWidth,
            m_resizeStartAudioPoolWidth + (m_resizeAnchorX - currentX));
        break;
    case ActiveHandle::Timeline:
        m_timelinePreferredHeight = std::max(
            m_timelineMinimumHeight,
            m_resizeStartTimelineHeight + (m_resizeAnchorY - currentY));
        break;
    case ActiveHandle::ClipEditor:
        m_clipEditorPreferredHeight = std::max(
            kMinimumClipEditorHeight,
            m_resizeStartClipEditorHeight + (m_resizeAnchorY - currentY));
        break;
    case ActiveHandle::NodeEditor:
        m_nodeEditorPreferredHeight = std::max(
            kMinimumClipEditorHeight,
            m_resizeStartNodeEditorHeight + (m_resizeAnchorY - currentY));
        break;
    case ActiveHandle::Mix:
        m_mixPreferredHeight = std::max(
            kMinimumMixHeight,
            m_resizeStartMixHeight + (m_resizeAnchorY - currentY));
        break;
    case ActiveHandle::None:
        break;
    }

    recomputeLayout();
    emitPreferredSizesIfNeeded(
        previousAudioPoolWidth,
        previousTimelineHeight,
        previousClipEditorHeight,
        previousNodeEditorHeight,
        previousMixHeight);
}

void ShellLayoutController::endResize()
{
    m_activeHandle = ActiveHandle::None;
}

void ShellLayoutController::recomputeLayout()
{
    const auto previousCanvasRect = m_canvasRect;
    const auto previousThumbnailRect = m_thumbnailRect;
    const auto previousTimelineRect = m_timelineRect;
    const auto previousClipEditorRect = m_clipEditorRect;
    const auto previousNodeEditorRect = m_nodeEditorRect;
    const auto previousMixRect = m_mixRect;
    const auto previousAudioPoolRect = m_audioPoolRect;
    const auto previousTimelineHandleRect = m_timelineHandleRect;
    const auto previousClipEditorHandleRect = m_clipEditorHandleRect;
    const auto previousNodeEditorHandleRect = m_nodeEditorHandleRect;
    const auto previousMixHandleRect = m_mixHandleRect;
    const auto previousAudioPoolHandleRect = m_audioPoolHandleRect;

    m_canvasRect = {};
    m_thumbnailRect = {};
    m_timelineRect = {};
    m_clipEditorRect = {};
    m_nodeEditorRect = {};
    m_mixRect = {};
    m_audioPoolRect = {};
    m_timelineHandleRect = {};
    m_clipEditorHandleRect = {};
    m_nodeEditorHandleRect = {};
    m_mixHandleRect = {};
    m_audioPoolHandleRect = {};

    const auto viewportWidth = std::max(0, m_viewportWidth);
    const auto viewportHeight = std::max(0, m_viewportHeight);
    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
        if (previousCanvasRect != m_canvasRect
            || previousThumbnailRect != m_thumbnailRect
            || previousTimelineRect != m_timelineRect
            || previousClipEditorRect != m_clipEditorRect
            || previousNodeEditorRect != m_nodeEditorRect
            || previousMixRect != m_mixRect
            || previousAudioPoolRect != m_audioPoolRect
            || previousTimelineHandleRect != m_timelineHandleRect
            || previousClipEditorHandleRect != m_clipEditorHandleRect
            || previousNodeEditorHandleRect != m_nodeEditorHandleRect
            || previousMixHandleRect != m_mixHandleRect
            || previousAudioPoolHandleRect != m_audioPoolHandleRect)
        {
            emit layoutChanged();
        }
        return;
    }

    const auto audioPoolVisible = m_audioPoolVisible;
    const auto minimumMainWidth = std::min(viewportWidth, kMinimumMainWidth);
    const auto maximumPoolWidth = std::max(
        kMinimumAudioPoolWidth,
        viewportWidth - minimumMainWidth - (audioPoolVisible ? kHandleThickness : 0));
    const auto audioPoolWidth = audioPoolVisible
        ? std::clamp(m_audioPoolPreferredWidth, kMinimumAudioPoolWidth, std::max(kMinimumAudioPoolWidth, maximumPoolWidth))
        : 0;
    const auto mainAreaWidth = audioPoolVisible
        ? std::max(0, viewportWidth - audioPoolWidth - kHandleThickness)
        : viewportWidth;
    if (audioPoolVisible)
    {
        m_audioPoolHandleRect = QRect(mainAreaWidth, 0, kHandleThickness, viewportHeight);
        m_audioPoolRect = QRect(mainAreaWidth + kHandleThickness, 0, audioPoolWidth, viewportHeight);
    }

    struct FixedPanel
    {
        bool visible = false;
        int preferred = 0;
        int minimum = 0;
        int assigned = 0;
    };

    std::array<FixedPanel, 5> panels{{
        FixedPanel{m_thumbnailsVisible, kThumbnailStripHeight, kThumbnailStripHeight, 0},
        FixedPanel{m_timelineVisible, m_timelinePreferredHeight, m_timelineMinimumHeight, 0},
        FixedPanel{m_clipEditorVisible, m_clipEditorPreferredHeight, kMinimumClipEditorHeight, 0},
        FixedPanel{m_nodeEditorVisible, m_nodeEditorPreferredHeight, kMinimumClipEditorHeight, 0},
        FixedPanel{m_mixVisible, m_mixPreferredHeight, kMinimumMixHeight, 0},
    }};

    const auto canvasVisible = !m_videoDetached;
    int visibleItemCount = canvasVisible ? 1 : 0;
    for (const auto& panel : panels)
    {
        if (panel.visible)
        {
            ++visibleItemCount;
        }
    }
    const auto handleCount = std::max(0, visibleItemCount - 1);
    const auto handleSpace = handleCount * kHandleThickness;
    const auto availableFixedHeight = std::max(0, viewportHeight - handleSpace - (canvasVisible ? kMinimumCanvasHeight : 0));

    int assignedFixedHeight = 0;
    for (auto& panel : panels)
    {
        if (!panel.visible)
        {
            continue;
        }

        panel.assigned = std::max(panel.minimum, panel.preferred);
        assignedFixedHeight += panel.assigned;
    }

    int overflow = assignedFixedHeight - availableFixedHeight;
    while (overflow > 0)
    {
        bool reducedAny = false;
        for (auto& panel : panels)
        {
            if (!panel.visible || panel.assigned <= panel.minimum)
            {
                continue;
            }

            --panel.assigned;
            --overflow;
            reducedAny = true;
            if (overflow <= 0)
            {
                break;
            }
        }

        if (!reducedAny)
        {
            break;
        }
    }

    // When the window becomes smaller than the combined panel minimums, keep
    // compressing the lower panels instead of letting the bottom of the layout
    // spill past the viewport.
    while (overflow > 0)
    {
        bool reducedAny = false;
        for (auto it = panels.rbegin(); it != panels.rend(); ++it)
        {
            auto& panel = *it;
            if (!panel.visible || panel.assigned <= 0)
            {
                continue;
            }

            --panel.assigned;
            --overflow;
            reducedAny = true;
            if (overflow <= 0)
            {
                break;
            }
        }

        if (!reducedAny)
        {
            break;
        }
    }

    const auto remainingHeight = std::max(0, viewportHeight - handleSpace - assignedFixedHeight);
    const auto canvasHeight = canvasVisible ? remainingHeight : 0;

    int currentY = 0;
    bool hasPreviousItem = false;
    if (canvasVisible)
    {
        m_canvasRect = QRect(0, currentY, mainAreaWidth, canvasHeight);
        currentY += canvasHeight;
        hasPreviousItem = true;
    }

    auto placePanel = [&](const FixedPanel& panel, QRect& rect, QRect& handleRect)
    {
        if (!panel.visible)
        {
            rect = {};
            handleRect = {};
            return;
        }

        if (hasPreviousItem)
        {
            handleRect = QRect(0, currentY, mainAreaWidth, kHandleThickness);
            currentY += kHandleThickness;
        }

        rect = QRect(0, currentY, mainAreaWidth, panel.assigned);
        currentY += panel.assigned;
        hasPreviousItem = true;
    };

    if (panels[0].visible)
    {
        if (hasPreviousItem)
        {
            currentY += kHandleThickness;
        }
        m_thumbnailRect = QRect(0, currentY, mainAreaWidth, panels[0].assigned);
        currentY += panels[0].assigned;
        hasPreviousItem = true;
    }
    placePanel(panels[1], m_timelineRect, m_timelineHandleRect);
    placePanel(panels[2], m_clipEditorRect, m_clipEditorHandleRect);
    placePanel(panels[3], m_nodeEditorRect, m_nodeEditorHandleRect);
    placePanel(panels[4], m_mixRect, m_mixHandleRect);

    if (previousCanvasRect != m_canvasRect
        || previousThumbnailRect != m_thumbnailRect
        || previousTimelineRect != m_timelineRect
        || previousClipEditorRect != m_clipEditorRect
        || previousNodeEditorRect != m_nodeEditorRect
        || previousMixRect != m_mixRect
        || previousAudioPoolRect != m_audioPoolRect
        || previousTimelineHandleRect != m_timelineHandleRect
        || previousClipEditorHandleRect != m_clipEditorHandleRect
        || previousNodeEditorHandleRect != m_nodeEditorHandleRect
        || previousMixHandleRect != m_mixHandleRect
        || previousAudioPoolHandleRect != m_audioPoolHandleRect)
    {
        emit layoutChanged();
    }
}

QVariantMap ShellLayoutController::rectMap(const QRect& rect)
{
    QVariantMap map;
    map.insert(QStringLiteral("x"), rect.x());
    map.insert(QStringLiteral("y"), rect.y());
    map.insert(QStringLiteral("width"), rect.width());
    map.insert(QStringLiteral("height"), rect.height());
    map.insert(QStringLiteral("visible"), !rect.isEmpty());
    return map;
}

void ShellLayoutController::emitPreferredSizesIfNeeded(
    const int previousAudioPoolWidth,
    const int previousTimelineHeight,
    const int previousClipEditorHeight,
    const int previousNodeEditorHeight,
    const int previousMixHeight)
{
    if (previousAudioPoolWidth == m_audioPoolPreferredWidth
        && previousTimelineHeight == m_timelinePreferredHeight
        && previousClipEditorHeight == m_clipEditorPreferredHeight
        && previousNodeEditorHeight == m_nodeEditorPreferredHeight
        && previousMixHeight == m_mixPreferredHeight)
    {
        return;
    }

    emit preferredSizesChanged(
        m_audioPoolPreferredWidth,
        m_timelinePreferredHeight,
        m_clipEditorPreferredHeight,
        m_nodeEditorPreferredHeight,
        m_mixPreferredHeight);
}
