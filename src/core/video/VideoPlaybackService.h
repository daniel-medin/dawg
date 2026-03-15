#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QString>

#include "core/video/VideoDecoder.h"
#include "core/video/VideoFrame.h"
#include "core/video/VideoFrameQueue.h"

struct VideoPlaybackRuntimeStats
{
    int queuedFrames = 0;
    int prefetchTargetFrames = 0;
    bool reachedEndOfStream = false;
    qint64 lastStepWaitMs = 0;
    bool lastStepUsedSynchronousFallback = false;
    std::uint64_t queueStarvationCount = 0;
};

class VideoPlaybackService
{
public:
    VideoPlaybackService();
    ~VideoPlaybackService();

    bool open(const QString& filePath);
    void close();
    [[nodiscard]] bool hasVideoLoaded() const;
    [[nodiscard]] QString loadedPath() const;
    [[nodiscard]] int totalFrames() const;
    [[nodiscard]] double fps() const;
    [[nodiscard]] cv::Size frameSize() const;
    [[nodiscard]] const VideoFrame& currentFrame() const;
    [[nodiscard]] QString decoderBackendName() const;
    [[nodiscard]] bool isHardwareDecoded() const;
    bool setCpuFrameExtractionEnabled(bool enabled);
    [[nodiscard]] bool cpuFrameExtractionEnabled() const;
    bool setPresentationScale(double scale);
    [[nodiscard]] double presentationScale() const;

    bool seekFrame(int frameIndex);
    [[nodiscard]] std::optional<VideoFrame> stepForward();
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] int frameIndexForPresentationTime(double targetTimestampSeconds, int currentFrameIndex) const;
    [[nodiscard]] VideoPlaybackRuntimeStats runtimeStats() const;

private:
    void startPrefetchThread();
    void stopPrefetchThread();
    void requestPrefetch();
    void prefetchLoop();
    void cacheFrameLocked(const VideoFrame& frame);
    void cacheFrameTimestampLocked(const VideoFrame& frame);
    void cacheFrameTimestamp(const VideoFrame& frame);
    [[nodiscard]] std::optional<VideoFrame> findCachedFrameLocked(int frameIndex) const;
    [[nodiscard]] std::optional<VideoFrame> decodeFrameAt(int frameIndex);
    void prefetchFrames(std::size_t desiredQueuedFrames);

    mutable std::mutex m_decoderMutex;
    mutable std::mutex m_stateMutex;
    std::condition_variable m_prefetchCv;
    std::thread m_prefetchThread;
    std::unique_ptr<VideoDecoder> m_decoder;
    VideoFrameQueue m_frameQueue;
    std::deque<VideoFrame> m_recentFrameCache;
    std::vector<double> m_frameTimestampsSeconds;
    QString m_loadedPath;
    VideoFrame m_currentFrame;
    int m_totalFrames = 0;
    double m_fps = 0.0;
    double m_presentationScale = 1.0;
    bool m_cpuFrameExtractionEnabled = true;
    std::size_t m_prefetchTargetSize = 8;
    bool m_stopPrefetch = false;
    bool m_reachedEndOfStream = false;
    bool m_decoderNeedsRealign = false;
    qint64 m_lastStepWaitMs = 0;
    bool m_lastStepUsedSynchronousFallback = false;
    std::uint64_t m_queueStarvationCount = 0;
    std::uint64_t m_prefetchGeneration = 0;
};
