#include "ui/ThumbnailStripQuickController.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QRectF>
#include <QUrl>

namespace
{
constexpr double kPanelInset = 8.0;
constexpr double kMinimumStripWidth = 120.0;
constexpr double kMinimumStripHeight = 28.0;

double clampedVisibleFrameSpan(const int totalFrames, const double requestedSpan)
{
    const auto maxFrameIndex = std::max(0, totalFrames - 1);
    const auto fullSpan = std::max(1.0, static_cast<double>(maxFrameIndex));
    return std::clamp(requestedSpan, 1.0, fullSpan);
}

}

ThumbnailStripQuickController::ThumbnailStripQuickController(QObject* parent)
    : QObject(parent)
{
    m_scrubRequestTimer.setSingleShot(true);
    m_scrubRequestTimer.setInterval(16);
    connect(&m_scrubRequestTimer, &QTimer::timeout, this, &ThumbnailStripQuickController::flushPendingFrameRequest);
}

void ThumbnailStripQuickController::clear()
{
    m_totalFrames = 0;
    m_currentFrame = 0;
    m_currentFramePosition = 0.0;
    m_fps = 0.0;
    m_viewStartFrame = 0.0;
    m_visibleFrameSpan = 1.0;
    m_dragging = false;
    m_lastRequestedFrame = -1;
    m_pendingRequestedFrame = -1;
    m_projectRootPath.clear();
    m_videoPath.clear();
    m_thumbnailFrames.clear();
    m_thumbnailManifest.reset();
    m_selectedNodeStartFrame.reset();
    m_selectedNodeEndFrame.reset();
    m_hasSelectedNodeRange = false;
    m_selectedNodeRangeX = 0.0;
    m_selectedNodeRangeWidth = 0.0;
    m_hoveredStripX.reset();
    m_scrubRequestTimer.stop();
    refreshVisuals();
}

void ThumbnailStripQuickController::setProjectRootPath(const QString& projectRootPath)
{
    const auto cleanedPath = QDir::cleanPath(QDir::fromNativeSeparators(projectRootPath));
    if (m_projectRootPath == cleanedPath)
    {
        return;
    }

    m_projectRootPath = cleanedPath;
    reloadThumbnailManifest();
    refreshVisuals();
}

void ThumbnailStripQuickController::setVideoPath(const QString& videoPath)
{
    if (m_videoPath == videoPath)
    {
        return;
    }

    m_videoPath = videoPath;
    m_thumbnailFrames.clear();
    reloadThumbnailManifest();
    refreshVisuals();
}

void ThumbnailStripQuickController::setTimeline(const int totalFrames, const double fps)
{
    m_totalFrames = std::max(0, totalFrames);
    m_fps = fps > 0.0 ? fps : 30.0;
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, m_totalFrames - 1));
    m_currentFramePosition = std::clamp(
        m_currentFramePosition,
        0.0,
        static_cast<double>(std::max(0, m_totalFrames - 1)));
    if (m_totalFrames <= 1)
    {
        m_viewStartFrame = 0.0;
        m_visibleFrameSpan = 1.0;
    }
    else
    {
        m_visibleFrameSpan = clampedVisibleFrameSpan(m_totalFrames, m_visibleFrameSpan);
        clampViewWindow();
    }
    refreshVisuals();
}

void ThumbnailStripQuickController::setViewWindow(const double startFrame, const double visibleFrameSpan)
{
    const auto previousStart = m_viewStartFrame;
    const auto previousSpan = m_visibleFrameSpan;
    m_viewStartFrame = std::max(0.0, startFrame);
    m_visibleFrameSpan = m_totalFrames <= 1
        ? 1.0
        : clampedVisibleFrameSpan(m_totalFrames, visibleFrameSpan);
    clampViewWindow();
    if (std::abs(m_viewStartFrame - previousStart) < 0.001
        && std::abs(m_visibleFrameSpan - previousSpan) < 0.001)
    {
        return;
    }

    refreshVisuals();
}

void ThumbnailStripQuickController::setCurrentFrame(const int frameIndex)
{
    const auto clampedFrame = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (m_currentFrame == clampedFrame)
    {
        return;
    }

    const auto previousMarkerX = m_markerX;
    m_currentFrame = clampedFrame;
    m_currentFramePosition = static_cast<double>(clampedFrame);
    m_lastRequestedFrame = m_currentFrame;
    m_pendingRequestedFrame = -1;
    m_markerX = xForFramePosition(m_currentFramePosition);
    if (std::abs(m_markerX - previousMarkerX) > 0.001)
    {
        emit markerChanged();
    }
}

void ThumbnailStripQuickController::setCurrentFramePosition(const double framePosition)
{
    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto clampedPosition = std::clamp(framePosition, 0.0, static_cast<double>(maxFrameIndex));
    if (std::abs(m_currentFramePosition - clampedPosition) < 0.001)
    {
        return;
    }

    const auto previousMarkerX = m_markerX;
    m_currentFramePosition = clampedPosition;
    m_currentFrame = std::clamp(
        static_cast<int>(std::lround(clampedPosition)),
        0,
        maxFrameIndex);
    m_markerX = xForFramePosition(m_currentFramePosition);
    if (std::abs(m_markerX - previousMarkerX) > 0.001)
    {
        emit markerChanged();
    }
}

void ThumbnailStripQuickController::setSelectedNodeRange(
    const std::optional<int> startFrame,
    const std::optional<int> endFrame)
{
    const auto normalizedStart = startFrame.has_value()
        ? std::optional<int>{std::max(0, *startFrame)}
        : std::nullopt;
    const auto normalizedEnd = endFrame.has_value()
        ? std::optional<int>{std::max(0, *endFrame)}
        : std::nullopt;
    if (m_selectedNodeStartFrame == normalizedStart && m_selectedNodeEndFrame == normalizedEnd)
    {
        return;
    }

    m_selectedNodeStartFrame = normalizedStart;
    m_selectedNodeEndFrame = normalizedEnd;
    refreshVisuals();
}

void ThumbnailStripQuickController::setPlaybackActive(const bool active)
{
    if (m_playbackActive == active)
    {
        return;
    }

    m_playbackActive = active;
    if (m_playbackActive)
    {
        m_hoveredStripX.reset();
    }
    refreshVisuals();
    emit playbackActiveChanged();
}

QVariantMap ThumbnailStripQuickController::stripRect() const
{
    return m_stripRect;
}

QVariantList ThumbnailStripQuickController::thumbnailTiles() const
{
    return m_thumbnailTiles;
}

double ThumbnailStripQuickController::markerX() const
{
    return m_markerX;
}

bool ThumbnailStripQuickController::hasSelectedNodeRange() const
{
    return m_hasSelectedNodeRange;
}

double ThumbnailStripQuickController::selectedNodeRangeX() const
{
    return m_selectedNodeRangeX;
}

double ThumbnailStripQuickController::selectedNodeRangeWidth() const
{
    return m_selectedNodeRangeWidth;
}

bool ThumbnailStripQuickController::hasHoverLine() const
{
    return m_hasHoverLine;
}

double ThumbnailStripQuickController::hoverX() const
{
    return m_hoverX;
}

bool ThumbnailStripQuickController::playbackActive() const
{
    return m_playbackActive;
}

bool ThumbnailStripQuickController::hasThumbnailManifest() const
{
    return m_thumbnailManifest.has_value();
}

void ThumbnailStripQuickController::setViewportSize(const double width, const double height)
{
    if (std::abs(m_viewportWidth - width) < 0.5 && std::abs(m_viewportHeight - height) < 0.5)
    {
        return;
    }

    m_viewportWidth = width;
    m_viewportHeight = height;
    refreshVisuals();
}

void ThumbnailStripQuickController::handleMousePress(const int button, const double x, const double y)
{
    if (button != static_cast<int>(Qt::LeftButton) || m_totalFrames <= 0)
    {
        return;
    }

    const auto strip = computeStripRect();
    if (!strip.contains(QPointF{x, y}))
    {
        return;
    }

    m_dragging = true;
    updateHover(x, y);
    requestFrameAt(x);
}

void ThumbnailStripQuickController::handleMouseMove(const double x, const double y)
{
    updateHover(x, y);
    if (!m_dragging || m_totalFrames <= 0)
    {
        return;
    }

    requestFrameCoalesced(frameForPosition(x));
}

void ThumbnailStripQuickController::handleMouseRelease(const int button)
{
    if (button != static_cast<int>(Qt::LeftButton))
    {
        return;
    }

    flushPendingFrameRequest();
    m_dragging = false;
}

void ThumbnailStripQuickController::handleHoverMove(const double x, const double y)
{
    updateHover(x, y);
}

void ThumbnailStripQuickController::handleHoverLeave()
{
    if (!m_hoveredStripX.has_value() && !m_hasHoverLine)
    {
        return;
    }

    m_hoveredStripX.reset();
    refreshVisuals();
}

void ThumbnailStripQuickController::refreshVisuals()
{
    const auto previousMarkerX = m_markerX;
    const auto previousHasSelectedNodeRange = m_hasSelectedNodeRange;
    const auto previousSelectedNodeRangeX = m_selectedNodeRangeX;
    const auto previousSelectedNodeRangeWidth = m_selectedNodeRangeWidth;
    const auto previousHasHoverLine = m_hasHoverLine;
    const auto previousHoverX = m_hoverX;

    const auto strip = computeStripRect();
    m_stripRect = rectMap(strip);

    const auto nextThumbnailFrames = computeThumbnailFrames();
    if (m_thumbnailFrames != nextThumbnailFrames)
    {
        m_thumbnailFrames = nextThumbnailFrames;
    }

    m_thumbnailTiles.clear();
    if (!m_thumbnailFrames.isEmpty())
    {
        const auto tileCount = std::max(1, static_cast<int>(m_thumbnailFrames.size()));
        const auto tileWidth = strip.width() / static_cast<double>(tileCount);
        for (int index = 0; index < m_thumbnailFrames.size(); ++index)
        {
            const auto frameIndex = m_thumbnailFrames.at(index);
            QVariantMap tile;
            tile.insert(QStringLiteral("x"), strip.left() + tileWidth * index);
            tile.insert(QStringLiteral("y"), strip.top());
            tile.insert(QStringLiteral("width"), tileWidth);
            tile.insert(QStringLiteral("height"), strip.height());
            tile.insert(QStringLiteral("frameIndex"), frameIndex);
            tile.insert(QStringLiteral("source"), thumbnailSourceForFrame(frameIndex));
            m_thumbnailTiles.push_back(tile);
        }
    }

    m_markerX = xForFramePosition(m_currentFramePosition);
    m_hasSelectedNodeRange = false;
    m_selectedNodeRangeX = 0.0;
    m_selectedNodeRangeWidth = 0.0;
    if (m_selectedNodeStartFrame.has_value() && m_selectedNodeEndFrame.has_value() && m_totalFrames > 0)
    {
        const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
        const auto startFrame = std::clamp(
            std::min(*m_selectedNodeStartFrame, *m_selectedNodeEndFrame),
            0,
            maxFrameIndex);
        const auto endFrame = std::clamp(
            std::max(*m_selectedNodeStartFrame, *m_selectedNodeEndFrame),
            startFrame,
            maxFrameIndex);
        const auto viewEndFrame = m_viewStartFrame + m_visibleFrameSpan;
        if (static_cast<double>(endFrame) >= m_viewStartFrame && static_cast<double>(startFrame) <= viewEndFrame)
        {
            const auto unclippedStartX = xForFramePosition(static_cast<double>(startFrame));
            const auto unclippedEndX = xForFramePosition(static_cast<double>(endFrame));
            const auto clippedStartX = std::clamp(unclippedStartX, strip.left(), strip.right());
            const auto clippedEndX = std::clamp(unclippedEndX, strip.left(), strip.right());
            m_hasSelectedNodeRange = true;
            m_selectedNodeRangeX = std::min(clippedStartX, clippedEndX);
            m_selectedNodeRangeWidth = std::max(2.0, std::abs(clippedEndX - clippedStartX));
        }
    }
    m_hasHoverLine = !m_playbackActive && m_hoveredStripX.has_value();
    m_hoverX = m_hasHoverLine
        ? std::clamp(*m_hoveredStripX, strip.left(), strip.right())
        : 0.0;

    emit visualsChanged();
    if (m_hasSelectedNodeRange != previousHasSelectedNodeRange
        || std::abs(m_selectedNodeRangeX - previousSelectedNodeRangeX) > 0.001
        || std::abs(m_selectedNodeRangeWidth - previousSelectedNodeRangeWidth) > 0.001
        || m_hasHoverLine != previousHasHoverLine
        || std::abs(m_hoverX - previousHoverX) > 0.001)
    {
        emit overlayChanged();
    }
    if (std::abs(m_markerX - previousMarkerX) > 0.001)
    {
        emit markerChanged();
    }
}

void ThumbnailStripQuickController::reloadThumbnailManifest()
{
    if (m_projectRootPath.isEmpty() || m_videoPath.isEmpty())
    {
        m_thumbnailManifest.reset();
        return;
    }

    const auto manifest = dawg::timeline::loadTimelineThumbnailManifest(m_projectRootPath);
    if (!manifest.has_value())
    {
        m_thumbnailManifest.reset();
        return;
    }

    const auto absoluteManifestVideoPath =
        QDir(m_projectRootPath).absoluteFilePath(manifest->videoRelativePath);
    if (QDir::cleanPath(QDir::fromNativeSeparators(absoluteManifestVideoPath))
        != QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(m_videoPath).absoluteFilePath())))
    {
        m_thumbnailManifest.reset();
        return;
    }

    m_thumbnailManifest = manifest;
}

QRectF ThumbnailStripQuickController::computeStripRect() const
{
    return QRectF{
        kPanelInset,
        kPanelInset,
        std::max(kMinimumStripWidth, m_viewportWidth - (kPanelInset * 2.0)),
        std::max(kMinimumStripHeight, m_viewportHeight - (kPanelInset * 2.0))
    };
}

QVector<int> ThumbnailStripQuickController::computeThumbnailFrames() const
{
    QVector<int> frameIndices;
    if (m_totalFrames <= 0 || m_videoPath.isEmpty() || !m_thumbnailManifest.has_value())
    {
        return frameIndices;
    }

    const auto strip = computeStripRect();
    const auto tileCount = std::clamp(
        static_cast<int>(std::floor(strip.width() / 92.0)),
        4,
        12);
    frameIndices.reserve(tileCount);
    for (int index = 0; index < tileCount; ++index)
    {
        const auto ratio = (static_cast<double>(index) + 0.5) / static_cast<double>(tileCount);
        frameIndices.push_back(static_cast<int>(std::lround(m_viewStartFrame + m_visibleFrameSpan * ratio)));
    }
    return frameIndices;
}

QString ThumbnailStripQuickController::thumbnailSourceForFrame(const int targetFrameIndex) const
{
    if (!m_thumbnailManifest.has_value() || m_projectRootPath.isEmpty())
    {
        return {};
    }

    const int thumbnailCount = std::max(1, static_cast<int>(m_thumbnailFrames.size()));
    const auto desiredStepFrames = std::max(1.0, m_visibleFrameSpan / static_cast<double>(thumbnailCount));
    const auto* selectedLevel = &m_thumbnailManifest->levels.front();
    auto bestDistance = std::abs(static_cast<double>(selectedLevel->frameStep) - desiredStepFrames);
    for (const auto& level : m_thumbnailManifest->levels)
    {
        const auto distance = std::abs(static_cast<double>(level.frameStep) - desiredStepFrames);
        if (distance < bestDistance)
        {
            selectedLevel = &level;
            bestDistance = distance;
        }
    }

    if (selectedLevel->frames.isEmpty())
    {
        return {};
    }

    int bestFrameIndex = selectedLevel->frames.front();
    int bestFrameDistance = std::abs(bestFrameIndex - targetFrameIndex);
    for (const int frameIndex : selectedLevel->frames)
    {
        const int distance = std::abs(frameIndex - targetFrameIndex);
        if (distance < bestFrameDistance)
        {
            bestFrameIndex = frameIndex;
            bestFrameDistance = distance;
        }
    }

    const auto path = dawg::timeline::timelineThumbnailFilePath(
        m_projectRootPath,
        selectedLevel->index,
        bestFrameIndex);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString{};
}

int ThumbnailStripQuickController::frameForPosition(const double x) const
{
    if (m_totalFrames <= 1)
    {
        return 0;
    }

    const auto strip = computeStripRect();
    const auto ratio = std::clamp((x - strip.left()) / std::max(1.0, strip.width()), 0.0, 1.0);
    return static_cast<int>(std::lround(m_viewStartFrame + ratio * m_visibleFrameSpan));
}

double ThumbnailStripQuickController::xForFrame(const int frameIndex) const
{
    return xForFramePosition(static_cast<double>(frameIndex));
}

double ThumbnailStripQuickController::xForFramePosition(const double framePosition) const
{
    const auto strip = computeStripRect();
    if (m_totalFrames <= 1)
    {
        return strip.left();
    }

    const auto ratio =
        (std::clamp(framePosition, 0.0, static_cast<double>(std::max(0, m_totalFrames - 1))) - m_viewStartFrame)
        / std::max(1.0, m_visibleFrameSpan);
    return strip.left() + strip.width() * ratio;
}

void ThumbnailStripQuickController::requestFrame(const int frameIndex)
{
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, std::max(0, m_totalFrames - 1));
    if (clampedFrameIndex == m_lastRequestedFrame)
    {
        return;
    }

    m_lastRequestedFrame = clampedFrameIndex;
    emit frameRequested(clampedFrameIndex);
}

void ThumbnailStripQuickController::requestFrameCoalesced(const int frameIndex)
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

void ThumbnailStripQuickController::flushPendingFrameRequest()
{
    if (m_pendingRequestedFrame < 0)
    {
        return;
    }

    const auto frameIndex = m_pendingRequestedFrame;
    m_pendingRequestedFrame = -1;
    requestFrame(frameIndex);
}

void ThumbnailStripQuickController::requestFrameAt(const double x)
{
    requestFrame(frameForPosition(x));
}

void ThumbnailStripQuickController::updateHover(const double x, const double y)
{
    const auto strip = computeStripRect();
    const auto hadHover = m_hoveredStripX.has_value();
    const auto previousHoverX = m_hoverX;
    const auto previousHasHoverLine = m_hasHoverLine;

    if (!m_playbackActive && strip.contains(QPointF{x, y}))
    {
        m_hoveredStripX = x;
    }
    else
    {
        m_hoveredStripX.reset();
    }

    m_hasHoverLine = !m_playbackActive && m_hoveredStripX.has_value();
    m_hoverX = m_hasHoverLine
        ? std::clamp(*m_hoveredStripX, strip.left(), strip.right())
        : 0.0;
    if (hadHover != m_hoveredStripX.has_value()
        || m_hasHoverLine != previousHasHoverLine
        || std::abs(m_hoverX - previousHoverX) > 0.001)
    {
        emit overlayChanged();
    }
}

void ThumbnailStripQuickController::clampViewWindow()
{
    if (m_totalFrames <= 1)
    {
        m_viewStartFrame = 0.0;
        m_visibleFrameSpan = 1.0;
        return;
    }

    m_visibleFrameSpan = clampedVisibleFrameSpan(m_totalFrames, m_visibleFrameSpan);
    const auto maxFrameIndex = std::max(1, m_totalFrames - 1);
    const auto maxStart = std::max(0.0, static_cast<double>(maxFrameIndex) - m_visibleFrameSpan);
    m_viewStartFrame = std::clamp(m_viewStartFrame, 0.0, maxStart);
}

QVariantMap ThumbnailStripQuickController::rectMap(const QRectF& rect)
{
    return QVariantMap{
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()}
    };
}
