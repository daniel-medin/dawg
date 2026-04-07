#pragma once

#include <functional>
#include <optional>

class NodeEditorQuickController;
class PlayerController;
class ThumbnailStripQuickController;
class TimelineQuickController;

class TransportUiSyncController final
{
public:
    using TimelineVisibleFn = std::function<bool()>;

    TransportUiSyncController(
        PlayerController& player,
        NodeEditorQuickController& nodeEditor,
        TimelineQuickController& timeline,
        ThumbnailStripQuickController& thumbnailStrip,
        TimelineVisibleFn timelineVisible);

    void resetNodeEditorSync();

    [[nodiscard]] std::optional<int> nodeEditorProjectFrameForPlayheadMs(int playheadMs) const;
    [[nodiscard]] std::optional<double> nodeEditorProjectFramePositionForPlayheadMs(int playheadMs) const;

    void syncProjectPlayheadToNodeEditor(int playheadMs);
    void syncProjectMarkersToNodeEditor(int playheadMs);
    void syncThumbnailStripMarkerToNodeEditor(int playheadMs);
    void syncNodeEditorPlayheadToProjectFrame(int frameIndex);

private:
    [[nodiscard]] bool isTimelineVisible() const;

    PlayerController& m_player;
    NodeEditorQuickController& m_nodeEditor;
    TimelineQuickController& m_timeline;
    ThumbnailStripQuickController& m_thumbnailStrip;
    TimelineVisibleFn m_timelineVisible;
    int m_lastNodeEditorSyncedProjectFrame = -1;
};
