#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QObject>
#include <QUuid>
#include <QString>
#include <QTimer>

#include <opencv2/core/mat.hpp>

#include "core/tracking/MotionTracker.h"
#include "core/video/DecodedFrame.h"
#include "core/video/VideoDecoder.h"
#include "ui/TimelineView.h"

class PlayerController final : public QObject
{
    Q_OBJECT

public:
    explicit PlayerController(QObject* parent = nullptr);

    bool openVideo(const QString& filePath);
    void goToStart();
    void togglePlayback();
    void pause(bool restorePlaybackAnchor = true);
    void seekToFrame(int frameIndex);
    void stepBackward();
    void stepForward();
    void seedTrack(const QPointF& imagePoint);
    void selectTrack(const QUuid& trackId);
    void clearSelection();
    void moveSelectedTrack(const QPointF& imagePoint);
    void deleteSelectedTrack();
    void clearAllTracks();
    void setSelectedTrackStartToCurrentFrame();
    void setSelectedTrackEndToCurrentFrame();
    void setAllTracksStartToCurrentFrame();
    void setAllTracksEndToCurrentFrame();
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
    [[nodiscard]] std::vector<TimelineTrackSpan> timelineTrackSpans() const;
    [[nodiscard]] const std::vector<TrackOverlay>& currentOverlays() const;

signals:
    void frameReady(const QImage& image, int frameIndex, double timestampSeconds);
    void overlaysChanged();
    void videoLoaded(const QString& filePath, int totalFrames, double fps);
    void playbackStateChanged(bool playing);
    void insertionFollowsPlaybackChanged(bool enabled);
    void motionTrackingChanged(bool enabled);
    void selectionChanged(bool hasSelection);
    void trackAvailabilityChanged(bool hasTracks);
    void statusChanged(const QString& message);

private slots:
    void advancePlayback();
    void advanceSelectionFade();

private:
    bool loadFrameAt(int frameIndex);
    void refreshOverlays();
    void emitCurrentFrame();
    void setSelectedTrackId(const QUuid& trackId, bool fadePreviousSelection = true);
    [[nodiscard]] QImage toImage(const cv::Mat& bgrFrame) const;

    std::unique_ptr<VideoDecoder> m_decoder;
    MotionTracker m_tracker;
    QTimer m_playbackTimer;
    QString m_loadedPath;
    DecodedFrame m_currentFrame;
    cv::Mat m_currentGrayFrame;
    std::vector<TrackOverlay> m_currentOverlays;
    int m_totalFrames = 0;
    double m_fps = 0.0;
    bool m_isPlaying = false;
    bool m_insertionFollowsPlayback = true;
    bool m_motionTrackingEnabled = false;
    int m_playbackAnchorFrame = -1;
    QUuid m_selectedTrackId;
    QUuid m_fadingDeselectedTrackId;
    float m_fadingDeselectedTrackOpacity = 0.0F;
    QTimer m_selectionFadeTimer;
};
