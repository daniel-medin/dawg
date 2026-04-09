#pragma once

#include <optional>
#include <vector>

#include <QList>
#include <QPointF>
#include <QString>
#include <QUuid>

#include "app/NodeDocument.h"

class PlayerController;

class NodeController
{
public:
    explicit NodeController(PlayerController& controller);

    void seedTrack(const QPointF& imagePoint);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint);
    bool selectTrackAndJumpToStart(const QUuid& trackId);
    void selectAllVisibleTracks();
    void selectTracks(const QList<QUuid>& trackIds);
    void selectTrack(const QUuid& trackId);
    void selectNextVisibleTrack();
    void selectNextTimelineTrack();
    void clearSelection();
    bool copySelectedTracks();
    bool pasteCopiedTracksAtCurrentFrame();
    bool cutSelectedTracks();
    bool undoLastTrackEdit();
    bool redoLastTrackEdit();
    bool renameTrack(const QUuid& trackId, const QString& label);
    void setTrackStartFrame(const QUuid& trackId, int frameIndex);
    void setTrackEndFrame(const QUuid& trackId, int frameIndex);
    void moveTrackFrameSpan(const QUuid& trackId, int deltaFrames);
    void moveSelectedTrack(const QPointF& imagePoint);
    void nudgeSelectedTracks(const QPointF& delta);
    void deleteSelectedTrack();
    [[nodiscard]] int emptyTrackCount() const;
    void deleteAllEmptyTracks();
    void clearAllTracks();
    void setSelectedTrackStartToCurrentFrame();
    void setSelectedTrackEndToCurrentFrame();
    void toggleSelectedTrackLabels();
    void setAllTracksStartToCurrentFrame();
    void setAllTracksEndToCurrentFrame();
    void trimSelectedTracksToAttachedSound();
    void toggleSelectedTrackAutoPan();
    [[nodiscard]] bool canPasteNodeClip() const;
    [[nodiscard]] bool copySelectedNodeClip(const QString& laneId, const QString& clipId);
    [[nodiscard]] bool copyNodeTimelineSelection(
        int startLaneIndex,
        int endLaneIndex,
        int startMs,
        int endMs);
    [[nodiscard]] bool cutSelectedNodeClip(
        const QString& laneId,
        const QString& clipId,
        QString* selectedLaneId = nullptr);
    [[nodiscard]] bool cutNodeTimelineSelection(
        int startLaneIndex,
        int endLaneIndex,
        int startMs,
        int endMs,
        QString* selectedLaneId = nullptr);
    [[nodiscard]] bool pasteSelectedNodeClip(
        const QString& targetLaneId,
        int playheadMs,
        QString* pastedLaneId = nullptr,
        QString* pastedClipId = nullptr);
    [[nodiscard]] bool dropSelectedNodeClip(
        const QString& sourceLaneId,
        const QString& clipId,
        const QString& targetLaneId,
        int laneOffsetMs,
        bool copyClip,
        QString* droppedLaneId = nullptr,
        QString* droppedClipId = nullptr);
    [[nodiscard]] std::optional<int> nodeLaneClipCount(const QString& laneId) const;
    [[nodiscard]] bool deleteSelectedNodeClipOrLane(
        const QString& laneId,
        const QString& laneHeaderId,
        const QString& clipId,
        bool allowDeletePopulatedLane,
        QString* nextSelectedLaneId = nullptr);
    [[nodiscard]] bool deleteNodeTimelineSelection(
        int startLaneIndex,
        int endLaneIndex,
        int startMs,
        int endMs,
        QString* nextSelectedLaneId = nullptr);
    [[nodiscard]] bool setNodeLaneMuted(const QString& laneId, bool muted);
    [[nodiscard]] bool setNodeLaneSoloed(const QString& laneId, bool soloed);
    [[nodiscard]] bool moveNodeClip(
        const QString& laneId,
        const QString& clipId,
        int laneOffsetMs,
        int nodeDurationMs);
    [[nodiscard]] bool splitNodeClipAtPlayhead(
        const QString& laneId,
        const QString& clipId,
        int playheadMs,
        QString* selectedLaneId = nullptr,
        QString* selectedClipId = nullptr);
    [[nodiscard]] bool resolveNodeClipAtPlayhead(
        const QString& preferredLaneId,
        int playheadMs,
        QString* resolvedLaneId,
        QString* resolvedClipId) const;
    [[nodiscard]] bool trimNodeClip(
        const QString& laneId,
        const QString& clipId,
        int targetMs,
        bool trimStart,
        int nodeDurationMs);
    [[nodiscard]] bool addNodeLaneOrImportClip(
        const QString& nodesDirectoryPath,
        const QString& importedAudioFilePath,
        const QString& selectedLaneId,
        QString* resolvedLaneId = nullptr,
        QString* resolvedLaneLabel = nullptr);
    [[nodiscard]] bool saveSelectedNodeToFile(
        const QString& nodeFilePath,
        bool bindToSelectedTrack = true,
        const QString& nodeLabelOverride = {},
        QString* errorMessage = nullptr);
    [[nodiscard]] bool openNodeFileAsNewNode(
        const QString& nodeFilePath,
        const QString& projectRootPath,
        QString* errorMessage = nullptr);

private:
    struct NodeSelectionClipboardSegment
    {
        dawg::node::AudioClipData clip;
        int laneIndexOffset = 0;
    };

    struct NodeSelectionClipboard
    {
        int durationMs = 0;
        std::vector<NodeSelectionClipboardSegment> segments;
    };

    [[nodiscard]] bool loadSelectedNodeDocument(
        QString* nodeDocumentPath,
        dawg::node::Document* nodeDocument,
        QString* errorMessage) const;
    [[nodiscard]] bool saveSelectedNodeDocument(
        const QString& nodeDocumentPath,
        const dawg::node::Document& nodeDocument,
        const QString& failureStatus);

    PlayerController& m_controller;
    std::optional<dawg::node::AudioClipData> m_nodeEditorClipClipboard;
    std::optional<NodeSelectionClipboard> m_nodeEditorSelectionClipboard;
};
