#include "ui/TimelineThumbnailCache.h"

#include <algorithm>
#include <memory>

#include <QImage>
#include <QMetaObject>

#include "core/video/FfmpegVideoDecoder.h"
#include "core/video/OpenCvVideoDecoder.h"
#include "core/video/VideoDecoder.h"

namespace
{
std::unique_ptr<VideoDecoder> createThumbnailDecoder()
{
#if DAWG_HAS_FFMPEG
    return std::make_unique<FfmpegVideoDecoder>();
#else
    return std::make_unique<OpenCvVideoDecoder>();
#endif
}

QString thumbnailKey(const QString& videoPath, const int frameIndex)
{
    return QStringLiteral("%1|%2").arg(videoPath).arg(frameIndex);
}
}

TimelineThumbnailCache::TimelineThumbnailCache(QObject* parent)
    : QObject(parent)
{
    m_workerThread = std::thread([this]()
    {
        workerLoop();
    });
}

TimelineThumbnailCache::~TimelineThumbnailCache()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_condition.notify_all();
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }
}

void TimelineThumbnailCache::clear()
{
    const std::lock_guard lock(m_mutex);
    m_requestedPath.clear();
    m_pendingFrames.clear();
    m_pendingFrameSet.clear();
    m_thumbnailImages.clear();
}

void TimelineThumbnailCache::requestFrames(const QString& videoPath, const QVector<int>& frameIndices)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_requestedPath != videoPath)
        {
            m_requestedPath = videoPath;
            m_pendingFrames.clear();
            m_pendingFrameSet.clear();
            m_thumbnailImages.clear();
        }

        for (const auto frameIndex : frameIndices)
        {
            if (frameIndex < 0)
            {
                continue;
            }
            if (m_thumbnailImages.contains(thumbnailKey(videoPath, frameIndex)))
            {
                continue;
            }
            if (m_pendingFrameSet.insert(frameIndex).second)
            {
                m_pendingFrames.push_back(frameIndex);
            }
        }
    }
    m_condition.notify_all();
}

bool TimelineThumbnailCache::hasThumbnail(const QString& videoPath, const int frameIndex) const
{
    const std::lock_guard lock(m_mutex);
    return m_thumbnailImages.contains(thumbnailKey(videoPath, frameIndex));
}

QImage TimelineThumbnailCache::thumbnail(const QString& videoPath, const int frameIndex) const
{
    const std::lock_guard lock(m_mutex);
    return m_thumbnailImages.value(thumbnailKey(videoPath, frameIndex));
}

void TimelineThumbnailCache::workerLoop()
{
    while (true)
    {
        QString videoPath;
        int frameIndex = -1;
        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this]()
            {
                return m_stop || !m_pendingFrames.empty();
            });

            if (m_stop)
            {
                return;
            }

            videoPath = m_requestedPath;
            frameIndex = m_pendingFrames.front();
            m_pendingFrames.pop_front();
            m_pendingFrameSet.erase(frameIndex);
        }

        if (videoPath.isEmpty() || frameIndex < 0)
        {
            continue;
        }

        if (!ensureDecoderOpen(videoPath))
        {
            continue;
        }

        if (!m_decoder || !m_decoder->seekFrame(frameIndex))
        {
            continue;
        }

        std::optional<VideoFrame> frame;
        while (true)
        {
            frame = m_decoder->readFrame();
            if (!frame.has_value() || !frame->isValid())
            {
                break;
            }

            if (frame->index >= frameIndex)
            {
                break;
            }
        }

        if (!frame.has_value() || !frame->hasCpuImage())
        {
            continue;
        }

        const auto thumbnailImage = frame->cpuImage.scaled(
            QSize{112, 64},
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        QMetaObject::invokeMethod(
            this,
            [this, videoPath, frameIndex, thumbnailImage]()
            {
                {
                    const std::lock_guard lock(m_mutex);
                    if (m_requestedPath != videoPath)
                    {
                        return;
                    }
                    m_thumbnailImages.insert(thumbnailKey(videoPath, frameIndex), thumbnailImage);
                }
                emit thumbnailReady(videoPath, frameIndex);
            },
            Qt::QueuedConnection);
    }
}

bool TimelineThumbnailCache::ensureDecoderOpen(const QString& videoPath)
{
    if (m_decoder && m_decoderPath == videoPath && m_decoder->isOpen())
    {
        return true;
    }

    auto decoder = createThumbnailDecoder();
    decoder->setCpuFrameExtractionEnabled(true);
    decoder->setOutputScale(0.18);
    if (!decoder->open(videoPath.toStdString()))
    {
        decoder = std::make_unique<OpenCvVideoDecoder>();
        decoder->setCpuFrameExtractionEnabled(true);
        decoder->setOutputScale(0.18);
        if (!decoder->open(videoPath.toStdString()))
        {
            m_decoder.reset();
            m_decoderPath.clear();
            return false;
        }
    }

    m_decoder = std::move(decoder);
    m_decoderPath = videoPath;
    return true;
}

TimelineThumbnailCache& timelineThumbnailCache()
{
    static TimelineThumbnailCache cache;
    return cache;
}
