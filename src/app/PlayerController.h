#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

#include <QImage>
#include <QList>
#include <QObject>
#include <QUuid>
#include <QString>
#include <QSize>
#include <QElapsedTimer>
#include <QTimer>

#include <opencv2/core/mat.hpp>

#include "app/TransportController.h"
#include "app/AudioPoolService.h"
#include "app/PerformanceLogger.h"
#include "core/audio/AudioEngine.h"
#include "core/render/RenderService.h"
#include "core/tracking/MotionTracker.h"
#include "core/video/AnalysisFrameProvider.h"
#include "core/video/VideoFrame.h"
#include "core/video/VideoPlaybackService.h"
#include "ui/ClipEditorView.h"
#include "ui/MixView.h"
#include "ui/TimelineView.h"

class PlayerController final : public QObject
{
    Q_OBJECT

public:
    explicit PlayerController(QObject* parent = nullptr);

    bool openVideo(const QString& filePath);
    bool importAudioToPool(const QString& filePath);
    void goToStart();
    void togglePlayback();
    void pause(bool restorePlaybackAnchor = true);
    void seekToFrame(int frameIndex);
    void stepBackward();
    void stepForward();
    void stepFastBackward();
    void stepFastForward();
    void seedTrack(const QPointF& imagePoint);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint);
    bool importSoundForSelectedTrack(const QString& filePath);
    bool selectTrackAndJumpToStart(const QUuid& trackId);
    void selectAllVisibleTracks();
    void selectTracks(const QList<QUuid>& trackIds);
    void selectTrack(const QUuid& trackId);
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
    void toggleEmbeddedVideoAudioMuted();
    void setFastPlaybackEnabled(bool enabled);
    void setInsertionFollowsPlayback(bool enabled);
    void setMotionTrackingEnabled(bool enabled);
    void setLoopStartFrame(int frameIndex);
    void setLoopEndFrame(int frameIndex);
    void clearLoopRange();
    void setMasterMixGainDb(float gainDb);
    void setMasterMixMuted(bool muted);
    void setMixLaneGainDb(int laneIndex, float gainDb);
    void setMixLaneMuted(int laneIndex, bool muted);
    void setMixLaneSoloed(int laneIndex, bool soloed);
    void selectNextVisibleTrack();
    bool startAudioPoolPreview(const QString& filePath);
    void stopAudioPoolPreview();
    bool startSelectedTrackClipPreview();
    void stopSelectedTrackClipPreview();
    bool setSelectedTrackClipRangeMs(int clipStartMs, int clipEndMs);

    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isInsertionFollowsPlayback() const;
    [[nodiscard]] bool isMotionTrackingEnabled() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] bool hasTracks() const;
    [[nodiscard]] bool canPasteTracks() const;
    [[nodiscard]] bool canUndoTrackEdit() const;
    [[nodiscard]] bool canRedoTrackEdit() const;
    [[nodiscard]] int trackCount() const;
    [[nodiscard]] int currentFrameIndex() const;
    [[nodiscard]] int totalFrames() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] QString loadedPath() const;
    [[nodiscard]] QUuid selectedTrackId() const;
    [[nodiscard]] QString decoderBackendName() const;
    [[nodiscard]] bool videoHardwareAccelerated() const;
    [[nodiscard]] QString renderBackendName() const;
    [[nodiscard]] bool renderHardwareAccelerated() const;
    [[nodiscard]] RenderService* renderService();
    [[nodiscard]] const VideoFrame& currentVideoFrame() const;
    [[nodiscard]] bool hasEmbeddedVideoAudio() const;
    [[nodiscard]] QString embeddedVideoAudioDisplayName() const;
    [[nodiscard]] bool isEmbeddedVideoAudioMuted() const;
    [[nodiscard]] bool isFastPlaybackEnabled() const;
    [[nodiscard]] bool isFastPlaybackActive() const;
    [[nodiscard]] std::optional<int> loopStartFrame() const;
    [[nodiscard]] std::optional<int> loopEndFrame() const;
    [[nodiscard]] float masterMixGainDb() const;
    [[nodiscard]] bool masterMixMuted() const;
    [[nodiscard]] float masterMixLevel() const;
    [[nodiscard]] QSize videoFrameSize() const;
    [[nodiscard]] QString trackLabel(const QUuid& trackId) const;
    [[nodiscard]] bool trackHasAttachedAudio(const QUuid& trackId) const;
    [[nodiscard]] bool trackAutoPanEnabled(const QUuid& trackId) const;
    [[nodiscard]] bool selectedTracksAutoPanEnabled() const;
    [[nodiscard]] std::optional<ClipEditorState> selectedClipEditorState() const;
    bool removeAudioFromPool(const QString& filePath);
    bool removeAudioAndConnectedNodesFromPool(const QString& filePath);
    [[nodiscard]] std::vector<AudioPoolItem> audioPoolItems() const;
    [[nodiscard]] std::vector<MixLaneStrip> mixLaneStrips() const;
    [[nodiscard]] std::vector<TimelineTrackSpan> timelineTrackSpans() const;
    [[nodiscard]] const std::vector<TrackOverlay>& currentOverlays() const;

signals:
    void frameReady(const QImage& image, int frameIndex, double timestampSeconds);
    void overlaysChanged();
    void videoLoaded(const QString& filePath, int totalFrames, double fps);
    void videoAudioStateChanged();
    void playbackStateChanged(bool playing);
    void insertionFollowsPlaybackChanged(bool enabled);
    void motionTrackingChanged(bool enabled);
    void loopRangeChanged();
    void selectionChanged(bool hasSelection);
    void trackAvailabilityChanged(bool hasTracks);
    void audioPoolChanged();
    void audioPoolPlaybackStateChanged();
    void editStateChanged();
    void statusChanged(const QString& message);

private slots:
    void advancePlayback();
    void advanceSelectionFade();

private:
    [[nodiscard]] bool advanceOneFrame(bool presentFrame, bool syncAudio);
    [[nodiscard]] bool needsTrackingFrameProcessing() const;
    void updateCurrentGrayFrameIfNeeded();
    bool loadFrameAt(int frameIndex);
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] std::optional<int> trimmedEndFrameForTrack(const TrackPoint& track) const;
    [[nodiscard]] std::optional<int> audioDurationMs(const QString& filePath) const;
    void saveUndoState();
    void restoreTrackEditState(const MotionTrackerState& trackerState, const std::vector<QUuid>& selectedTrackIds);
    void syncAttachedAudioForCurrentFrame();
    void refreshOverlays();
    void emitCurrentFrame();
    bool applyPresentationScaleForPlaybackState(bool playbackActive);
    [[nodiscard]] bool isTrackSelected(const QUuid& trackId) const;
    [[nodiscard]] float mixLaneGainDb(int laneIndex) const;
    [[nodiscard]] bool isMixLaneMuted(int laneIndex) const;
    [[nodiscard]] bool isMixLaneSoloed(int laneIndex) const;
    [[nodiscard]] bool anyMixLaneSoloed() const;
    [[nodiscard]] std::optional<std::pair<int, int>> activeLoopRange() const;
    void applyLiveMixStateToCurrentPlayback();
    void setSelectedTrackId(const QUuid& trackId, bool fadePreviousSelection = true);
    void logPlaybackHitchIfNeeded(int targetFrameIndex, int previousFrameIndex, int advancedFrames);

    VideoPlaybackService m_videoPlayback;
    TransportController m_transport;
    std::unique_ptr<AudioEngine> m_audioEngine;
    RenderService m_renderService;
    AnalysisFrameProvider m_analysisFrameProvider;
    AudioPoolService m_audioPool;
    MotionTracker m_tracker;
    QString m_loadedPath;
    QString m_embeddedVideoAudioPath;
    QString m_embeddedVideoAudioDisplayName;
    bool m_embeddedVideoAudioMuted = true;
    VideoFrame m_currentFrame;
    cv::Mat m_currentGrayFrame;
    std::vector<TrackOverlay> m_currentOverlays;
    int m_totalFrames = 0;
    double m_fps = 0.0;
    bool m_motionTrackingEnabled = false;
    double m_playbackStartTimestampSeconds = 0.0;
    QElapsedTimer m_playbackElapsedTimer;
    QElapsedTimer m_perfPlaybackTickTimer;
    std::optional<int> m_loopStartFrame;
    std::optional<int> m_loopEndFrame;
    float m_masterMixGainDb = 0.0F;
    bool m_masterMixMuted = false;
    std::unordered_map<int, float> m_mixLaneGainDbByLane;
    std::unordered_map<int, bool> m_mixLaneMutedByLane;
    std::unordered_map<int, bool> m_mixLaneSoloByLane;
    std::vector<QUuid> m_selectedTrackIds;
    QUuid m_selectedTrackId;
    QUuid m_fadingDeselectedTrackId;
    float m_fadingDeselectedTrackOpacity = 0.0F;
    QTimer m_selectionFadeTimer;
    std::vector<TrackPoint> m_copiedTracks;
    std::optional<MotionTrackerState> m_undoTrackerState;
    std::vector<QUuid> m_undoSelectedTrackIds;
    std::optional<MotionTrackerState> m_redoTrackerState;
    std::vector<QUuid> m_redoSelectedTrackIds;
    PerformanceLogger m_perfLogger;
    std::uint64_t m_lastLoggedQueueStarvationCount = 0;
    const QUuid m_embeddedVideoAudioTrackId = QUuid(QStringLiteral("{eb6fc60f-0781-433f-9f03-ff16531165f7}"));
    const QUuid m_audioPoolPreviewTrackId = QUuid(QStringLiteral("{8d6166c4-b107-4c55-8f11-f9cbf67d0e0a}"));
    const QUuid m_clipEditorPreviewTrackId = QUuid(QStringLiteral("{3427e43b-a4c0-4d8e-86ea-52e9d85f2747}"));
    QString m_audioPoolPreviewAssetPath;
    QTimer m_clipEditorPreviewStopTimer;
    mutable QHash<QString, std::optional<int>> m_audioDurationMsByPath;
};
