#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include <QElapsedTimer>
#include <QImage>
#include <QString>

#include <opencv2/core/mat.hpp>

#include "app/PerformanceLogger.h"
#include "app/TransportController.h"
#include "core/render/RenderService.h"
#include "core/video/AnalysisFrameProvider.h"
#include "core/video/VideoFrame.h"
#include "core/video/VideoPlaybackService.h"

class MotionTracker;

class VideoPlaybackCoordinator
{
public:
    struct PresentedFrame
    {
        QImage image;
        int frameIndex = -1;
        double timestampSeconds = 0.0;
    };

    struct OpenVideoResult
    {
        bool success = false;
        QString errorMessage;
    };

    struct SeekOutcome
    {
        enum class Status
        {
            NoVideo,
            Unchanged,
            Failed,
            Success
        };

        Status status = Status::NoVideo;
        int targetFrameIndex = -1;
        qint64 elapsedMs = 0;
        VideoPlaybackRuntimeStats runtimeStats;
    };

    struct RelativeSeekOutcome
    {
        enum class Status
        {
            NoVideo,
            Boundary,
            Failed,
            Success
        };

        Status status = Status::NoVideo;
        int targetFrameIndex = -1;
    };

    struct FrameCallbacks
    {
        std::function<void()> onFrameChanged;
        std::function<void()> onSyncAudio;
        std::function<void(bool)> onPausePlayback;
        std::function<void(const QString&)> onStatusChanged;
    };

    struct PlaybackCallbacks : FrameCallbacks
    {
        std::function<std::optional<std::pair<int, int>>()> activeLoopRange;
    };

    VideoPlaybackCoordinator(MotionTracker& tracker, TransportController& transport, PerformanceLogger& perfLogger);

    void resetState();
    void close();
    [[nodiscard]] OpenVideoResult openVideo(const QString& filePath);

    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] const QString& loadedPath() const;
    [[nodiscard]] int totalFrames() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] const VideoFrame& currentFrame() const;
    [[nodiscard]] bool hasEmbeddedVideoAudio() const;
    [[nodiscard]] const QString& embeddedVideoAudioPath() const;
    [[nodiscard]] const QString& embeddedVideoAudioDisplayName() const;
    [[nodiscard]] QString decoderBackendName() const;
    [[nodiscard]] bool videoHardwareAccelerated() const;
    [[nodiscard]] QString renderBackendName() const;
    [[nodiscard]] bool renderHardwareAccelerated() const;
    [[nodiscard]] RenderService& renderService();
    [[nodiscard]] const RenderService& renderService() const;
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] VideoPlaybackRuntimeStats runtimeStats() const;
    [[nodiscard]] PresentedFrame presentCurrentFrame(bool playbackActive);

    void restartPlaybackTiming();
    [[nodiscard]] bool loadFrameAt(int frameIndex, const std::function<void()>& onFrameChanged);
    [[nodiscard]] bool stepForward(bool syncAudio, const FrameCallbacks& callbacks);
    [[nodiscard]] RelativeSeekOutcome seekRelativeFrames(int deltaFrames, const std::function<void()>& onFrameChanged);
    [[nodiscard]] SeekOutcome seekToFrame(
        int frameIndex,
        bool isPlaying,
        const std::function<void()>& onFrameChanged,
        const std::function<void()>& onSyncAudio);
    void advancePlayback(const PlaybackCallbacks& callbacks);
    [[nodiscard]] bool applyPresentationScaleForPlaybackState(
        bool playbackActive,
        const std::function<void()>& onFrameChanged);

private:
    [[nodiscard]] bool advanceOneFrame(bool presentFrame, bool syncAudio, const FrameCallbacks& callbacks);
    [[nodiscard]] bool needsTrackingFrameProcessing() const;
    void updateCpuFrameExtractionMode();
    void updateCurrentGrayFrameIfNeeded();
    void logPlaybackHitchIfNeeded(int targetFrameIndex, int previousFrameIndex, int advancedFrames);

    MotionTracker& m_tracker;
    TransportController& m_transport;
    PerformanceLogger& m_perfLogger;
    VideoPlaybackService m_videoPlayback;
    RenderService m_renderService;
    AnalysisFrameProvider m_analysisFrameProvider;
    QString m_loadedPath;
    QString m_embeddedVideoAudioPath;
    QString m_embeddedVideoAudioDisplayName;
    VideoFrame m_currentFrame;
    cv::Mat m_currentGrayFrame;
    int m_totalFrames = 0;
    double m_fps = 0.0;
    double m_playbackStartTimestampSeconds = 0.0;
    QElapsedTimer m_playbackElapsedTimer;
    QElapsedTimer m_perfPlaybackTickTimer;
    std::uint64_t m_lastLoggedQueueStarvationCount = 0;
};
