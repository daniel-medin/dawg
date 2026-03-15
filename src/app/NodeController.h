#pragma once

#include <QList>
#include <QPointF>
#include <QString>
#include <QUuid>

class PlayerController;

class NodeController
{
public:
    explicit NodeController(PlayerController& controller);

    void seedTrack(const QPointF& imagePoint);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint);
    bool importSoundForSelectedTrack(const QString& filePath);
    bool selectTrackAndJumpToStart(const QUuid& trackId);
    void selectAllVisibleTracks();
    void selectTracks(const QList<QUuid>& trackIds);
    void selectTrack(const QUuid& trackId);
    void selectNextVisibleTrack();
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
    void clearAllTracks();
    void setSelectedTrackStartToCurrentFrame();
    void setSelectedTrackEndToCurrentFrame();
    void toggleSelectedTrackLabels();
    void setAllTracksStartToCurrentFrame();
    void setAllTracksEndToCurrentFrame();
    void trimSelectedTracksToAttachedSound();
    void toggleSelectedTrackAutoPan();

private:
    PlayerController& m_controller;
};
