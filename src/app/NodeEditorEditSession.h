#pragma once

#include <functional>

#include <QString>

class PlayerController;
class NodeEditorQuickController;

class NodeEditorEditSession
{
public:
    struct Outcome
    {
        bool documentChanged = false;
        bool refreshMix = false;
        bool forcePreviewSync = false;
        bool updatePasteAvailability = false;
        bool clearTimelineSelection = false;
        QString selectedLaneId;
        QString selectedClipId;
    };

    NodeEditorEditSession(PlayerController& controller, NodeEditorQuickController& nodeEditorQuickController);

    void setStatusCallback(std::function<void(const QString&)> statusCallback);

    [[nodiscard]] Outcome handleEditAction(const QString& actionKey);
    [[nodiscard]] Outcome deleteSelection(const std::function<bool(int clipCount)>& confirmDeletePopulatedLane);
    [[nodiscard]] Outcome splitSelectedClipAtPlayhead();
    [[nodiscard]] Outcome trimSelectedClipToPlayhead(bool trimStart);
    [[nodiscard]] Outcome nudgeSelectedClipFrames(int frameDelta);
    [[nodiscard]] Outcome setLaneMuted(const QString& laneId, bool muted);
    [[nodiscard]] Outcome setLaneSoloed(const QString& laneId, bool soloed);
    [[nodiscard]] Outcome moveClip(const QString& laneId, const QString& clipId, int laneOffsetMs);
    [[nodiscard]] Outcome copyClipAtOffset(const QString& laneId, const QString& clipId, int laneOffsetMs);
    [[nodiscard]] Outcome dropClip(
        const QString& sourceLaneId,
        const QString& clipId,
        const QString& targetLaneId,
        int laneOffsetMs,
        bool copyClip);
    [[nodiscard]] Outcome trimClip(
        const QString& laneId,
        const QString& clipId,
        int targetMs,
        bool trimStart);

private:
    void showStatus(const QString& message) const;

    PlayerController& m_controller;
    NodeEditorQuickController& m_nodeEditorQuickController;
    std::function<void(const QString&)> m_statusCallback;
};
