#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QString>

#include "core/video/VideoDecoder.h"
#include "core/video/VideoFrame.h"
#include "core/video/VideoFrameQueue.h"

class VideoPlaybackService
{
public:
    VideoPlaybackService();

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

    bool seekFrame(int frameIndex);
    [[nodiscard]] std::optional<VideoFrame> stepForward();
    [[nodiscard]] double frameTimestampSeconds(int frameIndex) const;
    [[nodiscard]] int frameIndexForPresentationTime(double targetTimestampSeconds, int currentFrameIndex) const;

private:
    void cacheFrameTimestamp(const VideoFrame& frame);
    void prefetchFrames(std::size_t desiredQueuedFrames);

    std::unique_ptr<VideoDecoder> m_decoder;
    VideoFrameQueue m_frameQueue;
    std::vector<double> m_frameTimestampsSeconds;
    QString m_loadedPath;
    VideoFrame m_currentFrame;
    int m_totalFrames = 0;
    double m_fps = 0.0;
};
