#include "core/video/VideoPlaybackService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>

#include "core/video/FfmpegVideoDecoder.h"
#include "core/video/OpenCvVideoDecoder.h"

namespace
{
constexpr std::size_t kRecentFrameCacheSize = 24;

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
    : m_frameQueue(12)
{
}

VideoPlaybackService::~VideoPlaybackService()
{
    close();
}

bool VideoPlaybackService::open(const QString& filePath)
{
    close();

    auto decoder = createVideoDecoder();
    decoder->setCpuFrameExtractionEnabled(m_cpuFrameExtractionEnabled);
    decoder->setOutputScale(m_presentationScale);
    decoder->setPreferredD3D11Device(m_preferredD3D11Device);
    if (!decoder->open(filePath.toStdString()))
    {
        decoder = std::make_unique<OpenCvVideoDecoder>();
        decoder->setCpuFrameExtractionEnabled(m_cpuFrameExtractionEnabled);
        decoder->setOutputScale(m_presentationScale);
        decoder->setPreferredD3D11Device(m_preferredD3D11Device);
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

    {
        const std::lock_guard decoderLock(m_decoderMutex);
        m_decoder = std::move(decoder);
    }

    {
        const std::lock_guard stateLock(m_stateMutex);
        m_loadedPath = filePath;
        m_totalFrames = m_decoder ? m_decoder->frameCount() : 0;
        m_fps = m_decoder ? m_decoder->fps() : 0.0;
        m_currentFrame = *firstFrame;
        m_frameQueue.clear();
        m_recentFrameCache.clear();
        m_frameTimestampsSeconds.clear();
        m_reachedEndOfStream = false;
        m_decoderNeedsRealign = false;
        m_lastStepWaitMs = 0;
        m_lastStepUsedSynchronousFallback = false;
        m_queueStarvationCount = 0;
        ++m_prefetchGeneration;
        cacheFrameLocked(m_currentFrame);
    }

    startPrefetchThread();
    requestPrefetch();
    return true;
}

void VideoPlaybackService::close()
{
    stopPrefetchThread();

    {
        const std::lock_guard decoderLock(m_decoderMutex);
        m_decoder.reset();
    }

    const std::lock_guard stateLock(m_stateMutex);
    m_frameQueue.clear();
    m_recentFrameCache.clear();
    m_frameTimestampsSeconds.clear();
    m_loadedPath.clear();
    m_currentFrame = {};
        m_totalFrames = 0;
        m_fps = 0.0;
        m_presentationScale = 1.0;
        m_cpuFrameExtractionEnabled = true;
        m_reachedEndOfStream = false;
    m_decoderNeedsRealign = false;
    m_lastStepWaitMs = 0;
    m_lastStepUsedSynchronousFallback = false;
    m_queueStarvationCount = 0;
    ++m_prefetchGeneration;
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

bool VideoPlaybackService::setPresentationScale(const double scale)
{
    const auto clampedScale = std::clamp(scale, 0.1, 1.0);

    {
        const std::lock_guard stateLock(m_stateMutex);
        if (std::abs(m_presentationScale - clampedScale) < 0.001)
        {
            return true;
        }
    }

    if (!hasVideoLoaded())
    {
        const std::lock_guard stateLock(m_stateMutex);
        m_presentationScale = clampedScale;
        return true;
    }

    const auto currentFrameIndex = m_currentFrame.index;
    {
        const std::lock_guard decoderLock(m_decoderMutex);
        if (m_decoder)
        {
            m_decoder->setOutputScale(clampedScale);
        }
    }

    const auto reloadedFrame = decodeFrameAt(currentFrameIndex);
    if (!reloadedFrame.has_value() || !reloadedFrame->isValid())
    {
        return false;
    }

    {
        const std::lock_guard stateLock(m_stateMutex);
        m_presentationScale = clampedScale;
        m_frameQueue.clear();
        m_recentFrameCache.clear();
        m_currentFrame = *reloadedFrame;
        m_reachedEndOfStream = false;
        m_decoderNeedsRealign = false;
        m_lastStepWaitMs = 0;
        m_lastStepUsedSynchronousFallback = false;
        ++m_prefetchGeneration;
        cacheFrameLocked(m_currentFrame);
    }

    requestPrefetch();
    return true;
}

double VideoPlaybackService::presentationScale() const
{
    const std::lock_guard stateLock(m_stateMutex);
    return m_presentationScale;
}

void VideoPlaybackService::setPreferredD3D11Device(void* device)
{
    const std::lock_guard stateLock(m_stateMutex);
    m_preferredD3D11Device = device;
}

void* VideoPlaybackService::preferredD3D11Device() const
{
    const std::lock_guard stateLock(m_stateMutex);
    return m_preferredD3D11Device;
}

bool VideoPlaybackService::setCpuFrameExtractionEnabled(const bool enabled)
{
    {
        const std::lock_guard stateLock(m_stateMutex);
        if (m_cpuFrameExtractionEnabled == enabled)
        {
            return true;
        }
    }

    if (!hasVideoLoaded())
    {
        const std::lock_guard stateLock(m_stateMutex);
        m_cpuFrameExtractionEnabled = enabled;
        return true;
    }

    const auto currentFrameIndex = m_currentFrame.index;
    {
        const std::lock_guard decoderLock(m_decoderMutex);
        if (m_decoder)
        {
            m_decoder->setCpuFrameExtractionEnabled(enabled);
        }
    }

    const auto reloadedFrame = decodeFrameAt(currentFrameIndex);
    if (!reloadedFrame.has_value() || !reloadedFrame->isValid())
    {
        return false;
    }

    {
        const std::lock_guard stateLock(m_stateMutex);
        m_cpuFrameExtractionEnabled = enabled;
        m_frameQueue.clear();
        m_recentFrameCache.clear();
        m_currentFrame = *reloadedFrame;
        m_reachedEndOfStream = false;
        m_decoderNeedsRealign = false;
        m_lastStepWaitMs = 0;
        m_lastStepUsedSynchronousFallback = false;
        ++m_prefetchGeneration;
        cacheFrameLocked(m_currentFrame);
    }

    requestPrefetch();
    return true;
}

bool VideoPlaybackService::cpuFrameExtractionEnabled() const
{
    const std::lock_guard stateLock(m_stateMutex);
    return m_cpuFrameExtractionEnabled;
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

    {
        const std::lock_guard stateLock(m_stateMutex);
        if (const auto cachedFrame = findCachedFrameLocked(targetFrameIndex); cachedFrame.has_value())
        {
            m_frameQueue.clear();
            m_currentFrame = *cachedFrame;
            m_reachedEndOfStream = false;
            m_decoderNeedsRealign = true;
            m_lastStepWaitMs = 0;
            m_lastStepUsedSynchronousFallback = false;
            ++m_prefetchGeneration;
            cacheFrameLocked(m_currentFrame);
            requestPrefetch();
            return true;
        }
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

    const auto resolvedFrame = decodeFrameAt(targetFrameIndex);

    if (!resolvedFrame.has_value() || !resolvedFrame->isValid())
    {
        return false;
    }

    {
        const std::lock_guard stateLock(m_stateMutex);
        m_frameQueue.clear();
        m_currentFrame = *resolvedFrame;
        m_reachedEndOfStream = false;
        m_decoderNeedsRealign = false;
        ++m_prefetchGeneration;
        cacheFrameLocked(m_currentFrame);
    }

    requestPrefetch();
    return true;
}

std::optional<VideoFrame> VideoPlaybackService::stepForward()
{
    if (!hasVideoLoaded())
    {
        return std::nullopt;
    }

    qint64 waitMs = 0;
    {
        std::unique_lock stateLock(m_stateMutex);
        if (m_frameQueue.empty() && !m_reachedEndOfStream)
        {
            const auto waitStart = std::chrono::steady_clock::now();
            m_prefetchCv.notify_all();
            m_prefetchCv.wait_for(
                stateLock,
                std::chrono::milliseconds(10),
                [this]()
                {
                    return !m_frameQueue.empty() || m_reachedEndOfStream || m_stopPrefetch;
                });
            waitMs = static_cast<qint64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - waitStart).count());
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

            m_lastStepWaitMs = waitMs;
            m_lastStepUsedSynchronousFallback = false;
            m_currentFrame = *nextFrame;
            stateLock.unlock();
            requestPrefetch();
            return m_currentFrame;
        }
    }

    std::optional<VideoFrame> nextFrame;
    {
        int alignFrameIndex = -1;
        {
            const std::lock_guard stateLock(m_stateMutex);
            if (m_decoderNeedsRealign)
            {
                alignFrameIndex = std::min(std::max(0, m_totalFrames - 1), m_currentFrame.index + 1);
            }
        }

        const std::lock_guard decoderLock(m_decoderMutex);
        if (m_decoder)
        {
            if (alignFrameIndex >= 0)
            {
                m_decoder->seekFrame(alignFrameIndex);
            }
            nextFrame = m_decoder->readFrame();
        }
    }

    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        const std::lock_guard stateLock(m_stateMutex);
        m_reachedEndOfStream = true;
        m_lastStepWaitMs = waitMs;
        m_lastStepUsedSynchronousFallback = false;
        return std::nullopt;
    }

    {
        const std::lock_guard stateLock(m_stateMutex);
        if (m_decoderNeedsRealign)
        {
            m_decoderNeedsRealign = false;
        }
        cacheFrameLocked(*nextFrame);
        m_lastStepWaitMs = waitMs;
        m_lastStepUsedSynchronousFallback = true;
        ++m_queueStarvationCount;
        m_currentFrame = *nextFrame;
    }
    requestPrefetch();
    return m_currentFrame;
}

double VideoPlaybackService::frameTimestampSeconds(const int frameIndex) const
{
    const std::lock_guard stateLock(m_stateMutex);
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
    const std::lock_guard stateLock(m_stateMutex);
    int bestFrameIndex = std::max(0, currentFrameIndex);
    double bestTimestamp = (bestFrameIndex >= 0
        && bestFrameIndex < static_cast<int>(m_frameTimestampsSeconds.size())
        && m_frameTimestampsSeconds[static_cast<std::size_t>(bestFrameIndex)] >= 0.0)
            ? m_frameTimestampsSeconds[static_cast<std::size_t>(bestFrameIndex)]
            : ((bestFrameIndex >= 0)
                ? (static_cast<double>(bestFrameIndex) / (m_fps > 0.0 ? m_fps : 30.0))
                : 0.0);

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

VideoPlaybackRuntimeStats VideoPlaybackService::runtimeStats() const
{
    const std::lock_guard stateLock(m_stateMutex);
    return VideoPlaybackRuntimeStats{
        .queuedFrames = static_cast<int>(m_frameQueue.size()),
        .prefetchTargetFrames = static_cast<int>(m_prefetchTargetSize),
        .reachedEndOfStream = m_reachedEndOfStream,
        .lastStepWaitMs = m_lastStepWaitMs,
        .lastStepUsedSynchronousFallback = m_lastStepUsedSynchronousFallback,
        .queueStarvationCount = m_queueStarvationCount,
    };
}

void VideoPlaybackService::cacheFrameTimestamp(const VideoFrame& frame)
{
    const std::lock_guard stateLock(m_stateMutex);
    cacheFrameTimestampLocked(frame);
}

void VideoPlaybackService::cacheFrameLocked(const VideoFrame& frame)
{
    cacheFrameTimestampLocked(frame);

    auto existingFrame = std::find_if(
        m_recentFrameCache.begin(),
        m_recentFrameCache.end(),
        [&frame](const VideoFrame& cachedFrame)
        {
            return cachedFrame.index == frame.index;
        });
    if (existingFrame != m_recentFrameCache.end())
    {
        m_recentFrameCache.erase(existingFrame);
    }

    m_recentFrameCache.push_back(frame);
    while (m_recentFrameCache.size() > kRecentFrameCacheSize)
    {
        m_recentFrameCache.pop_front();
    }
}

void VideoPlaybackService::cacheFrameTimestampLocked(const VideoFrame& frame)
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

std::optional<VideoFrame> VideoPlaybackService::findCachedFrameLocked(const int frameIndex) const
{
    auto frameIt = std::find_if(
        m_recentFrameCache.rbegin(),
        m_recentFrameCache.rend(),
        [frameIndex](const VideoFrame& cachedFrame)
        {
            return cachedFrame.index == frameIndex;
        });
    if (frameIt == m_recentFrameCache.rend())
    {
        return std::nullopt;
    }

    return *frameIt;
}

std::optional<VideoFrame> VideoPlaybackService::decodeFrameAt(const int frameIndex)
{
    const std::lock_guard decoderLock(m_decoderMutex);
    if (!m_decoder || !m_decoder->seekFrame(frameIndex))
    {
        return std::nullopt;
    }

    while (true)
    {
        const auto frame = m_decoder->readFrame();
        if (!frame.has_value() || !frame->isValid())
        {
            return std::nullopt;
        }

        if (frame->index >= frameIndex)
        {
            return frame;
        }
    }
}

void VideoPlaybackService::prefetchFrames(const std::size_t desiredQueuedFrames)
{
    m_prefetchTargetSize = std::max<std::size_t>(1, desiredQueuedFrames);
    requestPrefetch();
}

void VideoPlaybackService::startPrefetchThread()
{
    stopPrefetchThread();
    {
        const std::lock_guard stateLock(m_stateMutex);
        m_stopPrefetch = false;
    }
    m_prefetchThread = std::thread(&VideoPlaybackService::prefetchLoop, this);
}

void VideoPlaybackService::stopPrefetchThread()
{
    {
        const std::lock_guard stateLock(m_stateMutex);
        m_stopPrefetch = true;
    }
    m_prefetchCv.notify_all();

    if (m_prefetchThread.joinable())
    {
        m_prefetchThread.join();
    }
}

void VideoPlaybackService::requestPrefetch()
{
    m_prefetchCv.notify_all();
}

void VideoPlaybackService::prefetchLoop()
{
    for (;;)
    {
        std::uint64_t generation = 0;
        {
            std::unique_lock stateLock(m_stateMutex);
            m_prefetchCv.wait(
                stateLock,
                [this]()
                {
                    return m_stopPrefetch
                        || m_decoderNeedsRealign
                        || (m_totalFrames > 0 && !m_reachedEndOfStream && m_frameQueue.size() < m_prefetchTargetSize);
                });

            if (m_stopPrefetch)
            {
                return;
            }

            generation = m_prefetchGeneration;
        }

        std::optional<VideoFrame> decodedFrame;
        {
            int alignFrameIndex = -1;
            {
                const std::lock_guard stateLock(m_stateMutex);
                if (m_decoderNeedsRealign)
                {
                    alignFrameIndex = std::min(std::max(0, m_totalFrames - 1), m_currentFrame.index + 1);
                }
            }

            const std::lock_guard decoderLock(m_decoderMutex);
            if (!m_decoder)
            {
                continue;
            }

            if (alignFrameIndex >= 0)
            {
                m_decoder->seekFrame(alignFrameIndex);
            }
            decodedFrame = m_decoder->readFrame();
        }

        {
            const std::lock_guard stateLock(m_stateMutex);
            if (m_stopPrefetch)
            {
                return;
            }

            if (generation != m_prefetchGeneration)
            {
                continue;
            }

            if (m_decoderNeedsRealign)
            {
                m_decoderNeedsRealign = false;
            }

            if (!decodedFrame.has_value() || !decodedFrame->isValid())
            {
                m_reachedEndOfStream = true;
                continue;
            }

            cacheFrameLocked(*decodedFrame);
            if (decodedFrame->index > m_currentFrame.index)
            {
                m_frameQueue.push(*decodedFrame);
            }
        }

        m_prefetchCv.notify_all();
    }
}
