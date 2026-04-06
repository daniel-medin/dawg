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
#include "app/AudioPlaybackCoordinator.h"
#include "app/AudioPoolService.h"
#include "app/ClipEditorSession.h"
#include "app/MixStateStore.h"
#include "app/PerformanceLogger.h"
#include "app/ProjectDocument.h"
#include "app/SelectionController.h"
#include "app/TrackEditService.h"
#include "app/VideoPlaybackCoordinator.h"
#include "core/audio/AudioEngine.h"
#include "core/tracking/MotionTracker.h"
#include "core/video/VideoFrame.h"
#include "ui/MixTypes.h"
#include "ui/TimelineTypes.h"

struct PlaybackDebugStats
{
    double advancePlaybackMs = 0.0;
    double frameCallbackMs = 0.0;
    double overlayRefreshMs = 0.0;
    double overlayBuildMs = 0.0;
    double presentFrameMs = 0.0;
    double frameReadyDispatchMs = 0.0;
    double syncAudioMs = 0.0;
    int overlayCount = 0;
    int overlayLabelCount = 0;
    VideoPlaybackRuntimeStats runtimeStats;
};

class PlayerController final : public QObject
{
    Q_OBJECT

public:
    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    bool openVideo(const QString& filePath);
    bool refreshPlaybackSource(QString* errorMessage = nullptr);
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
    void toggleEmbeddedVideoAudioMuted();
    void setFastPlaybackEnabled(bool enabled);
    void setInsertionFollowsPlayback(bool enabled);
    void setMotionTrackingEnabled(bool enabled);
    void setUseProxyVideo(bool enabled);
    void setProxyVideoPath(const QString& filePath);
    void setPreferredD3D11Device(void* device);
    void setNativeVideoPresentationEnabled(bool enabled);
    bool addLoopRange(int startFrame, int endFrame);
    void setLoopStartFrame(int loopIndex, int frameIndex);
    void setLoopEndFrame(int loopIndex, int frameIndex);
    void removeLoopRange(int loopIndex);
    void clearLoopRanges();
    void setMasterMixGainDb(float gainDb);
    void setMasterMixMuted(bool muted);
    void setMixLaneGainDb(int laneIndex, float gainDb);
    void setMixLaneMuted(int laneIndex, bool muted);
    void setMixLaneSoloed(int laneIndex, bool soloed);
    void setMixSoloXorMode(bool enabled);
    [[nodiscard]] std::optional<float> mixLaneGainForTrack(const QUuid& trackId) const;
    bool setMixLaneGainForTrack(const QUuid& trackId, float gainDb);
    [[nodiscard]] std::optional<float> adjustMixLaneGainForTrack(const QUuid& trackId, float deltaDb);
    void selectNextVisibleTrack();
    void selectNextTimelineTrack();
    bool startAudioPoolPreview(const QString& filePath);
    void stopAudioPoolPreview();
    bool startSelectedTrackClipPreview();
    void stopSelectedTrackClipPreview();
    void resetProjectState();
    bool restoreProjectState(const dawg::project::ControllerState& state, QString* errorMessage = nullptr);
    bool setSelectedTrackClipRangeMs(int clipStartMs, int clipEndMs);
    bool setSelectedTrackClipPlayheadMs(int playheadMs);
    bool setSelectedTrackAudioGainDb(float gainDb);
    bool setSelectedTrackLoopEnabled(bool enabled);

    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isSelectedTrackClipPreviewPlaying() const;
    [[nodiscard]] bool isInsertionFollowsPlayback() const;
    [[nodiscard]] bool isMotionTrackingEnabled() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] bool hasTracks() const;
    [[nodiscard]] bool hasEmptyTracks() const;
    [[nodiscard]] bool canPasteTracks() const;
    [[nodiscard]] bool canUndoTrackEdit() const;
    [[nodiscard]] bool canRedoTrackEdit() const;
    [[nodiscard]] int trackCount() const;
    [[nodiscard]] int currentFrameIndex() const;
    [[nodiscard]] int totalFrames() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] QString loadedPath() const;
    [[nodiscard]] QString projectVideoPath() const;
    [[nodiscard]] QString proxyVideoPath() const;
    [[nodiscard]] QString preferredPlaybackPath() const;
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
    [[nodiscard]] bool useProxyVideo() const;
    [[nodiscard]] std::vector<TimelineLoopRange> loopRanges() const;
    [[nodiscard]] float masterMixGainDb() const;
    [[nodiscard]] bool masterMixMuted() const;
    [[nodiscard]] bool isMixSoloXorMode() const;
    [[nodiscard]] float masterMixLevel() const;
    [[nodiscard]] AudioEngine::StereoLevels masterMixStereoLevels() const;
    [[nodiscard]] QSize videoFrameSize() const;
    [[nodiscard]] QString trackLabel(const QUuid& trackId) const;
    [[nodiscard]] bool trackHasAttachedAudio(const QUuid& trackId) const;
    [[nodiscard]] bool selectedTrackLoopEnabled() const;
    [[nodiscard]] bool trackAutoPanEnabled(const QUuid& trackId) const;
    [[nodiscard]] bool selectedTracksAutoPanEnabled() const;
    [[nodiscard]] std::optional<ClipEditorState> selectedClipEditorState() const;
    bool removeAudioFromPool(const QString& filePath);
    bool removeAudioAndConnectedNodesFromPool(const QString& filePath);
    [[nodiscard]] std::vector<AudioPoolItem> audioPoolItems() const;
    [[nodiscard]] dawg::project::ControllerState snapshotProjectState() const;
    [[nodiscard]] std::vector<MixLaneStrip> mixLaneStrips() const;
    [[nodiscard]] std::vector<MixLaneMeterState> mixLaneMeterStates(const std::vector<TimelineTrackSpan>& spans) const;
    [[nodiscard]] std::vector<TimelineTrackSpan> timelineTrackSpans() const;
    [[nodiscard]] const std::vector<TrackOverlay>& currentOverlays() const;
    [[nodiscard]] PlaybackDebugStats playbackDebugStats() const;

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
    void mixSoloModeChanged(bool xorMode);
    void statusChanged(const QString& message);

private slots:
    void advancePlayback();
    void advanceSelectionFade();

private:
    friend class NodeController;

    [[nodiscard]] bool advanceOneFrame(bool presentFrame, bool syncAudio);
    [[nodiscard]] bool openPlaybackVideo(const QString& filePath);
    [[nodiscard]] QString resolvedPlaybackPath(const QString& projectVideoPath, const QString& proxyVideoPath) const;
    bool loadFrameAt(int frameIndex);
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] std::optional<int> trimmedEndFrameForTrack(const TrackPoint& track) const;
    [[nodiscard]] std::optional<int> audioDurationMs(const QString& filePath) const;
    [[nodiscard]] std::optional<int> audioChannelCount(const QString& filePath) const;
    void saveUndoState();
    void restoreTrackEditState(const MotionTrackerState& trackerState, const std::vector<QUuid>& selectedTrackIds);
    void clearProjectStateAfterMediaStop(bool resetVideoPlaybackState = true);
    void syncAttachedAudioForCurrentFrame();
    void refreshOverlays();
    void emitCurrentFrame();
    [[nodiscard]] bool isTrackSelected(const QUuid& trackId) const;
    [[nodiscard]] std::optional<int> mixLaneIndexForTrack(const QUuid& trackId) const;
    [[nodiscard]] std::optional<std::pair<int, int>> activeLoopRange() const;
    [[nodiscard]] bool loopRangeOverlaps(const TimelineLoopRange& candidate, int ignoreIndex = -1) const;
    void applyLiveMixStateToCurrentPlayback();
    void handleClipEditorPreviewTimeout();
    void setSelectedTrackId(const QUuid& trackId, bool fadePreviousSelection = true);

    TransportController m_transport;
    std::unique_ptr<AudioEngine> m_audioEngine;
    std::unique_ptr<AudioPlaybackCoordinator> m_audioPlaybackCoordinator;
    std::unique_ptr<VideoPlaybackCoordinator> m_videoPlaybackCoordinator;
    std::unique_ptr<class NodeController> m_nodeController;
    AudioPoolService m_audioPool;
    MotionTracker m_tracker;
    std::unique_ptr<SelectionController> m_selectionController;
    std::unique_ptr<TrackEditService> m_trackEditService;
    std::unique_ptr<ClipEditorSession> m_clipEditorSession;
    std::unique_ptr<MixStateStore> m_mixStateStore;
    QString m_projectVideoPath;
    QString m_proxyVideoPath;
    bool m_embeddedVideoAudioMuted = true;
    bool m_useProxyVideo = false;
    std::vector<TrackOverlay> m_currentOverlays;
    bool m_motionTrackingEnabled = false;
    std::vector<TimelineLoopRange> m_loopRanges;
    QTimer m_selectionFadeTimer;
    std::optional<MotionTrackerState> m_undoTrackerState;
    std::vector<QUuid> m_undoSelectedTrackIds;
    std::optional<MotionTrackerState> m_redoTrackerState;
    std::vector<QUuid> m_redoSelectedTrackIds;
    PerformanceLogger m_perfLogger;
    const QUuid m_embeddedVideoAudioTrackId = QUuid(QStringLiteral("{eb6fc60f-0781-433f-9f03-ff16531165f7}"));
    QTimer m_clipEditorPreviewStopTimer;
    mutable QHash<QString, std::optional<int>> m_audioDurationMsByPath;
    mutable QHash<QString, std::optional<int>> m_audioChannelCountByPath;
    PlaybackDebugStats m_playbackDebugStats;
};
