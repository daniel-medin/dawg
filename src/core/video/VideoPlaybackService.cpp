#include "core/video/VideoPlaybackService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "core/video/FfmpegVideoDecoder.h"
#include "core/video/OpenCvVideoDecoder.h"

namespace
{
std::unique_ptr<VideoDecoder> createVideoDecoder()
{
#if DAWG_HAS_FFMPEG
    return std::make_unique<FfmpegVideoDecoder>();
#else
    return std::make_unique<OpenCvVideoDecoder>();
#endif
}
}

VideoPlaybackService::VideoPlaybackService()
    : m_frameQueue(3)
{
}

bool VideoPlaybackService::open(const QString& filePath)
{
    close();

    auto decoder = createVideoDecoder();
    if (!decoder->open(filePath.toStdString()))
    {
        decoder = std::make_unique<OpenCvVideoDecoder>();
        if (!decoder->open(filePath.toStdString()))
        {
            return false;
        }
    }

    const auto firstFrame = decoder->readFrame();
    if (!firstFrame.has_value() || !firstFrame->isValid())
    {
        return false;
    }

    m_loadedPath = filePath;
    m_totalFrames = decoder->frameCount();
    m_fps = decoder->fps();
    m_currentFrame = *firstFrame;
    m_decoder = std::move(decoder);
    cacheFrameTimestamp(m_currentFrame);
    prefetchFrames(2);
    return true;
}

void VideoPlaybackService::close()
{
    m_decoder.reset();
    m_frameQueue.clear();
    m_frameTimestampsSeconds.clear();
    m_loadedPath.clear();
    m_currentFrame = {};
    m_totalFrames = 0;
    m_fps = 0.0;
}

bool VideoPlaybackService::hasVideoLoaded() const
{
    return m_decoder != nullptr && m_currentFrame.isValid();
}

QString VideoPlaybackService::loadedPath() const
{
    return m_loadedPath;
}

int VideoPlaybackService::totalFrames() const
{
    return m_totalFrames;
}

double VideoPlaybackService::fps() const
{
    return m_fps;
}

cv::Size VideoPlaybackService::frameSize() const
{
    return m_currentFrame.frameSize;
}

const VideoFrame& VideoPlaybackService::currentFrame() const
{
    return m_currentFrame;
}

QString VideoPlaybackService::decoderBackendName() const
{
    return m_decoder ? m_decoder->backendName() : QStringLiteral("Unknown Decoder");
}

bool VideoPlaybackService::isHardwareDecoded() const
{
    return m_decoder && m_decoder->isHardwareAccelerated();
}

bool VideoPlaybackService::seekFrame(const int frameIndex)
{
    if (!hasVideoLoaded())
    {
        return false;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto targetFrameIndex = std::clamp(frameIndex, 0, maxFrameIndex);
    if (targetFrameIndex == m_currentFrame.index)
    {
        return true;
    }

    if (targetFrameIndex > m_currentFrame.index && (targetFrameIndex - m_currentFrame.index) <= 8)
    {
        while (m_currentFrame.index < targetFrameIndex)
        {
            const auto nextFrame = stepForward();
            if (!nextFrame.has_value() || !nextFrame->isValid())
            {
                return false;
            }
        }

        return true;
    }

    if (!m_decoder->seekFrame(targetFrameIndex))
    {
        return false;
    }

    m_frameQueue.clear();
    while (true)
    {
        const auto frame = m_decoder->readFrame();
        if (!frame.has_value() || !frame->isValid())
        {
            return false;
        }

        cacheFrameTimestamp(*frame);
        if (frame->index >= targetFrameIndex)
        {
            m_currentFrame = *frame;
            return true;
        }
    }
}

std::optional<VideoFrame> VideoPlaybackService::stepForward()
{
    if (!hasVideoLoaded())
    {
        return std::nullopt;
    }

    while (!m_frameQueue.empty())
    {
        const auto nextFrame = m_frameQueue.takeFront();
        if (!nextFrame.has_value() || !nextFrame->isValid())
        {
            continue;
        }

        if (nextFrame->index <= m_currentFrame.index)
        {
            continue;
        }

        m_currentFrame = *nextFrame;
        prefetchFrames(2);
        return m_currentFrame;
    }

    const auto nextFrame = m_decoder->readFrame();
    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        return std::nullopt;
    }

    cacheFrameTimestamp(*nextFrame);
    m_currentFrame = *nextFrame;
    prefetchFrames(2);
    return m_currentFrame;
}

double VideoPlaybackService::frameTimestampSeconds(const int frameIndex) const
{
    if (frameIndex >= 0
        && frameIndex < static_cast<int>(m_frameTimestampsSeconds.size())
        && m_frameTimestampsSeconds[static_cast<std::size_t>(frameIndex)] >= 0.0)
    {
        return m_frameTimestampsSeconds[static_cast<std::size_t>(frameIndex)];
    }

    const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
    return frameIndex >= 0 ? (static_cast<double>(frameIndex) / safeFps) : 0.0;
}

int VideoPlaybackService::frameIndexForPresentationTime(const double targetTimestampSeconds, const int currentFrameIndex) const
{
    int bestFrameIndex = std::max(0, currentFrameIndex);
    double bestTimestamp = frameTimestampSeconds(bestFrameIndex);

    for (int frameIndex = 0; frameIndex < static_cast<int>(m_frameTimestampsSeconds.size()); ++frameIndex)
    {
        const auto timestampSeconds = m_frameTimestampsSeconds[static_cast<std::size_t>(frameIndex)];
        if (timestampSeconds < 0.0)
        {
            continue;
        }

        if (timestampSeconds <= targetTimestampSeconds)
        {
            bestFrameIndex = frameIndex;
            bestTimestamp = timestampSeconds;
            continue;
        }

        if (std::abs(timestampSeconds - targetTimestampSeconds) < std::abs(bestTimestamp - targetTimestampSeconds))
        {
            bestFrameIndex = frameIndex;
        }
        break;
    }

    if (m_frameTimestampsSeconds.empty())
    {
        const auto safeFps = m_fps > 0.0 ? m_fps : 30.0;
        bestFrameIndex = static_cast<int>(std::floor(targetTimestampSeconds * safeFps));
    }

    return std::clamp(bestFrameIndex, 0, std::max(0, m_totalFrames - 1));
}

void VideoPlaybackService::cacheFrameTimestamp(const VideoFrame& frame)
{
    if (frame.index < 0)
    {
        return;
    }

    if (frame.index >= static_cast<int>(m_frameTimestampsSeconds.size()))
    {
        m_frameTimestampsSeconds.resize(frame.index + 1, -1.0);
    }

    m_frameTimestampsSeconds[static_cast<std::size_t>(frame.index)] = frame.timestampSeconds;
}

void VideoPlaybackService::prefetchFrames(const std::size_t desiredQueuedFrames)
{
    if (!m_decoder)
    {
        return;
    }

    while (m_frameQueue.size() < desiredQueuedFrames)
    {
        const auto decodedFrame = m_decoder->readFrame();
        if (!decodedFrame.has_value() || !decodedFrame->isValid())
        {
            break;
        }

        cacheFrameTimestamp(*decodedFrame);
        if (decodedFrame->index <= m_currentFrame.index)
        {
            continue;
        }

        m_frameQueue.push(*decodedFrame);
    }
}
