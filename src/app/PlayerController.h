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

class PlayerController final : public QObject
{
    Q_OBJECT

public:
    explicit PlayerController(QObject* parent = nullptr);

    bool openVideo(const QString& filePath);
    void goToStart();
    void togglePlayback();
    void pause();
    void stepBackward();
    void stepForward();
    void seedTrack(const QPointF& imagePoint);
    void selectTrack(const QUuid& trackId);
    void clearSelection();
    void moveSelectedTrack(const QPointF& imagePoint);
    void deleteSelectedTrack();
    void setMotionTrackingEnabled(bool enabled);

    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isMotionTrackingEnabled() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] int currentFrameIndex() const;
    [[nodiscard]] int totalFrames() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] QString loadedPath() const;
    [[nodiscard]] const std::vector<TrackOverlay>& currentOverlays() const;

signals:
    void frameReady(const QImage& image, int frameIndex, double timestampSeconds);
    void overlaysChanged();
    void videoLoaded(const QString& filePath, int totalFrames, double fps);
    void playbackStateChanged(bool playing);
    void motionTrackingChanged(bool enabled);
    void selectionChanged(bool hasSelection);
    void statusChanged(const QString& message);

private slots:
    void advancePlayback();

private:
    bool loadFrameAt(int frameIndex);
    void refreshOverlays();
    void emitCurrentFrame();
    void setSelectedTrackId(const QUuid& trackId);
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
    bool m_motionTrackingEnabled = false;
    QUuid m_selectedTrackId;
};
