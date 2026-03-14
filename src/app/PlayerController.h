#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QList>
#include <QObject>
#include <QUuid>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>

#include <opencv2/core/mat.hpp>

#include "app/TransportController.h"
#include "app/AudioPoolService.h"
#include "core/audio/AudioEngine.h"
#include "core/render/RenderService.h"
#include "core/tracking/MotionTracker.h"
#include "core/video/AnalysisFrameProvider.h"
#include "core/video/VideoFrame.h"
#include "core/video/VideoPlaybackService.h"
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
    void seedTrack(const QPointF& imagePoint);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath);
    bool createTrackWithAudioAtCurrentFrame(const QString& filePath, const QPointF& imagePoint);
    bool importSoundForSelectedTrack(const QString& filePath);
    void selectAllVisibleTracks();
    void selectTracks(const QList<QUuid>& trackIds);
    void selectTrack(const QUuid& trackId);
    void clearSelection();
    bool renameTrack(const QUuid& trackId, const QString& label);
    void setTrackStartFrame(const QUuid& trackId, int frameIndex);
    void setTrackEndFrame(const QUuid& trackId, int frameIndex);
    void moveTrackFrameSpan(const QUuid& trackId, int deltaFrames);
    void moveSelectedTrack(const QPointF& imagePoint);
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
    void setInsertionFollowsPlayback(bool enabled);
    void setMotionTrackingEnabled(bool enabled);

    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isInsertionFollowsPlayback() const;
    [[nodiscard]] bool isMotionTrackingEnabled() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] bool hasTracks() const;
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
    [[nodiscard]] bool hasEmbeddedVideoAudio() const;
    [[nodiscard]] QString embeddedVideoAudioDisplayName() const;
    [[nodiscard]] bool isEmbeddedVideoAudioMuted() const;
    [[nodiscard]] QString trackLabel(const QUuid& trackId) const;
    [[nodiscard]] bool trackHasAttachedAudio(const QUuid& trackId) const;
    [[nodiscard]] bool trackAutoPanEnabled(const QUuid& trackId) const;
    [[nodiscard]] bool selectedTracksAutoPanEnabled() const;
    bool removeAudioFromPool(const QString& filePath);
    bool removeAudioAndConnectedNodesFromPool(const QString& filePath);
    [[nodiscard]] std::vector<AudioPoolItem> audioPoolItems() const;
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
    void selectionChanged(bool hasSelection);
    void trackAvailabilityChanged(bool hasTracks);
    void audioPoolChanged();
    void statusChanged(const QString& message);

private slots:
    void advancePlayback();
    void advanceSelectionFade();

private:
    [[nodiscard]] bool needsTrackingFrameProcessing() const;
    void updateCurrentGrayFrameIfNeeded();
    bool loadFrameAt(int frameIndex);
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] std::optional<int> trimmedEndFrameForTrack(const TrackPoint& track) const;
    void syncAttachedAudioForCurrentFrame();
    void refreshOverlays();
    void emitCurrentFrame();
    [[nodiscard]] bool isTrackSelected(const QUuid& trackId) const;
    void setSelectedTrackId(const QUuid& trackId, bool fadePreviousSelection = true);

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
    std::vector<QUuid> m_selectedTrackIds;
    QUuid m_selectedTrackId;
    QUuid m_fadingDeselectedTrackId;
    float m_fadingDeselectedTrackOpacity = 0.0F;
    QTimer m_selectionFadeTimer;
    const QUuid m_embeddedVideoAudioTrackId = QUuid(QStringLiteral("{eb6fc60f-0781-433f-9f03-ff16531165f7}"));
};
