#include "app/TransportUiSyncController.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/PlayerController.h"
#include "ui/NodeEditorQuickController.h"
#include "ui/ThumbnailStripQuickController.h"
#include "ui/TimelineQuickController.h"

TransportUiSyncController::TransportUiSyncController(
    PlayerController& player,
    NodeEditorQuickController& nodeEditor,
    TimelineQuickController& timeline,
    ThumbnailStripQuickController& thumbnailStrip,
    TimelineVisibleFn timelineVisible)
    : m_player(player)
    , m_nodeEditor(nodeEditor)
    , m_timeline(timeline)
    , m_thumbnailStrip(thumbnailStrip)
    , m_timelineVisible(std::move(timelineVisible))
{
}

void TransportUiSyncController::resetNodeEditorSync()
{
    m_lastNodeEditorSyncedProjectFrame = -1;
}

std::optional<int> TransportUiSyncController::nodeEditorProjectFrameForPlayheadMs(const int playheadMs) const
{
    const auto selectedRange = m_player.selectedTrackFrameRange();
    if (!selectedRange.has_value())
    {
        return std::nullopt;
    }

    const auto fps = std::max(0.0001, m_player.fps());
    const auto frameOffset = static_cast<int>(std::lround((static_cast<double>(std::max(0, playheadMs)) * fps) / 1000.0));
    return std::clamp(
        selectedRange->first + frameOffset,
        selectedRange->first,
        selectedRange->second);
}

std::optional<double> TransportUiSyncController::nodeEditorProjectFramePositionForPlayheadMs(const int playheadMs) const
{
    const auto selectedRange = m_player.selectedTrackFrameRange();
    if (!selectedRange.has_value())
    {
        return std::nullopt;
    }

    const auto fps = std::max(0.0001, m_player.fps());
    const auto frameOffset = (static_cast<double>(std::max(0, playheadMs)) * fps) / 1000.0;
    return std::clamp(
        static_cast<double>(selectedRange->first) + frameOffset,
        static_cast<double>(selectedRange->first),
        static_cast<double>(selectedRange->second));
}

void TransportUiSyncController::syncProjectPlayheadToNodeEditor(const int playheadMs)
{
    const auto projectFrame = nodeEditorProjectFrameForPlayheadMs(playheadMs);
    if (!projectFrame.has_value() || *projectFrame == m_lastNodeEditorSyncedProjectFrame)
    {
        return;
    }

    m_lastNodeEditorSyncedProjectFrame = *projectFrame;
    m_player.seekToFrame(*projectFrame);
    if (isTimelineVisible())
    {
        m_timeline.setCurrentFrame(*projectFrame);
    }
    m_thumbnailStrip.setCurrentFrame(*projectFrame);
}

void TransportUiSyncController::syncProjectMarkersToNodeEditor(const int playheadMs)
{
    const auto projectFrame = nodeEditorProjectFrameForPlayheadMs(playheadMs);
    const auto projectFramePosition = nodeEditorProjectFramePositionForPlayheadMs(playheadMs);
    if (!projectFrame.has_value())
    {
        syncThumbnailStripMarkerToNodeEditor(playheadMs);
        return;
    }

    if (isTimelineVisible() && projectFramePosition.has_value())
    {
        m_timeline.setCurrentFramePosition(*projectFramePosition);
    }
    if (*projectFrame != m_lastNodeEditorSyncedProjectFrame)
    {
        m_lastNodeEditorSyncedProjectFrame = *projectFrame;
        m_player.refreshOverlaysForFrame(*projectFrame);
    }
    syncThumbnailStripMarkerToNodeEditor(playheadMs);
}

void TransportUiSyncController::syncThumbnailStripMarkerToNodeEditor(const int playheadMs)
{
    const auto projectFramePosition = nodeEditorProjectFramePositionForPlayheadMs(playheadMs);
    if (!projectFramePosition.has_value())
    {
        return;
    }

    m_thumbnailStrip.setCurrentFramePosition(*projectFramePosition);
}

void TransportUiSyncController::syncNodeEditorPlayheadToProjectFrame(const int frameIndex)
{
    const auto selectedRange = m_player.selectedTrackFrameRange();
    if (!selectedRange.has_value())
    {
        resetNodeEditorSync();
        return;
    }

    const auto nodeDurationMs = m_nodeEditor.nodeDurationMs();
    if (nodeDurationMs <= 0)
    {
        return;
    }

    const auto fps = std::max(0.0001, m_player.fps());
    int playheadMs = 0;
    if (frameIndex <= selectedRange->first)
    {
        playheadMs = 0;
    }
    else if (frameIndex > selectedRange->second)
    {
        playheadMs = nodeDurationMs;
    }
    else
    {
        playheadMs = std::clamp(
            static_cast<int>(std::lround(
                (static_cast<double>(frameIndex - selectedRange->first) * 1000.0) / fps)),
            0,
            nodeDurationMs);
    }

    m_lastNodeEditorSyncedProjectFrame = frameIndex;
    m_nodeEditor.setPlayheadMs(playheadMs);
}

bool TransportUiSyncController::isTimelineVisible() const
{
    return !m_timelineVisible || m_timelineVisible();
}
